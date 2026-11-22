"""Cached data-symbol lookup for the emulator's matching ARM ELF.

GNU nm on the host can read the ARM symbol table without running ARM code.
Prefer an installed cross nm, then host nm; retain the pinned container as a
fallback for hosts whose binutils cannot read this ELF. Never invoke a shell.
"""
from functools import lru_cache
import os
from pathlib import Path
import shutil
import subprocess

IMAGE = 'devkitpro/devkitarm:20240511'


def _stamp(path):
    try:
        st = os.stat(path)
    except OSError as exc:
        raise RuntimeError(f'cannot read symbol ELF {path}: {exc}') from exc
    # Replacement/in-place rebuilds invalidate the table even if a copy
    # preserves the old mtime and size. stat runs before every cache lookup,
    # so deleting the ELF cannot leave a silently usable stale address map.
    return st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns


@lru_cache(maxsize=4)
def _table(path, stamp):
    commands = []
    for tool in ('arm-none-eabi-nm', 'nm'):
        executable = shutil.which(tool)
        if executable:
            commands.append([executable, '--defined-only', path])
    p = Path(path)
    commands.append([
        'docker', 'run', '--rm', '-v', f'{p.parent}:/work:ro', IMAGE,
        '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm',
        '--defined-only', f'/work/{p.name}',
    ])
    failures = []
    for cmd in commands:
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exc:
            failures.append(f'{cmd[0]}: {exc}')
            continue
        if result.returncode:
            failures.append(f'{cmd[0]}: {result.stderr.strip() or "exit " + str(result.returncode)}')
            continue
        found = {}
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[1] in ('b', 'B', 'd', 'D'):
                try:
                    found[fields[2]] = int(fields[0], 16)
                except ValueError:
                    continue
        if _stamp(path) != stamp:
            raise RuntimeError(f'symbol ELF changed during lookup: {path}; retry after the build finishes')
        return found
    # lru_cache never caches exceptions: fixing the tool/ELF can be retried.
    raise RuntimeError(f'cannot read symbols from {path}: ' + '; '.join(failures))


def symbols(elf, names):
    """Addresses of defined data/BSS variables, resolved once per ELF version."""
    path = os.path.realpath(os.fspath(elf))
    table = _table(path, _stamp(path))
    names = tuple(names)
    missing = [name for name in names if name not in table]
    if missing:
        raise RuntimeError(f'symbols not found in {path}: {missing}')
    return {name: table[name] for name in names}


def symbol(elf, name):
    """Address of a defined data/BSS variable in the matching ROM ELF."""
    return symbols(elf, (name,))[name]
