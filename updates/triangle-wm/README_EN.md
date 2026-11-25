# Triangle-tile WM board experiment

[한국어 — 기본 문서](README.md) · English reference preserved from the deployed v13 documentation.

## Current release — filled splash resize v13

The [current design and physical results](SOLID_RESIZE_EN.md) use independent S splash
and B window-background colors. Move keeps the real window frozen and moves one
complete S triangle. Resize hides the captured content and controls, then fills
the entire target footprint with S. All preview triangles share one immutable
1 KiB color payload per scene bank; recurring previews change metadata, not pixels.
Release publishes B plus the latest accumulated content once. Cancel restores the
original window. The optional pointer dot remains independent of input tracking.

The physical board was updated and the boot files saved on 2026-11-25. The release
retains Q24 geometry with quarter-degree angles, BWM1 binary application messages,
PTR1 latest-coordinate input and the improved `BO_PUBLISH_COPY` driver. Application
socket/shared-surface call names remain available; see [BOAYO.md](BOAYO_EN.md).

| Actual PC input to USB HDMI capture | Median | Observed range | Samples |
|---|---:|---:|---:|
| v13 move, one S tile | 239.6 ms | 215.7–277.8 ms | 3 |
| v12 resize, outer tile rim | 497.3 ms | 465.9–513.0 ms | 3 |
| v13 resize, complete S region | 529.5 ms | 458.3–674.3 ms | 3 |

These use 20 coordinates at 10 Hz and a distinct final target, including HTTP,
UART and USB capture sampling. The resize comparison uses the same 20×14-degree
window, target geometry and capture pixel probe. **No resize speed improvement
was demonstrated.** Final real-window completion was 1.52 s in the native socket
test and 1.82 s in a separate visual grow test (one sample each); a one-second
completion target remains unmet. Emulator times are not hardware measurements.
Raw summaries: [board](results/solid-resize-board.json),
[validation](results/solid-resize-validation.json),
[image manifest](results/solid-resize-image.json),
[RV32 image boot](results/solid-resize-boot.json).

## Build, validate and package the current release

Run from the repository root in WSL/Linux:

```sh
bash updates/triangle-wm/tools/build_fixed.sh /var/tmp/triangle-solid-resize-release
bash updates/triangle-wm/tools/validate_solid_resize.sh /var/tmp/triangle-solid-resize-release
python3 updates/triangle-wm/tools/package_fixed.py \
  --build /var/tmp/triangle-solid-resize-release \
  --output /var/tmp/triangle-solid-resize-image \
  --name HAEBANCHEON-solid-resize-preview.img
python3 updates/triangle-wm/tools/test_publish_image.py --marker \
  --image /var/tmp/triangle-solid-resize-image/HAEBANCHEON-solid-resize-preview.img \
  --output /var/tmp/triangle-solid-resize-image/focused-boot
```

`build_fixed.sh` requires the validated RV32 Buildroot compiler under
`/var/tmp/haebancheon-userland-validation/buildroot/host/bin/`, plus host C++ and
Python. Packaging additionally requires the checked publication-copy base
rootfs/kernel/boot image under `/var/tmp/triangle-publish-copy/`, `debugfs` and
`e2fsck`; it is not a self-contained clean-machine image build. Prepare that
foundation with [PUBLISH_COPY.md](PUBLISH_COPY.md) and the
[reproducible userland build](../../software/userland/README.md). The focused boot
check requires QEMU RV32 and `dtc`. Validation never substitutes for HDMI checks.

The image starts `triangle-fixed-start`, which stages the WM, demo app and input
client in `/run/triangle-wm-runtime/` and launches the RGB332 demo. The loopback PC
capture/input setup and independent color controls are in [INPUT.md](INPUT_EN.md).
Image binaries, capture videos, build trees and machine-local evidence are not
committed; manifests and measurement summaries are tracked.

## Earlier experiments and foundations

| Topic | Record |
|---|---|
| Driver publication stages and copy-only path | [PUBLISH_COPY.md](PUBLISH_COPY.md) |
| Single tile / cell update costs | [PRIMITIVE_RESULTS.md](PRIMITIVE_RESULTS.md) |
| Fixed-point formats and BWM1 transport | [FIXED_BINARY.md](FIXED_BINARY.md) |
| Initial fixed/binary board comparison | [FIXED_BOARD_RESULTS.md](FIXED_BOARD_RESULTS.md) |
| Isolated math/transport comparison and codec fix | [FACTORIAL_RESULTS.md](FACTORIAL_RESULTS.md) |
| Latest-only input acceptance | [INPUT_ASYNC.md](INPUT_ASYNC.md) |
| Original central-marker preview | [SINGLE_TILE_PREVIEW.md](SINGLE_TILE_PREVIEW.md), [board results](SINGLE_TILE_BOARD_RESULTS.md) |
| Previous outer-tile resize policy | [TILE_RIM_PREVIEW.md](TILE_RIM_PREVIEW.md) |
| Whole move tile | [FULL_TILE_MOVE.md](FULL_TILE_MOVE.md) |
| Fast held-move path | [FAST_MOTION.md](FAST_MOTION.md) |
| Cached fixed-point resize preparation | [FAST_RESIZE.md](FAST_RESIZE.md) |

