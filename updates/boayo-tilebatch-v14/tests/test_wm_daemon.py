import base64
import json
import mmap
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

BINARY = str(Path(sys.argv[1]).resolve())


class Client:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(30)
        self.socket.connect(path)
        self.file = self.socket.makefile("rwb", buffering=0)
        self.sequence = 0

    def call(self, op, args=None, ok=True):
        self.sequence += 1
        request = dict(id=self.sequence, app="same-client-name", op=op, args=args or {})
        self.file.write((json.dumps(request) + "\n").encode())
        response = json.loads(self.file.readline())
        assert response["ok"] == ok, response
        return response.get("result")

    def close(self):
        self.file.close()
        self.socket.close()


def wait_for(predicate, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = predicate()
        if result:
            return result
        time.sleep(.01)
    raise AssertionError("timed out waiting for WM state")


def shared_protocol_test():
    with tempfile.TemporaryDirectory() as temporary:
        log = Path(temporary) / "wm.log"
        path = str(Path(temporary) / "wm.sock")
        executable = Path(temporary) / "boayo"
        executable.symlink_to(BINARY)
        env = dict(os.environ, BOAYO_SOCKET=path)
        with log.open("w") as output:
            daemon = subprocess.Popen([str(executable), "serve", "--mock"],
                                      env=env, stdout=output, stderr=output)
            client = other = None
            try:
                wait_for(lambda: Path(path).exists() or daemon.poll() is not None)
                assert daemon.poll() is None, log.read_text()
                client, other = Client(path), Client(path)
                wait_for(lambda: client.call("get_state")["service"]["phase"] == "ready")
                assert client.call("get_state")["app_processes"] == []
                window = client.call("create_window", dict(title="Shared test", surface_width=64,
                    surface_height=64, width_deg=32, height_deg=22, input_enabled=False))["window_id"]
                description = client.call("create_surface", dict(window_id=window))
                fd = os.open(description["path"], os.O_RDWR)
                length = description["slot_bytes"]
                try:
                    os.ftruncate(fd, length * 2 - 1)
                    raise AssertionError("shared memory size was mutable")
                except PermissionError:
                    pass
                memory = mmap.mmap(fd, length * 2)
                memory[:length] = bytes([32, 64, 128]) * (64 * 64)
                result = client.call("commit_surface", dict(window_id=window, slot=0, sequence=1,
                                                            damage=[[0, 0, 64, 64]]))
                client.call("wait_present", dict(generation=result["generation"]))
                before = wait_for(lambda: (s if (s := client.call("get_state"))["performance"]["rendered_generation"]
                                           >= result["generation"] else None))
                assert before["performance"]["frame_limit"] == 60
                other.call("create_surface", dict(window_id=window), ok=False)
                other.call("commit_surface", dict(window_id=window, slot=0, sequence=2,
                                                  damage=[[0, 0, 1, 1]]), ok=False)
                client.call("commit_surface", dict(window_id=window, slot=0, sequence=2,
                                                   damage=[[0, 0, 1, 1], [63, 0, 2, 1]]), ok=False)
                client.call("commit_surface", dict(window_id=window, slot=0, sequence=2,
                                                   damage=[[2**32, 0, 1, 1]]), ok=False)
                client.call("commit_surface", dict(window_id=window, slot=2**32, sequence=2,
                                                   damage=[[0, 0, 1, 1]]), ok=False)
                client.call("wait_present", dict(generation=2**63), ok=False)
                for y in range(9, 17):
                    offset = length + (y * 64 + 12) * 3
                    memory[offset:offset + 24] = bytes([224, 32, 64]) * 8
                changed = client.call("commit_surface", dict(window_id=window, slot=1, sequence=2,
                                                             damage=[[12, 9, 8, 8]]))
                client.call("wait_present", dict(generation=changed["generation"]))
                after = wait_for(lambda: (s if (s := client.call("get_state"))["performance"]["rendered_generation"]
                                          >= changed["generation"] else None))
                p0, p1 = before["projection"]["texture_pool"], after["projection"]["texture_pool"]
                assert p1["source_tiles_updated"] - p0["source_tiles_updated"] == 1
                assert p1["upload_bytes"] - p0["upload_bytes"] == 256
                assert 0 < sum(after["projection"]["jobs_per_core"]) < sum(before["projection"]["jobs_per_core"])
                unchanged = client.call("commit_surface", dict(window_id=window, slot=1, sequence=3,
                                                               damage=[[12, 9, 8, 8]]))
                assert unchanged["generation"] == changed["generation"] and not unchanged["changed"]
                client.call("commit_surface", dict(window_id=window, slot=1, sequence=2,
                                                   damage=[[12, 9, 8, 8]]), ok=False)
                client.call("commit_surface", dict(window_id=window, slot=2, sequence=4,
                                                   damage=[[12, 9, 8, 8]]), ok=False)
                client.close()
                client = None
                wait_for(lambda: not other.call("get_state")["windows"])
                memory.close()
                os.close(fd)
                # Built-in apps and launcher render in independently supervised processes.
                launcher = other.call("ui_start")
                first = other.call("launch_app", dict(kind="dashboard", azimuth=-30))
                second = other.call("launch_app", dict(kind="pulse", azimuth=30))
                state = wait_for(lambda: (s if len((s := other.call("get_state"))["app_processes"]) == 3 else None))
                processes = state["app_processes"]
                assert len({p["pid"] for p in processes}) == 3
                assert all(p["pid"] != daemon.pid for p in processes)
                assert {first, second, launcher["launcher_window"]} <= {w["window_id"] for w in state["windows"]}
                victim = next(p for p in processes if p["kind"] == "dashboard")
                os.kill(victim["pid"], signal.SIGKILL)
                state = wait_for(lambda: (s if len((s := other.call("get_state"))["app_processes"]) == 2
                                         and not any(w["window_id"] == first for w in s["windows"]) else None))
                assert any(w["window_id"] == second for w in state["windows"])
                assert state["render_error"] is None
                other.call("request_window_action", dict(window_id=second, action="close"))
                wait_for(lambda: len(other.call("get_state")["app_processes"]) == 1)
                other.call("shutdown")
                other.close()
                other = None
                assert daemon.wait(timeout=20) == 0, log.read_text()
                assert "runtime error:" not in log.read_text(), log.read_text()
                assert "ERROR: AddressSanitizer" not in log.read_text(), log.read_text()
                print("PASS shared buffers, bounded damage, leases, owner isolation, partial projection, separate app processes and crash cleanup")
            finally:
                if client:
                    client.close()
                if other:
                    other.close()
                if daemon.poll() is None:
                    daemon.terminate()
                    daemon.wait(timeout=20)
                if daemon.returncode:
                    print(log.read_text(), file=sys.stderr)


def snapshot_test():
    with subprocess.Popen([BINARY, "--rpc"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          text=True) as child:
        def call(op, **args):
            child.stdin.write(json.dumps(dict(id=1, app="snapshot", op=op, args=args)) + "\n")
            child.stdin.flush()
            result = json.loads(child.stdout.readline())
            assert result["ok"], result
            return result["result"]
        window = call("create_window", title="Immutable snapshot", surface_width=32, surface_height=32)["window_id"]
        info = call("create_surface", window_id=window)
        fd = os.open(info["path"], os.O_RDWR)
        pixels = mmap.mmap(fd, info["slot_bytes"] * 2)
        pixels[:info["slot_bytes"]] = bytes([80, 96, 128]) * (32 * 32)
        call("commit_surface", window_id=window, slot=0, sequence=1, damage=[[0, 0, 32, 32]])
        pixels[:info["slot_bytes"]] = bytes([224, 0, 0]) * (32 * 32)
        assert base64.b64decode(call("test_surface", window_id=window)) == bytes([80, 96, 128]) * (32 * 32)
        pixels.close()
        os.close(fd)
        child.stdin.close()
        assert child.wait(timeout=5) == 0
    print("PASS application buffer reuse preserves the acknowledged WM snapshot")


snapshot_test()
shared_protocol_test()
