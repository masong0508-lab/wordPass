"""Read global-struct member offsets from the matching built ELF's DWARF.

Never compile the current checkout to guess a possibly older ROM's ABI.
pyelftools is preferred; GNU readelf is a dependency-free host fallback.
Both backends follow typedefs and declaration/specification references and
reject bitfields or location expressions that are not simple byte offsets.
"""
from functools import lru_cache
import os
import re
import shutil
import subprocess

from elf_symbols import _stamp

QUALIFIERS = {'DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type',
              'DW_TAG_restrict_type', 'DW_TAG_atomic_type'}


def _pyelftools(path, variable):
    from elftools.elf.elffile import ELFFile
    from elftools.dwarf.descriptions import describe_form_class
    from elftools.dwarf.dwarf_expr import DWARFExprParser

    def inherited(die, name, seen=None):
        seen = set() if seen is None else seen
        if die.offset in seen:
            return None
        seen.add(die.offset)
        if name in die.attributes:
            return die, die.attributes[name]
        for ref in ('DW_AT_specification', 'DW_AT_abstract_origin'):
            if ref in die.attributes:
                found = inherited(die.get_DIE_from_attribute(ref), name, seen)
                if found:
                    return found
        return None

    result = []
    with open(path, 'rb') as source:
        elf = ELFFile(source)
        if not elf.has_dwarf_info():
            raise RuntimeError(f'ELF has no DWARF type information: {path}')
        dwarf = elf.get_dwarf_info()
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag != 'DW_TAG_variable' or 'DW_AT_location' not in die.attributes:
                    continue
                name = inherited(die, 'DW_AT_name')
                external = inherited(die, 'DW_AT_external')
                if not name or name[1].value.decode('utf-8') != variable or not external or not external[1].value:
                    continue
                ref = inherited(die, 'DW_AT_type')
                if not ref:
                    continue
                kind = ref[0].get_DIE_from_attribute('DW_AT_type')
                seen = set()
                while kind.tag in QUALIFIERS and kind.offset not in seen:
                    seen.add(kind.offset)
                    kind = kind.get_DIE_from_attribute('DW_AT_type')
                if kind.tag != 'DW_TAG_structure_type':
                    raise RuntimeError(f'{variable} is not a struct in {path}')
                layout = {}
                for member in kind.iter_children():
                    if member.tag != 'DW_TAG_member' or 'DW_AT_name' not in member.attributes:
                        continue
                    attrs = member.attributes
                    field = attrs['DW_AT_name'].value.decode('utf-8')
                    location = attrs.get('DW_AT_data_member_location')
                    offset = None
                    if location and not any(k in attrs for k in ('DW_AT_bit_size', 'DW_AT_data_bit_offset')):
                        form = describe_form_class(location.form)
                        if form == 'constant':
                            offset = location.value
                        elif form in ('exprloc', 'block'):
                            ops = DWARFExprParser(dwarf.structs).parse_expr(location.value)
                            if len(ops) == 1 and ops[0].op_name == 'DW_OP_plus_uconst':
                                offset = ops[0].args[0]
                    layout[field] = offset
                result.append(layout)
    return result