The remaining six-stage description and older measurements below are the original
standalone experiment, not the current v13 application's latency results.

Base: `feat/reproducible-linux-userland`, commit `5f6ff539fae3e529ef53c1d90952dbe72ba609ea`.
The test worktree uses branch `feat/triangle-tile-wm`. The existing checkout and SD boot installation are preserved.

The subsequent `triangle-boayo` service preserves BoAYO's application-side socket and shared-surface calls while replacing the WM internals. See [BOAYO.md](BOAYO_EN.md) for the API, internal move/resize/close controls, and application tests, and [BOAYO_RESULTS.md](BOAYO_RESULTS.md) for its separate board measurements. The six-stage experiment below remains reproducible through the original `triangle-wm` executable.

## Design contract

The later RGB332/RGB32 application buffers and CPU projection planning optimization are documented in [PIXEL_FORMATS.md](PIXEL_FORMATS.md), with measured comparisons in [OPTIMIZATION_RESULTS.md](OPTIMIZATION_RESULTS.md). The subsequent Q44 CPU implementation is documented in [CPU_OPTIMIZATION.md](CPU_OPTIMIZATION.md). The resident-texture and shared-transform changes are in [TEXTURE_CACHE.md](TEXTURE_CACHE.md). Completion-response and diagnostic overhead are addressed in [RESPONSE_PATH.md](RESPONSE_PATH.md). The outline cleanup experiment is documented in [OUTLINE.md](OUTLINE.md). Split controls and the software pointer are documented in [INPUT.md](INPUT_EN.md), with [measured board latency and limits](INPUT_RESULTS.md). The [pointer CPU and visibility update](INPUT_OPTIMIZATION.md) adds runtime dot visibility and exact projected-sample caching. The [button-mask and first-content update](INPUT_CACHE.md) follows it. [Latest-content coalescing](CONTENT_COALESCING.md) merges queued damage before conversion and publication. [Background-first feedback](FEEDBACK.md) adds separate S/B colors and tile-outline move/resize previews. [Thin-outline cell updates](CELL_DELTA.md) reuse masks and update changed overlay cells. [Independent pointer acceptance](INPUT_ASYNC.md) keeps only the latest queued coordinates and prioritizes pointer publication ahead of pending content. The historical v15 application deployment used `triangle-boayo-v15-tuned` and the unchanged `triangle-app-v3`; the six-stage measurements below describe the earlier standalone experiment.

- A window owns whole spherical triangle tiles. There is no rounded corner mask, permanent outline, or inner rectangular frame.
- Background coverage exceeds the content rectangle by an angular padding. Triangle edges are the visible window silhouette.
- Equal solid colors share one immutable 1 KiB payload per scene bank. Moving a solid window changes directory entries, not its pixels.
- The highest visible window owns each tile, including the padded edge. Closing or moving it reveals the lower window.
- Content is a retained list of rectangles and bitmap text. Repainting a damaged source rectangle restores the background and replays intersecting components in order.
- Only affected projected tiles are regenerated. Unchanged tiles in the other bank are reused or DMA-copied. Two projection cores have disjoint destination tiles.
- Planning resets and visits the union of the previous and current visible footprints. It does not clear/scan all 4,220 tile bookkeeping entries on every request. The current ABI still submits a complete 4,476-word metadata snapshot to the driver.
- The standalone experiment and `TRIANGLE_INPUT=0` use the pose ioctl independently of scene composition. The interactive overlay path republishes on pose changes to keep its pointer screen-positioned.
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

The experiment daemon is staged under `/tmp/triangle-wm`, serves `/tmp/triangle-wm.sock`, and does not install itself at boot. A stale socket must be removed only after confirming its previous daemon has stopped. Stop the test daemon and run `bosioctl stop` to leave the output stopped. The application protocol is implemented by the separate `triangle-boayo` service; its current input routing and GUI window controls are described in [INPUT.md](INPUT_EN.md).

The verified final session runs `/tmp/triangle-wm-v3`; its PID is saved in `/tmp/triangle-wm.pid`. Requests are serialized. A long content composition can delay a newly arriving pose request; the six stage pose sweeps run after the scene operations. The 60fps capture stream is not a promise of 60fps interaction. Projection input currently caches one source image in DDR; switching between window sources uploads the selected source again. These are explicit limits of this rendering experiment.

JSON requests are one line per connection. Operations: `create`, `move`, `visible`, `raise`, `close`, `clear`, `content`, `component`, `remove_component`, `pose`, `pose_sweep`, `status`. `snapshot` is available only in the host reference mode. `content` accepts a bounded surface and a component array; `component` replaces/inserts by ID; `remove_component` takes `component_id`. Colors are RGB332 indices. Background colors initially reserved: 84, 164, 109, 38, 228, 255.

Physical stage results and remaining limitations are recorded in `RESULTS.md` as they are measured.

Latest held-move input path and physical latency results: [FAST_MOTION.md](FAST_MOTION.md). The current fast input bridge uses `--fast-input`; recurring demo diagnostics are kept out of the interaction loop.

Whole-tile resize direct input and exact fixed-point preparation: [FAST_RESIZE.md](FAST_RESIZE.md).
