"""Real socket/memfd bursts: latest overlapping pixels and accumulated damage.

SIGSTOP only makes the initial ready backlog deterministic on a fast host.
Slots remain unchanged until all their commit replies release them.
"""
import json
import mmap
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile


class Client:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(30)
        self.sock.connect(path)
        self.file = self.sock.makefile('rb')
        self.sequence = 0

    def encode(self, op, args=None, padding=''):
        self.sequence += 1
        return (json.dumps(dict(id=self.sequence, op=op, args=args or {}, app=padding)) + '\n').encode()

    def receive(self):
        result = json.loads(self.file.readline())
        return result

    def call(self, op, args=None, ok=True):
        self.sock.sendall(self.encode(op, args))
        result = self.receive()
        assert result['id'] == self.sequence and result['ok'] == ok, result
        return result.get('result', result)

    def wait(self, result):
        return self.call('wait_present', {'generation': result['generation']})

    def close(self):
        self.file.close()
        self.sock.close()


class Server:
    def __init__(self, path):
        self.proc = subprocess.Popen([sys.argv[1], '--mock'], stdout=subprocess.PIPE, text=True,
                                     env={**os.environ, 'BOAYO_SOCKET': path, 'TRIANGLE_INPUT': '1'})
        assert self.proc.stdout.readline().startswith('TRIANGLE_BOAYO_READY')
        self.app = Client(path)
        self.ctl = Client(path + '.control')
        self.ctl.wait(self.ctl.call('input_visibility', {'visible': False}))

    def close(self):
        os.kill(self.proc.pid, signal.SIGCONT)
        self.app.close()
        self.ctl.call('shutdown')
        self.ctl.close()
        assert self.proc.wait(timeout=20) == 0


def packed(index, fmt):
    r, g, b = (index >> 5) * 255 // 7, ((index >> 2) & 7) * 255 // 7, (index & 3) * 255 // 3
    return bytes([index]) if fmt == 'RGB332' else bytes([r, g, b]) if fmt == 'RGB24' else struct.pack('<I', r << 16 | g << 8 | b)


def window(server, fmt, azimuth=0):
    result = server.app.call('create_window', dict(surface_width=64, surface_height=48,
                             width_deg=20, height_deg=14, azimuth=azimuth, background_index=84))
    server.app.wait(result)
    ident = result['window_id']
    info = server.app.call('create_surface', {'window_id': ident, 'format': fmt})
    fd = os.open(info['path'], os.O_RDWR)
    pixels = mmap.mmap(fd, info['slot_bytes'] * 2)
    os.close(fd)
    return ident, info, pixels


def paint(image, rect, color):
    x, y, w, h = rect
    for row in range(y, y + h):
        image[(row * 64 + x) * len(color):(row * 64 + x + w) * len(color)] = color * w


