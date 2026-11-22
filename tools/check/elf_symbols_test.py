#!/usr/bin/env python3
"""Symbol lookup regressions without Docker, DeSmuME or a devkitARM install."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'emu'))
import elf_symbols

OUTPUT = ('02000000 B DBG\n02000100 D S\n02000200 b fatReady\n'
          '02000300 d lastSaveFailed\n02000400 T code\n02000500 R constant\n'
          '         U missing\n')


class SymbolTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.elf = Path(self.folder.name) / 'word Pass;$(literal).elf'
        self.elf.write_bytes(b'ELF fixture')
        elf_symbols._table.cache_clear()
        self.addCleanup(elf_symbols._table.cache_clear)
        which = patch.object(elf_symbols.shutil, 'which',
                             side_effect=lambda name: '/usr/bin/nm' if name == 'nm' else None)
        self.which = which.start()
        self.addCleanup(which.stop)

    @staticmethod
    def result(output=OUTPUT, code=0):
        return subprocess.CompletedProcess([], code, stdout=output,
                                           stderr='unsupported ELF' if code else '')

    def test_many_variables_share_one_table(self):
        with patch.object(elf_symbols.subprocess, 'run', return_value=self.result()) as run:
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x02000000)
            self.assertEqual(elf_symbols.symbols(self.elf, ['S', 'fatReady', 'lastSaveFailed']),
                             {'S': 0x02000100, 'fatReady': 0x02000200, 'lastSaveFailed': 0x02000300})
            self.assertEqual(elf_symbols.symbol(self.elf, 'S'), 0x02000100)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0], ['/usr/bin/nm', '--defined-only', str(self.elf)])
            self.assertNotIn('shell', run.call_args.kwargs)

    def test_missing_and_non_data_symbols_are_errors(self):
        with patch.object(elf_symbols.subprocess, 'run', return_value=self.result()) as run:
            for name in ('absent', 'code', 'constant', 'missing'):
                with self.assertRaisesRegex(RuntimeError, 'symbols not found'):
                    elf_symbols.symbol(self.elf, name)
            self.assertEqual(run.call_count, 1)
            self.elf.unlink()
            with self.assertRaisesRegex(RuntimeError, 'cannot read symbol ELF'):
                elf_symbols.symbol(self.elf, 'DBG')
            self.assertEqual(run.call_count, 1)

    def test_in_place_rebuild_invalidates_cache(self):
        with patch.object(elf_symbols.subprocess, 'run', side_effect=[self.result(), self.result('03000000 B DBG\n')]) as run:
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x02000000)
            st = self.elf.stat()
            self.elf.write_bytes(b'new fixture')
            os.utime(self.elf, ns=(st.st_atime_ns, st.st_mtime_ns + 1000000000))
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x03000000)
            self.assertEqual(run.call_count, 2)

    def test_replacement_with_preserved_size_and_mtime_invalidates_cache(self):
        with patch.object(elf_symbols.subprocess, 'run', side_effect=[self.result(), self.result('04000000 B DBG\n')]) as run:
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x02000000)
            st = self.elf.stat()
            replacement = self.elf.with_suffix('.new')
            replacement.write_bytes(self.elf.read_bytes())
            os.utime(replacement, ns=(st.st_atime_ns, st.st_mtime_ns))
            os.replace(replacement, self.elf)
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x04000000)
            self.assertEqual(run.call_count, 2)

    def test_container_fallback_uses_literal_arguments(self):
        with patch.object(elf_symbols.subprocess, 'run', side_effect=[self.result(code=1), self.result()]) as run:
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x02000000)
            cmd = run.call_args.args[0]
            self.assertEqual(cmd[0:3], ['docker', 'run', '--rm'])
            self.assertEqual(cmd[-1], '/work/' + self.elf.name)
            self.assertIn(str(self.elf.parent) + ':/work:ro', cmd)
            self.assertNotIn('bash', cmd)
            self.assertNotIn('shell', run.call_args.kwargs)

    def test_cross_nm_is_preferred(self):
        self.which.side_effect = lambda name: '/cross/' + name
        with patch.object(elf_symbols.subprocess, 'run', return_value=self.result()) as run:
            self.assertEqual(elf_symbols.symbol(self.elf, 'DBG'), 0x02000000)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0][0], '/cross/arm-none-eabi-nm')

    def test_command_failures_are_not_cached(self):
        with patch.object(elf_symbols.subprocess, 'run', return_value=self.result(code=1)) as run:
            for _ in range(2):
                with self.assertRaisesRegex(RuntimeError, 'cannot read symbols'):
                    elf_symbols.symbol(self.elf, 'DBG')
            self.assertEqual(run.call_count, 4)  # each attempt tries nm and Docker


if __name__ == '__main__':
    unittest.main()
