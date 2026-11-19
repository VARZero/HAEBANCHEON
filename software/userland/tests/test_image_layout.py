"""Exercise boot layout and corruption detection without a toolchain/hardware."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('image', Path(__file__).parents[1] / 'make_image.py')
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)


class ImageLayout(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        boot = bytearray(2048 * 512)
        struct.pack_into('<B3sB3sII', boot, 446, 0, bytes(3), 0xda, bytes(3), 2048, 4)
        boot[510:512] = b'\x55\xaa'
        header = bytearray(512)
        header[:8] = b'ORYSD001'
        for index in range(3):
            struct.pack_into('<II', header, 8 + index * 8, 1, zlib.crc32(bytes([index]) * 512))
        boot += header + b''.join(bytes([i]) * 512 for i in range(3))
        self.boot = self.root / 'boot.img'
        self.boot.write_bytes(boot)
        self.ext = self.root / 'root.ext4'
        ext = bytearray(32768)
        struct.pack_into('<I', ext, 1024 + 4, 32)
        ext[1080:1082] = b'\x53\xef'
        self.ext.write_bytes(ext)
        self.target = self.root / 'sd.img'
        image.assemble(self.boot, self.ext, self.target)

    def mutate(self, offset, data):
        with self.target.open('r+b') as f:
            f.seek(offset)
            f.write(data)

    def test_partition_and_exact_payload(self):
        result = image.inspect_image(self.target)
        self.assertEqual(result['root_lba'], 4096)
        with self.target.open('rb') as f:
            f.seek(2048 * 512)
            self.assertEqual(f.read(2048), self.boot.read_bytes()[2048 * 512:])
            f.seek(result['root_lba'] * 512)
            self.assertEqual(f.read(), self.ext.read_bytes())

    def test_crc_corruption(self):
        self.mutate(2049 * 512, b'!')
        with self.assertRaisesRegex(ValueError, 'CRC'):
            image.inspect_image(self.target)

    def test_overlap(self):
        self.mutate(462 + 8, struct.pack('<I', 2048))
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            image.inspect_image(self.target)

    def test_kernel_exceeds_dtb_boundary(self):
        self.mutate(2048 * 512 + 16, struct.pack('<I', 251905))
        with self.assertRaisesRegex(ValueError, 'load range'):
            image.inspect_image(self.target)

    def test_truncated_rootfs(self):
        with self.target.open('r+b') as f:
            f.truncate(self.target.stat().st_size - 512)
        with self.assertRaisesRegex(ValueError, 'beyond image'):
            image.inspect_image(self.target)


if __name__ == '__main__':
    unittest.main()