with tempfile.TemporaryDirectory(prefix='triangle-coalesce-') as folder:
    main = Server(folder + '/main')
    reference = Server(folder + '/reference')
    mappings = []
    try:
        summaries = []
        for fmt in ['RGB332', 'RGB24', 'XRGB8888']:
            ident, info, pixels = window(main, fmt)
            refid, refinfo, refpixels = window(reference, fmt)
            mappings += [pixels, refpixels]
            expected = bytearray(packed(84, fmt) * (64 * 48))
            a, b = packed(228, fmt), packed(31, fmt)
            pixels[:info['slot_bytes']] = a * (64 * 48)
            pixels[info['slot_bytes']:] = b * (64 * 48)
            previous = main.ctl.call('get_state')['performance']
            # >8 KiB, crossing recv boundaries; >16 requests, crossing the
            # bounded drain rounds. Last writer wins only inside its damage.
            burst = []
            expected_changes = []
            sequence = 0
            for i in range(40):
                slot = i % 2
                rect = ([4, 4, 24, 20], [16, 12, 24, 20], [52, 36, 8, 8])[i % 3]
                old = expected[:]
                paint(expected, rect, a if slot == 0 else b)
                sequence += 1
                expected_changes.append(i == 0 or expected != old)
                burst.append(main.app.encode('commit_surface', dict(window_id=ident,
                             sequence=sequence, slot=slot, damage=[rect]), padding='x' * 1600))
            os.kill(main.proc.pid, signal.SIGSTOP)
            try:
                main.app.sock.sendall(b''.join(burst))
            finally:
                os.kill(main.proc.pid, signal.SIGCONT)
            replies = [main.app.receive() for _ in burst]
            for i, reply in enumerate(replies):
                assert reply['ok'] and reply['result']['sequence'] == i + 1, reply
                assert reply['result']['changed'] == expected_changes[i], reply
            # All leases released: poison both shared slots before wait_present.
            pixels[:] = b'\xff' * len(pixels)
            last = replies[-1]['result']
            waited = main.app.wait(last)
            assert waited['generation'] >= last['generation']
            refpixels[:refinfo['slot_bytes']] = expected
            reference.app.wait(reference.app.call('commit_surface', dict(window_id=refid,
                               sequence=1, slot=0, damage=[[0, 0, 64, 48]])))
            assert main.ctl.call('snapshot') == reference.ctl.call('snapshot'), fmt
            perf = main.ctl.call('get_state')['performance']
            assert perf['coalesced_updates'] > previous['coalesced_updates'], perf
            publications = perf['publications'] - previous['publications']
            assert 1 < publications < sum(expected_changes), (fmt, publications)
            # An invalid mixed damage list cannot poison latest pixels/sequence.
            main.app.call('commit_surface', dict(window_id=ident, sequence=sequence + 1,
                          slot=0, damage=[[0, 0, 1, 1], [64, 0, 1, 1]]), ok=False)
            pixels[:info['slot_bytes']] = expected
            no_op = main.app.call('commit_surface', dict(window_id=ident, sequence=sequence + 1,
                                  slot=0, damage=[[0, 0, 64, 48]]))
            assert not no_op['changed'] and no_op['generation'] == last['generation'], no_op
            # Overflow the 128-region accumulator with disjoint patches. A
            # conservative bounding fallback must preserve unsubmitted gaps.
            pixels[:info['slot_bytes']] = a * (64 * 48)
            pixels[info['slot_bytes']:] = b * (64 * 48)
            cells = [[x, y, 2, 2] for y in range(0, 48, 4) for x in range(0, 64, 4)]
            sequence += 1  # the no-op above consumed its sequence
            queued = []
            for slot, areas in [(0, cells[:128]), (1, cells[128:])]:
                sequence += 1
                for rect in areas:
                    paint(expected, rect, a if slot == 0 else b)
                queued.append(main.app.encode('commit_surface', dict(window_id=ident,
                              sequence=sequence, slot=slot, damage=areas)))
            # Repeat the latest patch while still pending: no new generation.
            sequence += 1
            queued.append(main.app.encode('commit_surface', dict(window_id=ident,
                          sequence=sequence, slot=1, damage=cells[128:])))
            os.kill(main.proc.pid, signal.SIGSTOP)
            try:
                main.app.sock.sendall(b''.join(queued))
            finally:
                os.kill(main.proc.pid, signal.SIGCONT)
            overflow = [main.app.receive() for _ in queued]
            assert all(r['ok'] for r in overflow), overflow
            assert not overflow[-1]['result']['changed'], overflow
            assert overflow[-1]['result']['generation'] == overflow[-2]['result']['generation']
            main.app.wait(overflow[-1]['result'])
            refpixels[:refinfo['slot_bytes']] = expected
            reference.app.wait(reference.app.call('commit_surface', dict(window_id=refid,
                               sequence=2, slot=0, damage=[[0, 0, 64, 48]])))
            assert main.ctl.call('snapshot') == reference.ctl.call('snapshot'), (fmt, 'damage cap')
            # Geometry changes must use the latest accepted image, even after
            # client slots have been reused and the content was temporarily hidden.
            for server, wid in [(main, ident), (reference, refid)]:
                server.app.wait(server.app.call('configure_window', dict(window_id=wid, mapped=False)))
                server.app.wait(server.app.call('configure_window', dict(window_id=wid, mapped=True, azimuth=3)))
            assert main.ctl.call('snapshot') == reference.ctl.call('snapshot'), fmt
            summaries.append(dict(format=fmt, submissions=40, publications=publications,
                                  coalesced=perf['coalesced_updates'] - previous['coalesced_updates']))
            for server, wid in [(main, ident), (reference, refid)]:
                server.app.wait(server.app.call('destroy_window', {'window_id': wid}))
        # Different windows/formats share a drain batch but never share pixels.
        pair = []
        for fmt, yaw in [('RGB332', -9), ('XRGB8888', 9)]:
            ident, info, pixels = window(main, fmt, yaw)
            refid, refinfo, refpixels = window(reference, fmt, yaw)
            mappings += [pixels, refpixels]
            pixels[:info['slot_bytes']] = packed(228, fmt) * (64 * 48)
            pixels[info['slot_bytes']:] = packed(31, fmt) * (64 * 48)
            pair.append((fmt, ident, refid, refinfo, refpixels))
        queued = []
        for i in range(8):
            fmt, ident, _, _, _ = pair[i % 2]
            queued.append(main.app.encode('commit_surface', dict(window_id=ident,
                          sequence=i // 2 + 1, slot=(i // 2) % 2, damage=[[8, 8, 20, 16]])))
        os.kill(main.proc.pid, signal.SIGSTOP)
        try:
            main.app.sock.sendall(b''.join(queued))
        finally:
            os.kill(main.proc.pid, signal.SIGCONT)
        replies = [main.app.receive() for _ in queued]
        assert all(r['ok'] for r in replies), replies
        main.app.wait(replies[-1]['result'])
        for fmt, ident, refid, refinfo, refpixels in pair:
            expected = bytearray(packed(84, fmt) * (64 * 48))
            paint(expected, [8, 8, 20, 16], packed(31, fmt))
            refpixels[:refinfo['slot_bytes']] = expected
            reference.app.wait(reference.app.call('commit_surface', dict(window_id=refid,
                               sequence=1, slot=0, damage=[[0, 0, 64, 48]])))
        assert main.ctl.call('snapshot') == reference.ctl.call('snapshot'), 'mixed windows'
        print('PASS_COALESCING ' + json.dumps(summaries))
    finally:
        for pixels in mappings:
            pixels.close()
        main.close()
        reference.close()
