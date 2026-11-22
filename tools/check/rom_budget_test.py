#!/usr/bin/env python3
"""Resource-gate regressions without a DS compiler or emulator."""
import contextlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rom_budget as budget


def symbols(heap_bytes=256 * 1024):
    return {'__end__': 0x02380000 - heap_bytes, '__eheap_end': 0x02380000,
            '__itcm_start': 0x01000100, '__itcm_end': 0x01005000,
            '__dtcm_start': 0x02ff0000, '__sbss_end': 0x02ff2378,
            '__dtcm_top': 0x02ff4000}


def pack(code, size):
    return {'language': code, 'payload_bytes': size, 'loader_padding_bytes': 0,
            'allocation_bytes': size, 'files': {'dict.bin': size}}


def nm_output(table=None):
    table = symbols() if table is None else table
    return ''.join(f'{value:08x} A {name}\n' for name, value in table.items())


class NmTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.elf = Path(self.folder.name) / 'word Pass;$(literal).elf'
        self.elf.write_bytes(b'\x7fELF test fixture')

    @staticmethod
    def result(output=None, code=0):
        return subprocess.CompletedProcess([], code,
                                           stdout=nm_output() if output is None else output,
                                           stderr='unsupported ELF' if code else '')

    def test_parser_handles_arm_mapping_and_duplicate_local_symbols(self):
        text = (nm_output() + '01000100 t $a\n01000200 t $a\n'
                '02000010 t helper\n02000020 t helper\n'
                '         U absent\n00000000 U absent_too\n')
        found = budget.parse_nm(text)
        self.assertEqual(found['__end__'], symbols()['__end__'])
        self.assertNotIn('$a', found)
        self.assertNotIn('absent', found)
        self.assertNotIn('absent_too', found)

    def test_conflicting_linker_symbol_and_empty_output_are_rejected(self):
        with self.assertRaisesRegex(budget.BudgetInputError, 'conflicting'):
            budget.parse_nm(nm_output() + '02000000 A __end__\n')
        with self.assertRaisesRegex(budget.BudgetInputError, 'no defined'):
            budget.parse_nm('not an ELF symbol table\n')

    def test_literal_arguments_and_cross_nm_preference(self):
        with patch.object(budget.shutil, 'which', side_effect=lambda name: '/tools/' + name), \
             patch.object(budget.subprocess, 'run', return_value=self.result()) as run:
            self.assertEqual(budget.read_symbols(self.elf), symbols())
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0],
                             ['/tools/arm-none-eabi-nm', '--defined-only', str(self.elf)])
            self.assertNotIn('shell', run.call_args.kwargs)
            self.assertEqual(run.call_args.kwargs['timeout'], 30)

    def test_host_nm_fallback_after_cross_tool_error(self):
        with patch.object(budget.shutil, 'which', side_effect=lambda name: '/tools/' + name), \
             patch.object(budget.subprocess, 'run', side_effect=[self.result(code=1), self.result()]) as run:
            self.assertEqual(budget.read_symbols(self.elf), symbols())
            self.assertEqual(run.call_args.args[0][0], '/tools/nm')

    def test_broken_command_and_timeout_report_errors(self):
        for failure in (self.result(code=1), OSError('no executable'), subprocess.TimeoutExpired('nm', 30)):
            with self.subTest(failure=failure):
                kwargs = {'side_effect': failure} if isinstance(failure, Exception) else {'return_value': failure}
                with patch.object(budget.subprocess, 'run', **kwargs):
                    with self.assertRaisesRegex(budget.BudgetInputError, 'cannot read ELF symbols'):
                        budget.read_symbols(self.elf, nm='/exact/tool')

    def test_missing_or_non_elf_file_never_runs_nm(self):
        with patch.object(budget.subprocess, 'run') as run:
            self.elf.write_bytes(b'not an ELF')
            with self.assertRaisesRegex(budget.BudgetInputError, 'not an ELF'):
                budget.read_symbols(self.elf)
            self.elf.unlink()
            with self.assertRaisesRegex(budget.BudgetInputError, 'cannot read ELF'):
                budget.read_symbols(self.elf)
            run.assert_not_called()


