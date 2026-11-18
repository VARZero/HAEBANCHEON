"""Check that sparse source edits produce the same scene as a fresh render."""
import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


class Renderer:
    def __init__(self, binary):
        self.process = subprocess.Popen(
            [binary, "--rpc"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, env={**os.environ, "BOAYO_ASSETS": "0", "BOAYO_PLANS": "0"},
        )
        self.window = self.call("create_window", title="Partial projection",
                                surface_width=257, surface_height=129,
                                width_deg=112, height_deg=70)["window_id"]

    def call(self, op, **args):
        self.process.stdin.write(json.dumps({"id": 1, "app": "test", "op": op, "args": args}) + "\n")
        self.process.stdin.flush()
        reply = json.loads(self.process.stdout.readline())
        assert reply["ok"], reply
        return reply["result"]

    def update(self, rgb):
        self.call("update_surface", window_id=self.window, x=0, y=0, width=257, height=129,
                  rgb24=base64.b64encode(rgb).decode("ascii"))

    def render(self, path):
        result = self.call("test_resident_digest", path=str(path))
        return result["projection"], path.with_suffix(".scene").read_bytes()

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=15)
        self.process.stdout.close()
        assert self.process.returncode == 0


def main(binary):
    image = bytearray(bytes((16, 24, 32)) * (257 * 129))
    incremental, fresh = Renderer(binary), Renderer(binary)
    try:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            incremental.update(image)
            before, old_scene = incremental.render(tmp / "before")
            # Two separated changes, one on the last partial tile of the image.
            for x, y in [(25, 23), (256, 128)]:
                offset = (y * 257 + x) * 3
                image[offset:offset+3] = bytes((240, 144, 96))
            incremental.update(image)
            after, updated_scene = incremental.render(tmp / "partial")
            assert updated_scene != old_scene, "edit never reached scene"
            assert 0 < sum(after["jobs_per_core"]) < sum(before["jobs_per_core"]), (before, after)
            prev, now = before["texture_pool"], after["texture_pool"]
            assert now["source_tiles_updated"] - prev["source_tiles_updated"] == 2
            assert now["upload_bytes"] - prev["upload_bytes"] == 8

            incremental.update(image)
            same, same_scene = incremental.render(tmp / "same")
            assert same_scene == updated_scene
            assert same["jobs_per_core"] == [0, 0] and same["update_kind"] == "none"
            assert same["texture_pool"]["upload_bytes"] == now["upload_bytes"]

            fresh.update(image)
            _, full_scene = fresh.render(tmp / "full")
            assert updated_scene == full_scene, "incremental scene differs from fresh projection"
            print(f"PASS partial projection: 2 source tiles, 8 upload bytes, "
                  f"{sum(after['jobs_per_core'])} of {sum(before['jobs_per_core'])} scene jobs")
    finally:
        incremental.close()
        fresh.close()


def test_background_and_move(binary):
    def overlay(renderer, azimuth):
        wid = renderer.call("create_window", title="Overlap", surface_width=257,
                            surface_height=129, width_deg=112, height_deg=70,
                            azimuth=azimuth)["window_id"]
        renderer.call("fill", window_id=wid, rgb=[255, 0, 0])
        return wid

    image = bytearray(bytes((16, 180, 32)) * (257 * 129))
    incremental, fresh = Renderer(binary), Renderer(binary)
    try:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            incremental.update(image)
            upper = overlay(incremental, 0)
            first_path = root / "initial"
            first = incremental.call("test_resident_render", path=str(first_path))
            initial = first_path.with_suffix(".scene").read_bytes()
            assert any(op["op"] == "fill" and op["value"] == 0 and op["words"] > 256
                       for op in first["trace"]), "adjacent background fills were not merged"
            assert any(op["op"] == "fill" and op["value"] == 0xe0 and op["words"] > 256
                       for op in first["trace"]), "adjacent solid fills were not merged"

            for y in range(62, 67):
                pos = (y * 257 + 126) * 3
                image[pos:pos + 15] = bytes((32, 32, 240)) * 5
            incremental.update(image)
            hidden, covered = incremental.render(root / "covered")
            assert covered == initial
            assert hidden["jobs_per_core"] == [0, 0] and hidden["update_kind"] == "none"
            assert (hidden["texture_pool"]["upload_bytes"] ==
                    first["projection"]["texture_pool"]["upload_bytes"])

            incremental.call("configure_window", window_id=upper, azimuth=48)
            _, moved = incremental.render(root / "moved")
            fresh.update(image)
            fresh_upper = overlay(fresh, 48)
            _, full = fresh.render(root / "fresh-moved")
            assert moved != initial and moved == full, "move failed to restore the underlying layer"

            incremental.call("configure_window", window_id=upper, mapped=False)
            fresh.call("configure_window", window_id=fresh_upper, mapped=False)
            _, exposed = incremental.render(root / "exposed")
            _, full_exposed = fresh.render(root / "fresh-exposed")
            assert exposed == full_exposed, "hiding failed to restore the underlying layer"
            print("PASS merged background/solid fills, occluded edit, and restoration after move")
    finally:
        incremental.close()
        fresh.close()


if __name__ == "__main__":
    main(sys.argv[1])
    test_background_and_move(sys.argv[1])