def _readelf(path, variable):
    executable = shutil.which('arm-none-eabi-readelf') or shutil.which('readelf')
    if not executable:
        raise RuntimeError('DWARF offsets require pyelftools or GNU readelf; install pyelftools')
    run = subprocess.run([executable, '--debug-dump=info', path],
                         capture_output=True, text=True, timeout=30,
                         env=dict(os.environ, LC_ALL='C'))
    if run.returncode:
        raise RuntimeError(f'readelf failed for {path}: {run.stderr.strip()}')
    nodes, parents, current = {}, [], None
    for line in run.stdout.splitlines():
        header = re.match(r'\s*<(\d+)><([0-9a-fA-F]+)>: Abbrev Number: \d+(?: \((DW_TAG_\w+)\))?', line)
        if header:
            depth, address, tag = int(header[1]), int(header[2], 16), header[3]
            parents = parents[:depth]
            current = None
            if not tag:
                continue
            current = {'tag': tag, 'attrs': {}, 'children': []}
            nodes[address] = current
            if parents:
                parents[-1]['children'].append(current)
            parents.append(current)
            continue
        attr = re.match(r'\s*<[0-9a-fA-F]+>\s+(DW_AT_\w+)\s*:\s*(.*)', line)
        if current is not None and attr:
            current['attrs'][attr[1]] = attr[2]

    def reference(node, attribute):
        raw = node['attrs'].get(attribute, '')
        target = re.fullmatch(r'<0x([0-9a-fA-F]+)>', raw)
        return nodes.get(int(target[1], 16)) if target else None

    def inherited(node, name, seen=None):
        seen = set() if seen is None else seen
        if id(node) in seen:
            return None
        seen.add(id(node))
        if name in node['attrs']:
            return node, node['attrs'][name]
        for ref in ('DW_AT_specification', 'DW_AT_abstract_origin'):
            target = reference(node, ref)
            if target:
                found = inherited(target, name, seen)
                if found:
                    return found
        return None

    def name(raw):
        # GNU readelf prefixes indirect/indexed strings with "(...): ".
        return re.sub(r'^\([^)]*\):\s*', '', raw)

    result = []
    for node in nodes.values():
        if node['tag'] != 'DW_TAG_variable' or 'DW_AT_location' not in node['attrs']:
            continue
        named, external = inherited(node, 'DW_AT_name'), inherited(node, 'DW_AT_external')
        if not named or name(named[1]) != variable or not external or external[1] != '1':
            continue
        ref = inherited(node, 'DW_AT_type')
        kind = reference(ref[0], 'DW_AT_type') if ref else None
        seen = set()
        while kind and kind['tag'] in QUALIFIERS and id(kind) not in seen:
            seen.add(id(kind))
            kind = reference(kind, 'DW_AT_type')
        if not kind or kind['tag'] != 'DW_TAG_structure_type':
            raise RuntimeError(f'{variable} is not a struct in {path}')
        layout = {}
        for member in kind['children']:
            attrs = member['attrs']
            if member['tag'] != 'DW_TAG_member' or 'DW_AT_name' not in attrs:
                continue
            raw, offset = attrs.get('DW_AT_data_member_location', ''), None
            if not any(k in attrs for k in ('DW_AT_bit_size', 'DW_AT_data_bit_offset')):
                if re.fullmatch(r'(?:0x[0-9a-fA-F]+|\d+)', raw):
                    offset = int(raw, 16 if raw.startswith('0x') else 10)
                else:
                    simple = re.fullmatch(r'\d+ byte block: [0-9a-fA-F ]+\s*\(DW_OP_plus_uconst: (\d+)\)', raw)
                    if simple:
                        offset = int(simple[1])
            layout[name(attrs['DW_AT_name'])] = offset
        result.append(layout)
    return result


@lru_cache(maxsize=8)
def _layout(path, stamp, variable, backend):
    if backend == 'readelf':
        candidates = _readelf(path, variable)
    else:
        try:
            candidates = _pyelftools(path, variable)
        except ImportError:
            if backend == 'pyelftools':
                raise RuntimeError('pyelftools is not installed') from None
            candidates = _readelf(path, variable)
    if not candidates:
        raise RuntimeError(f'no defined global struct {variable!r} with DWARF in {path}; use the matching unstripped ELF')
    if any(candidate != candidates[0] for candidate in candidates[1:]):
        raise RuntimeError(f'ambiguous DWARF layouts for {variable!r} in {path}')
    if _stamp(path) != stamp:
        raise RuntimeError(f'ELF changed while reading its ABI: {path}')
    return candidates[0]


def member_offsets(elf, variable, members, *, backend='auto'):
    """Byte offsets of direct, non-bitfield members of a defined global struct."""
    if backend not in ('auto', 'pyelftools', 'readelf'):
        raise ValueError(f'unknown DWARF backend {backend!r}')
    path = os.path.realpath(os.fspath(elf))
    layout = _layout(path, _stamp(path), variable, backend)
    result = {}
    for member in members:
        offset = layout.get(member)
        if not isinstance(offset, int) or offset < 0:
            raise RuntimeError(f'{variable}.{member} has no simple byte offset in {path}')
        result[member] = offset
    return result
