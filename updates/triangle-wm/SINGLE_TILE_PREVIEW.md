# Single-tile move/resize preview — 2026-11-23

Historical release record. Current behavior, build steps and board measurements are in [README.md](README.md) and [SOLID_RESIZE.md](SOLID_RESIZE.md); the version-specific results below remain unchanged.

Resize is superseded by [v4 whole-tile boundary preview](TILE_RIM_PREVIEW.md). Move retains its central 64-cell marker. This page records the v3 corner-marker implementation.

Follow-up: [2026-11-24 physical-board timings](SINGLE_TILE_BOARD_RESULTS.md). Native move/resize feedback was measured; the command-to-completion 300 ms target is not met. The offline preparation record below describes the earlier build-stage scope.

Implemented offline through an SD image. No physical board, UART, capture card or
physical SD was contacted. The 300 ms feedback target is **not yet verified on
hardware**. QEMU checks correctness and operation counts, not BOSIO/HDMI latency.

## Behavior

1. Press the existing move control: retain the real window, geometry and content.
   Pick its central BS25 triangle and paint the triangle's central 64 cells with
   the window's splash color S. Resize uses a marker at the dragged corner.
2. Drag: track the latest coordinates and move only this marker. The steady preview
   path skips full-window outlines, footprint construction, descriptor preparation,
   texture conversion and projection submission.
3. Release: acknowledge a queued transform ticket before preparing expensive work.
   Calculate the final geometry and plan once; publish the actual window with
   background B and latest content in one final publication, removing the marker.
4. Cancel: restore the frozen scene. Preparation failure reports a failed transform
   and retains original geometry. Software failure after staging but before
   publication also rolls back slots, plan and geometry.

The marked triangle is a visual cue, not an angle-quantization unit. Final movement
retains the latest ray within the triangle and the existing quarter-degree angle
API; it does not snap to the triangle centroid. The move marker follows the window
center's projected displacement from the press point. The resize marker follows
the dragged corner; final resize retains the anchored calculation and range checks.

Internal pointer coordinates, capture and button state stay active. The optional
dot is temporarily suppressed during preview to avoid a second moving overlay;
the user's `display_enabled` preference is retained and restored afterward.

Content commits during preview snapshot and merge damage into the latest retained
surface. They do not convert or project intermediate content. On release, the union
of pending damage is processed from the latest values. App `wait_present` waits for
real content publication. Control preview waits return `preview_only=true` with a
separate `content_generation`, avoiding a false acknowledgment of app content.

Valid competing geometry, z-order or camera changes, capture-owner disconnect and
the existing capture timeout cancel preview. Invalid configure requests are checked
before they can cancel capture. Higher windows occlude the marker according to
frozen tile ownership; the marker does not overwrite them.

## Fast path and remaining costs

`move_marker.inc` implements dedicated preview publication; `move_commit.inc`
handles queued final preparation, completion and software rollback. The retained
thin-cell outline is selectable with `TRIANGLE_PREVIEW=outline`; default is `tile`.

- A constexpr 32-word mask selects 64 central cells out of 1,024. Motion never
  generates window-edge masks.
- One 1 KiB payload per bank is reserved. An unseen source uses one DMA base
  copy/fill and one masked color blend. Equal immutable background source/color
  combinations reuse the bank's prepared payload and need only metadata changes.
- A static colored tile cannot preserve arbitrary text beneath unpainted cells.
  Different underlying content therefore needs the base copy/blend. Generic motion
  is not claimed to be entirely metadata-only.
- Frozen base/overlay payloads synchronize to the other bank once at drag start.
  Stale copies are sorted, deduplicated and merged when contiguous. Per-bank version
  stamps remain valid throughout the frozen interval.
- CPU metadata buffers alternate with the banks. After warmup, restore the target
  bank's old marker entry, change the new entry and bank tag, then swap buffers.
  This avoids a full CPU header rebuild/copy on every movement.
- Coordinates remaining in the same tile, movement between two fully occluded
  tiles and app-only commits during preview do not require extra publication.

The driver still transfers the full **4,476-word metadata header** and waits
synchronously for publication completion. `BO_RUN`/`BO_PRESENT` were not made
asynchronous; no hardware command processor was added. Initial control composition,
one-time bank synchronization and final rendering also remain. These costs can
still prevent 300 ms feedback on the physical board.

The improved `BO_PUBLISH_COPY` driver is retained. Scalar checks, inactive-bank
rules, camera stability, faults and completion waiting remain. Actual ioctl failure
still faults the service under existing rules. Software rollback is verified
**before publication**; a command accepted by hardware that later times out cannot
be assumed to have left the displayed bank unchanged.

