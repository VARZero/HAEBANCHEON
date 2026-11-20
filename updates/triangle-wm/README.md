# Triangle-tile WM board experiment

Base: `feat/reproducible-linux-userland`, commit `5f6ff539fae3e529ef53c1d90952dbe72ba609ea`.
The test worktree uses branch `feat/triangle-tile-wm`. The existing checkout and SD boot installation are preserved.

The subsequent `triangle-boayo` service preserves BoAYO's application-side socket and shared-surface calls while replacing the WM internals. See [BOAYO.md](BOAYO.md) for the API, internal move/resize/close controls, and application tests, and [BOAYO_RESULTS.md](BOAYO_RESULTS.md) for its separate board measurements. The six-stage experiment below remains reproducible through the original `triangle-wm` executable.

## Design contract

The later RGB332/RGB32 application buffers and CPU projection planning optimization are documented in [PIXEL_FORMATS.md](PIXEL_FORMATS.md), with measured comparisons in [OPTIMIZATION_RESULTS.md](OPTIMIZATION_RESULTS.md). The subsequent Q44 CPU implementation is documented in [CPU_OPTIMIZATION.md](CPU_OPTIMIZATION.md). The resident-texture and shared-transform changes are in [TEXTURE_CACHE.md](TEXTURE_CACHE.md). Completion-response and diagnostic overhead are addressed in [RESPONSE_PATH.md](RESPONSE_PATH.md). The outline cleanup experiment is documented in [OUTLINE.md](OUTLINE.md). The current live application deployment uses `triangle-boayo-v7` and the unchanged `triangle-app-v3`; the six-stage measurements below describe the earlier standalone experiment.

- A window owns whole spherical triangle tiles. There is no rounded corner mask, outline, or inner rectangular frame.
- Background coverage exceeds the content rectangle by an angular padding. Triangle edges are the visible window silhouette.
- Equal solid colors share one immutable 1 KiB payload per scene bank. Moving a solid window changes directory entries, not its pixels.
- The highest visible window owns each tile, including the padded edge. Closing or moving it reveals the lower window.
- Content is a retained list of rectangles and bitmap text. Repainting a damaged source rectangle restores the background and replays intersecting components in order.
- Only affected projected tiles are regenerated. Unchanged tiles in the other bank are reused or DMA-copied. Two projection cores have disjoint destination tiles.
- Planning resets and visits the union of the previous and current visible footprints. It does not clear/scan all 4,220 tile bookkeeping entries on every request. The current ABI still submits a complete 4,476-word metadata snapshot to the driver.
- Output yaw/pitch uses the pose ioctl independently of scene composition. Pose-only updates must not increase the scene commit count.
- Every hardware submission is synchronous. An ioctl failure latches a fault; the experiment must inspect device state before restarting.

## Stage gates

| Stage | Implementation and visual gate | Latency reported |
|---|---|---|
| 1 | One solid tile window; borderless silhouette and black surroundings | Show request through acknowledged publication; first creation separately |
| 2 | Move the same window; restore its old footprint | Movement request through publication |
| 3 | Several windows; overlap, move, raise and close; restore lower windows | Multiwindow movement and ordering operations |
| 4 | Place static components; background remains continuous around them | Initial content attachment, including geometry/upload/projection |
| 5 | Change/move/remove components; repaint only affected tiles, no residue | Partial content update; dirty/projected/copied tiles and upload bytes |
| 6 | Increase window count and update area; repeat representative operations | Median/P95/max by workload size; CPU planning, projection, publication breakdown |

Every stage also performs a precomputed smooth yaw/pitch trajectory on the physical output core, checks the real HDMI capture, and saves raw board timings. A trajectory's preparation cost is reported separately; this must not be mistaken for interactive input-to-photon latency.

## Monitoring and measurement

`tools/monitor.py` opens only the explicitly named `USB3.0 Video` capture card. It serves the actual HDMI stream at `http://127.0.0.1:8766`, with stage/status/latency rows from `evidence/status.json`. `/frame` is a snapshot and `/status` is machine readable. The service binds to loopback only.

