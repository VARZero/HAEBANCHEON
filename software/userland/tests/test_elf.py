import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('elf', Path(__file__).with_name('check_elf.py'))
elf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(elf)


class ElfAudit(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write_elf(self, flags=0, elf_class=1):
        head = bytearray(52)
        head[:6] = b'\x7fELF' + bytes([elf_class, 1])
        struct.pack_into('<H', head, 18, 243)
        struct.pack_into('<I', head, 36, flags)
        (self.root / 'program').write_bytes(head)

    def test_rv32_soft_float(self):
        self.write_elf()
        self.assertEqual(elf.check(self.root)['files'], ['program'])

    def test_reject_rv64(self):
        self.write_elf(elf_class=2)
        with self.assertRaises(ValueError):
            elf.check(self.root)

    def test_reject_compressed_and_float(self):
        for flags in [1, 2, 4, 8, 16]:
            with self.subTest(flags=flags):
                self.write_elf(flags)
                with self.assertRaises(ValueError):
                    elf.check(self.root)