class BudgetTests(unittest.TestCase):
    def report(self, table=None, rom_bytes=17 * 1024 * 1024, mode='retail',
               retail=None, preview=None, forbid_float=False):
        return budget.evaluate(symbols() if table is None else table, rom_bytes, mode,
                               [pack('nl', 128 * 1024)] if retail is None else retail,
                               preview, forbid_float)

    def test_exact_rom_heap_and_tcm_boundaries_pass(self):
        table = symbols(128 * 1024 + budget.MIN_RESERVE)
        table['__itcm_end'] = budget.ITCM_LIMIT
        table['__sbss_end'] = budget.DTCM_LIMIT
        report = self.report(table, rom_bytes=budget.ROM_LIMIT)
        self.assertEqual(report['status'], 'pass')
        self.assertEqual(report['remaining_before_other_allocations_bytes'], budget.MIN_RESERVE)
        self.assertEqual(report['dtcm_remaining_before_stack_bytes'], 0)

    def test_each_missing_linker_symbol_refuses_to_guess(self):
        for name in symbols():
            with self.subTest(name=name):
                table = symbols()
                del table[name]
                with self.assertRaisesRegex(budget.BudgetInputError, 'missing linker budget symbols'):
                    self.report(table)

    def test_sbss_alternate_linker_spelling(self):
        table = symbols()
        table['__sbss_end__'] = table.pop('__sbss_end')
        self.assertEqual(self.report(table)['status'], 'pass')

    def test_one_byte_rom_or_heap_over_budget_fails(self):
        self.assertEqual(self.report(rom_bytes=budget.ROM_LIMIT + 1)['status'], 'fail')
        report = self.report(symbols(128 * 1024 + budget.MIN_RESERVE - 1))
        self.assertEqual(report['status'], 'fail')
        self.assertIn('nl', ' '.join(report['violations']))
        self.assertIn(str(budget.MIN_RESERVE - 1), ' '.join(report['violations']))

    def test_invalid_tcm_and_heap_ranges_fail(self):
        for name, value in (('__itcm_end', budget.ITCM_LIMIT + 1),
                            ('__sbss_end', budget.DTCM_LIMIT + 1),
                            ('__dtcm_top', budget.DTCM_LIMIT + 1),
                            ('__end__', 0x02380001),
                            ('__eheap_end', 0x02400001)):
            with self.subTest(name=name):
                table = symbols()
                table[name] = value
                self.assertEqual(self.report(table)['status'], 'fail')

    def test_retail_cannot_link_preview_code_or_state(self):
        for name in ('scriptPreviewBoard', 'v4DictOpen', 'scriptBoardHint'):
            with self.subTest(name=name):
                table = symbols()
                table[name] = 0x02000100
                report = self.report(table)
                self.assertEqual(report['status'], 'fail')
                self.assertIn(name, ' '.join(report['violations']))

    def test_preview_uses_maximum_exclusive_pack_not_sum(self):
        report = self.report(symbols(200 * 1024 + budget.MIN_RESERVE), mode='preview',
                             retail=[pack('nl', 200 * 1024), pack('fi', 190 * 1024)],
                             preview=[pack('ru', 100 * 1024), pack('uk', 90 * 1024)])
        self.assertEqual(report['status'], 'pass')
        self.assertEqual(report['largest_active_pack_allocation_bytes'], 200 * 1024)
        self.assertEqual(report['remaining_before_other_allocations_bytes'], budget.MIN_RESERVE)
        self.assertEqual(report['largest_preview_pack']['language'], 'ru')

    def test_external_preview_pack_can_be_the_largest(self):
        report = self.report(symbols(200 * 1024 + budget.MIN_RESERVE), mode='preview',
                             preview=[pack('ru', 200 * 1024 + 1), pack('uk', 50 * 1024)])
        self.assertEqual(report['status'], 'fail')
        self.assertEqual(report['worst_active_pack_mode'], 'preview')
        self.assertIn('preview pack ru', ' '.join(report['violations']))

    def test_preview_needs_its_pack_inputs(self):
        with self.assertRaisesRegex(budget.BudgetInputError, 'complete resource pack list'):
            self.report(mode='preview')

    def test_float_formatter_guard_is_explicit_and_actionable(self):
        table = symbols()
        table['_dtoa_r'] = 0x02000100
        self.assertEqual(self.report(table)['status'], 'pass')
        report = self.report(table, forbid_float=True)
        self.assertEqual(report['status'], 'fail')
        self.assertIn('integer formatting', ' '.join(report['violations']))


class PackTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        counts = [1] * 18
        self.dictionary = b'WPD3' + struct.pack('<18I', *counts) + bytes(sum(budget.WPD3_WIDTHS))
        headers = b''.join(b'THEME\0\0\0' + struct.pack('<6H', 1, 0, 0, 0, 0, 0) for _ in range(40))
        self.pools = b'WPP1' + struct.pack('<I', 40) + headers + b'WORD' * 40
        letters = bytearray(68)
        letters[:4] = b'WPL1'
        letters[16:20] = bytes([25] * 4)
        self.letters = bytes(letters)
        for code in budget.RETAIL_CODES:
            target = self.root / code
            target.mkdir()
            for name, data in (('dict.bin', self.dictionary), ('pools.bin', self.pools), ('letters.bin', self.letters)):
                (target / name).write_bytes(data)

    def test_all_retail_file_sizes_and_raw_padding_are_counted(self):
        result = budget.scan_packs(self.root, 'retail')
        self.assertEqual(len(result), 10)
        payload = len(self.dictionary) + len(self.pools) + len(self.letters)
        self.assertEqual(result[0]['payload_bytes'], payload)
        self.assertEqual(result[0]['allocation_bytes'], payload + 12)

    def test_missing_directory_or_resource_is_an_error(self):
        with self.assertRaisesRegex(budget.BudgetInputError, 'missing retail pack directory'):
            budget.scan_packs(self.root / 'absent', 'retail')
        target = self.root / 'nl' / 'dict.bin'
        target.unlink()
        with self.assertRaisesRegex(budget.BudgetInputError, 'missing/unreadable'):
            budget.scan_packs(self.root, 'retail')
        for child in (self.root / 'nl').iterdir():
            child.unlink()
        (self.root / 'nl').rmdir()
        with self.assertRaisesRegex(budget.BudgetInputError, 'missing retail packs: nl'):
            budget.scan_packs(self.root, 'retail')

    def test_corrupt_magic_record_counts_and_letters_fail(self):
        corruptions = [('dict.bin', b'BAD!' + self.dictionary[4:]),
                       ('dict.bin', self.dictionary[:-1]),
                       ('pools.bin', self.pools[:-1]),
                       ('letters.bin', self.letters[:-1]),
                       ('letters.bin', self.letters[:16] + bytes(52))]
        originals = {'dict.bin': self.dictionary, 'pools.bin': self.pools, 'letters.bin': self.letters}
        for name, data in corruptions:
            with self.subTest(name=name, size=len(data)):
                path = self.root / 'nl' / name
                path.write_bytes(data)
                with self.assertRaises(budget.BudgetInputError):
                    budget.scan_packs(self.root, 'retail')
                path.write_bytes(originals[name])

    def test_preview_uses_canonical_checksum_and_matching_pack_validation(self):
        sys.path.insert(0, str(budget.ROOT / 'tools' / 'lang'))
        import dict_v4
        import tiles_v2
        import pools_v2
        root = self.root / 'preview'
        for code in budget.PREVIEW_CODES:
            target = root / code
            target.mkdir(parents=True)
            words, common = dict_v4.FIXTURES[code]
            dictionary = dict_v4.build(code, words, common)
            resources = {'dict.bin': dictionary, 'tiles.bin': tiles_v2.build(code, words),
                         'pools.bin': pools_v2.build(code, dictionary, pools_v2.FIXTURES[code])}
            for name, data in resources.items():
                (target / name).write_bytes(data)
        packs = budget.scan_packs(root, 'preview')
        self.assertEqual([item['language'] for item in packs], ['ru', 'uk'])
        self.assertEqual(packs[0]['loader_padding_bytes'], 0)
        path = root / 'ru' / 'dict.bin'
        corrupt = bytearray(path.read_bytes())
        corrupt[-1] ^= 1
        path.write_bytes(corrupt)
        with self.assertRaisesRegex(budget.BudgetInputError, 'invalid preview pack'):
            budget.scan_packs(root, 'preview')

    def test_cli_writes_failure_report_and_nonzero_exit(self):
        rom = self.root / 'game.nds'
        rom.write_bytes(bytes(512))
        output = self.root / 'report.json'
        table = symbols(128 * 1024 + budget.MIN_RESERVE - 1)
        with patch.object(budget, 'read_symbols', return_value=table), \
             patch.object(budget, 'scan_packs', return_value=[pack('nl', 128 * 1024)]), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            code = budget.main(['--rom', str(rom), '--elf', 'unused.elf', '--mode', 'retail', '--output', str(output)])
        self.assertEqual(code, 1)
        self.assertEqual(json.loads(output.read_text())['status'], 'fail')

    def test_preview_cli_refuses_missing_pack_root_and_writes_error(self):
        rom = self.root / 'game.nds'
        rom.write_bytes(bytes(512))
        output = self.root / 'error.json'
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            code = budget.main(['--rom', str(rom), '--elf', 'unused.elf', '--mode', 'preview', '--output', str(output)])
        self.assertEqual(code, 2)
        self.assertIn('--preview-root', json.loads(output.read_text())['errors'][0])


if __name__ == '__main__':
    unittest.main()