`tools/board_client.c` runs on the target and times local Unix-socket requests using the target monotonic clock. `tools/generate_suites.py` creates the six deterministic request files and their sample labels on the host. The timed IPC interval includes request send, WM parsing/work/response serialization, and client receive; it excludes connection setup and client request serialization. Each request acknowledges the hardware operation. This is **not** an optical or capture-card latency measurement. UART transfer and printing are outside the interval. Publish profile fields expose driver validation/upload/wait costs. The initial Python target harness was abandoned before obtaining any samples because startup on this SD/CPU was impractically slow.

Capture timestamps use the host monotonic clock. They are not synchronized to the target monotonic clock. AVI frame rate is a playback hint; use `capture-timeline.jsonl` for actual capture timing. Do not infer display latency by subtracting unsynchronized timestamps.

## Build and tests

Run from WSL/Linux:

```sh
bash updates/triangle-wm/build.sh /var/tmp/triangle-wm-host host
python3 updates/triangle-wm/tests/test_solid.py /var/tmp/triangle-wm-host/triangle-wm
python3 updates/triangle-wm/tests/test_content.py /var/tmp/triangle-wm-host/triangle-wm
/var/tmp/triangle-wm-host/triangle-wm --check-camera
bash updates/triangle-wm/build.sh /var/tmp/triangle-wm-rv32 rv32
```

The host mock executes the software projection reference and checks writes against the active bank. Differential tests compare every tile's bytes after incremental updates against a separate full reconstruction, including overlapping components, erasure, covered updates, bank synchronization and movement. Host tests do not replace board verification.

The RV32 compiler is the validated Buildroot cross toolchain; override `RV32_CXX` if needed. Geometry tables are generated from the pinned native geometry, not hand-authored approximations. Integer Q13 bounds are conservative culling only; content projection retains the existing descriptor mathematics.

Output camera preparation uses generated Q26 inverse face matrices and Q26 pose vectors, producing the original Q24 hardware format. The host sweep test compares 1,196 yaw/pitch combinations with the original floating-point implementation; the observed maximum coefficient difference was one Q24 unit. This is a numerical check, with physical camera movement checked separately.

## Board operation

The current test uses COM4 at 115200 baud with DTR/RTS disabled. Only one host process may own the UART. `tools/transfer.py` uses a checked chunk protocol and validates the decompressed SHA256. Commands and recordings are retained in ignored `evidence/`.

After staging the WM daemon, receiver and `triangle-client`, generate and run a stage on the Windows host:

```powershell
python updates/triangle-wm/tools/generate_suites.py updates/triangle-wm/evidence
python updates/triangle-wm/tools/run_stage.py 1 --output updates/triangle-wm/evidence
```

Run stages 1 through 6 in order and review each physical recording. A failed stage stops the host runner. The optional COPY/LITERAL delta tools accelerate later UART deployments; reconstructed binaries require the same SHA256 check before execution. Old running binaries are never patched in place.

The experiment daemon is staged under `/tmp/triangle-wm`, serves `/tmp/triangle-wm.sock`, and does not install itself at boot. A stale socket must be removed only after confirming its previous daemon has stopped. Stop the test daemon and run `bosioctl stop` to leave the output stopped. The application protocol is implemented by the separate `triangle-boayo` service; input routing and GUI window controls remain deferred.

The verified final session runs `/tmp/triangle-wm-v3`; its PID is saved in `/tmp/triangle-wm.pid`. Requests are serialized. A long content composition can delay a newly arriving pose request; the six stage pose sweeps run after the scene operations. The 60fps capture stream is not a promise of 60fps interaction. Projection input currently caches one source image in DDR; switching between window sources uploads the selected source again. These are explicit limits of this rendering experiment.

JSON requests are one line per connection. Operations: `create`, `move`, `visible`, `raise`, `close`, `clear`, `content`, `component`, `remove_component`, `pose`, `pose_sweep`, `status`. `snapshot` is available only in the host reference mode. `content` accepts a bounded surface and a component array; `component` replaces/inserts by ID; `remove_component` takes `component_id`. Colors are RGB332 indices. Background colors initially reserved: 84, 164, 109, 38, 228, 255.

Physical stage results and remaining limitations are recorded in `RESULTS.md` as they are measured.
