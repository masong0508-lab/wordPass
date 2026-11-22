#!/usr/bin/env python3
"""Compare emulator ABI discovery with compiler-emitted offsetof values."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'emu'))
import elf_abi


class ElfAbiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='wordpass-elf-abi-')
        self.root = Path(self.temp.name)
        self.elf = self.root / 'matching build.elf'
        elf_abi._layout.cache_clear()

    def tearDown(self):
        self.temp.cleanup()

    def compile(self, padding=3, dwarf=5):
        source = self.root / 'fixture.c'
        source.write_text('''#include <stddef.h>
#include <stdio.h>
typedef struct { char first; char padding[PAD]; int count; long long score;
                 unsigned bits:3; } Layout;
typedef volatile const Layout Qualified;
extern Qualified sample;
Qualified sample = {0};
int not_a_struct;
int main(void) {
    printf("%zu %zu %zu\\n", offsetof(Layout, first), offsetof(Layout, count),
           offsetof(Layout, score));
    return 0;
}
'''.replace('PAD', str(padding)))
        subprocess.run(['gcc', '-std=c11', '-O0', '-g', f'-gdwarf-{dwarf}',
                        str(source), '-o', str(self.elf)], check=True,
                       capture_output=True, text=True)
        actual = subprocess.check_output([str(self.elf)], text=True).split()
        return dict(zip(('first', 'count', 'score'), map(int, actual)))

    def backends(self):
        result = ['readelf']
        if importlib.util.find_spec('elftools'):
            result.append('pyelftools')
        return result

    def test_real_compiler_offsets_both_dwarf_versions_and_backends(self):
        for version in (2, 5):
            expected = self.compile(dwarf=version)
            for backend in self.backends():
                with self.subTest(version=version, backend=backend):
                    self.assertEqual(elf_abi.member_offsets(self.elf, 'sample', expected,
                                                           backend=backend), expected)

    def test_missing_fields_bitfields_and_wrong_global_rejected(self):
        self.compile()
        for backend in self.backends():
            for variable, fields in (('sample', ['bits']), ('sample', ['missing']),
                                     ('missing', ['count']), ('not_a_struct', ['count'])):
                with self.subTest(backend=backend, variable=variable, fields=fields):
                    with self.assertRaises(RuntimeError):
                        elf_abi.member_offsets(self.elf, variable, fields, backend=backend)

    def test_rebuilt_matching_elf_invalidates_cache_even_with_old_mtime(self):
        before = self.compile(padding=3)
        stamp = self.elf.stat()
        for backend in self.backends():
            self.assertEqual(elf_abi.member_offsets(self.elf, 'sample', before,
                                                   backend=backend), before)
        after = self.compile(padding=11)
        os.utime(self.elf, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        self.assertNotEqual(before['count'], after['count'])
        for backend in self.backends():
            self.assertEqual(elf_abi.member_offsets(self.elf, 'sample', after,
                                                   backend=backend), after)
        self.elf.unlink()
        with self.assertRaises(RuntimeError):
            elf_abi.member_offsets(self.elf, 'sample', ['count'])

    def test_readelf_fallback_without_python_dependency(self):
        expected = self.compile()
        with mock.patch.object(elf_abi, '_pyelftools', side_effect=ImportError):
            self.assertEqual(elf_abi.member_offsets(self.elf, 'sample', expected), expected)

    def test_stripped_elf_refused_instead_of_guessing_current_source_layout(self):
        self.compile()
        subprocess.run(['strip', '--strip-debug', str(self.elf)], check=True)
        for backend in self.backends():
            with self.assertRaises(RuntimeError):
                elf_abi.member_offsets(self.elf, 'sample', ['count'], backend=backend)


if __name__ == '__main__':
    if not shutil.which('gcc') or not shutil.which('readelf') or not shutil.which('strip'):
        raise SystemExit('ELF ABI tests require gcc and GNU readelf/strip, as used by native make check')
    unittest.main(verbosity=2)