## Validation and comparison

[Raw comparison](results/single-tile-preview.json) uses the **same final image**.
The reference is the already optimized thin-cell outline implementation, not the
historical November 19 measurement from the design brief.

For each of move and resize, eight drag samples produce:

| Work during/after drag | Thin-cell outline | Single tile |
|---|---:|---:|
| Window-footprint calculations during eight moves | 8 | 0 |
| Descriptor-plan rebuilds during motion | 0 | 0 |
| Projection ioctl submissions during motion | 0 | 0 |
| Real plan rebuilds after release | 1 | 1 |
| Publications after release | 2 | 1 |

These are work counts from actual RV32 executables against mock BOSIO, not
predicted speedup factors or board latency measurements.

ASan/UBSan tests check all 1,024 payload bytes and 4,220 directory entries against
an independent frozen reference, alternating banks, restoration, occlusion, shared
color reuse, pointer visibility, capture ownership, button ordering and latest input.
Socket tests cover app completion isolation, held/latest content, one final rebuild,
cancel/resize, scene barriers, final pixels against a separately rebuilt scene, and
injected preflight/post-staging failures. Binary and JSON reference transport pass.
RGB332/RGB24/XRGB8888 coalescing and the retained outline mode also pass. The
validation script runs thirteen suite invocations.

The final image boots its real RV32 kernel, fixed WM, RGB332 demo and input client
in QEMU with floating-point CPU extensions disabled. Both preview modes, async
input endpoints, the RAM startup path and final transforms pass. Ext4 checks,
partition/boot-payload checks and installed-file byte readback pass. Raw emulator
logs are retained in `evidence/single-tile`; their times are not hardware latency.

## SD image and reproduction

Windows artifact:
`D:\temp\haebancheon-single-tile-preview\HAEBANCHEON-single-tile-preview.img`

Sibling manifest and SHA256SUMS identify the image and installed executables.
Kernel `6.12.0-haebancheon-publishcopy` is retained, SHA256:
`91a89cc145d2df3b8781e7803d9159d1ce58cb4b994cc938c86627e2326eee99`.
Fixed Q24 geometry, Q2 angle API, optimized BWM1 binary IPC and latest-input mailbox
remain enabled. [FIXED_BINARY.md](FIXED_BINARY.md) explains numeric scope; diagnostic
and library code is not claimed to be entirely free of floating types.

From the repo root in the prepared WSL environment:

```sh
bash updates/triangle-wm/tools/build_fixed.sh /var/tmp/triangle-marker-release
python3 updates/triangle-wm/tools/package_fixed.py --build /var/tmp/triangle-marker-release
python3 updates/triangle-wm/tools/test_publish_image.py --marker --image /var/tmp/triangle-marker-image/HAEBANCHEON-single-tile-preview.img --output /var/tmp/triangle-marker-image/focused-boot
python3 updates/triangle-wm/tools/test_publish_image.py --marker --preview outline --image /var/tmp/triangle-marker-image/HAEBANCHEON-single-tile-preview.img --output /var/tmp/triangle-marker-image/reference-boot
python3 updates/triangle-wm/tools/analyze_marker.py /var/tmp/triangle-marker-image --output updates/triangle-wm/results/single-tile-preview.json
bash updates/triangle-wm/tools/validate_marker.sh /var/tmp/triangle-marker-host
```

Host validation expects generated fixed sources, geometry tables, reference code
and a sanitizer-built `triangle-boayo` in its supplied output folder. Packaging
requires the previously prepared publish-copy base image/rootfs. Every package run
starts from that clean base, checks installed bytes and checks ext4 before assembly.
All commands operate on regular files.

On a future board boot, startup stages the three executables in
`/run/triangle-wm-runtime`, avoiding SD demand fetches during input. Normal startup
requires `/dev/bosio0`; explicit `mock` startup is for correctness tests. Selection:

```sh
triangle-fixed-start stop
TRIANGLE_PREVIEW=tile triangle-fixed-start start
# Reference mode, after stopping the running service:
TRIANGLE_PREVIEW=outline triangle-fixed-start start
```

For later board measurement, with an application window present:

```sh
/run/triangle-wm-runtime/triangle-input --marker-bench 64 > /tmp/marker-bench.log
```

This reports press, motion and release separately: coordinate acceptance round
trip, server acceptance-to-publication completion, client command-to-completion,
render breakdown and transform ticket. Release `accept_ms` is queued receipt;
`present_ms` includes final real publication. These endpoints exclude UART printing
and measure publication acknowledgment, not optical input-to-photon delay. Compare
both modes on the same board/image using median/P95/max, separating initial freeze,
steady preview and final commit. No physical-board run was performed here.
