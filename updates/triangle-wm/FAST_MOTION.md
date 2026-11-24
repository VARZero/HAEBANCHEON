# Latest-coordinate movement feedback — v9

The existing WM control connection now carries a lightweight one-way motion path.
Movement still displays one complete 1,024-cell S-colored BS25 triangle while the
actual window and content remain frozen. Resize keeps the whole-triangle rim.
Release performs the existing single final transform and displays latest content.
The optional pointer-dot preference is preserved.

## Input path

`pointer_bridge.py --fast-input` immediately queues the newest PC coordinate. Its
receipt is labeled **queued on PC**, never board acceptance or display completion.
A worker sends at most 30 compact coordinate lines/s on the existing UART. Pending
coordinates replace one another; ordinary button/cancel/visibility requests flush
the pending coordinate first. The browser also preserves button order and avoids
periodic board progress queries during a fast drag.

The native `--bridge-fast` client parses `@sequence x y` using integers, coalesces
already-arrived lines, and forwards the latest coordinate on the same WM socket.
Collection is bounded to four already-ready reads so continuing arrivals cannot
postpone forwarding indefinitely. Normal requests remain ordered barriers.

The existing BWM1 envelope contains a 12-byte PTR1 body: little-endian magic,
32-bit sequence, and 16-bit x/y (20 bytes including the envelope). Motion produces
no reply. The service validates coordinates and ownership, ignores stale or
duplicate sequences, and replaces its pending input. `fast_accepted_sequence` and
`fast_visible_sequence` are separate diagnostics for the current fast connection.
Fast mode negotiates support; JSON reference mode retains ordinary RPC input.

Held movement directly updates coordinates and the representative tile without
constructing per-point JSON arguments/results, boundaries or projection plans.
The output driver still uploads the full header and waits for publication.

## Repeated diagnostics caused long pauses

The demo used `get_state` every cycle to check whether its window still existed.
That query constructs and encodes a large window/performance report on the same
WM thread as input. Pausing the app removed the long capture holds; scheduling
priority alone improved the median but retained an 803 ms outlier.

The demo now sends damage and waits for publication without polling diagnostics.
Its next commit detects a closed window through the existing `window not found`
response, and exits normally; other failures remain errors. Startup and explicit
benchmarks retain diagnostics. The public application protocol is unchanged.

Normal scheduling priorities favor interaction: WM nice -10, input bridge -5,
demo app +10. The startup script and fast input launcher retain these settings.
This uses ordinary process scheduling, without real-time scheduling policy.

## Physical results

One centered 20x14-degree window with a 240x144 RGB332 demo updating every two
seconds. Both builds receive 20 points over 1.9 seconds at 10 Hz, with at most one
HTTP request in flight and only its newest waiting point. The distinct final
jump allows actual captured HDMI arrival to be detected without same-tile zeros.
The demo remains running for the final measurements.

| Input to captured HDMI | Before | Final |
| --- | ---: | ---: |
| Continuous, 3-trial median | 836.8 ms | 236.2 ms |
| Continuous range | 485.8-948.1 ms | 224.5-285.2 ms |
| Settled single jump, separate final-only workload | unmeasured | 260.4 ms median |

The continuous median decreased 71.8%. All three final continuous samples were
below 300 ms. Distinct tile changes during the source interval were [4,1,0]
before versus [6,6,6] after. Captures no longer showed the earlier roughly
1.5-second holds in these three final trials. Tile changes are not FPS: topology
quantizes motion. These are small-sample observations, not tail guarantees.

Timing includes HTTP, UART and USB capture sampling, and excludes monitor optical
latency. It measures movement after capture acquisition, not initial button press
or final full-window reprojection. Physical native release remained about 1.57 s
for move and 1.59 s for resize in the final smoke test. Frozen-content comparison,
release completion and pointer preference restoration passed for both operations.
The user excluded 60 inputs/s from acceptance; the intermediate diagnostic is
retained in evidence and is not pooled with final results or rerun on this build.

See `results/fast-motion-board.json` for individual samples and workload boundaries.
The comparison video preserves real capture timestamps and uses the median trial
from each build. Its white crosshair is the host input target, not board output.

## Validation and artifact

ASan/UBSan covers raw socket fragmentation/batching, latest replacement, stale
sequences, capture ownership, invalid packet isolation, ordered release and exact
preview bytes across both banks. Legacy binary/JSON input regressions pass. Host
queue tests check truthful receipts and button order. The actual native UART client
test drains 200 queued coordinates before release. The actual demo test verifies
startup-only diagnostics and normal exit on window close. The final RV32 image
boots and exercises the WM/app, raw bridge, native markers and final transforms
against mock BOSIO; emulator timings are not physical performance results.

Final image: `D:\temp\haebancheon-fast-motion-preview\HAEBANCHEON-fast-motion-preview.img`.
The improved driver and kernel are retained. Build manifests describe offline
image scope; the separate hardware report records hot deployment. The board and
live capture/input pages remain running after validation.
