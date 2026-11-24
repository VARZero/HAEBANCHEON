# Single-tile preview: physical-board timing, 2026-11-24

Historical release record. Current behavior, build steps and board measurements are in [README.md](README.md) and [SOLID_RESIZE.md](SOLID_RESIZE.md); the version-specific results below remain unchanged.

Booted the supplied `fixed-wm-single-tile-v3` SD image. Kernel release and embedded
build manifest match the prepared image. No WM, kernel or application binary was
replaced for this comparison. Both modes use fixed Q24 geometry, Q2 angles, BWM1
binary IPC and the existing BO_PUBLISH_COPY driver.

Image SHA256: `61df684d8a5ab54e13b5458d1c4ca782211eba3dcb1d48101b6dd327b56f9d21`.

## Workload and timing boundaries

One 20x14 degree window with a 240x144 RGB332 demo. The app runs its normal two-second
counter updates. Each mode exercises move and resize with 16 alternating pointer
positions, +/-90 horizontal and +/-24 vertical pixels from its control. Timings
come from the native board client, not the host serial command duration. UART
transfer, printing, USB capture and monitor/optical delay are excluded.

- **Acceptance:** client input-submit start to receipt of its accepted ticket.
- **Accepted to publication:** server acceptance to its output publication acknowledgment.
- **Command to completion:** client input-submit start through wait-input completion.
  This includes the separate completion request/reply.
- **Press/release:** separate native commands; release includes the actual window
  transformation and final publication. One sample each, not distributions.

The 300 ms feedback target is assessed against command-to-completion, not the
shorter server acceptance-to-publication interval. It is **not met** in this trial.

## Results

All figures are milliseconds. Motion rows have 16 samples; P95 uses nearest rank
and therefore equals the maximum at this sample size.

| Action / command-to-completion | Outline median | Single tile median | Single tile P95/max |
|---|---:|---:|---:|
| Move preview | 1147.5 | 349.7 | 452.6 |
| Resize preview | 946.2 | 352.0 | 407.8 |

Median reductions: **69.5%** for move and **62.8%** for resize.

| Single-tile motion interval | Move median | Resize median |
|---|---:|---:|
| Coordinate acceptance | 159.2 | 169.0 |
| Accepted to publication acknowledgment | 172.2 | 176.5 |
| Marker render, including publish | 107.1 | 112.6 |
| Publication within marker render | 91.9 | 100.9 |

These intervals overlap; do not add their medians as a pipeline breakdown.

| Separate command, one sample | Outline move | Single-tile move | Outline resize | Single-tile resize |
|---|---:|---:|---:|---:|
| Press through first preview completion | 1429.0 | 1671.8 | 1695.7 | 1955.9 |
| Release through real transform completion | 1701.9 | 1455.1 | 3451.0 | 1586.0 |

The new first-press preparation is still slow and was slower than the retained
outline in these samples. It includes frozen-scene setup and bank synchronization.
Steady feedback improves substantially; final transformation remains over a second.

## Work avoided and remaining bottlenecks

During each set of 16 single-tile movements: zero window-footprint calls, descriptor
plan builds, BO_RUN submissions, UI mask work and texture uploads. The last marker
report records 17 frames including the initial press, 3 payload builds and 14
payload reuses. The last move changes at most two directory entries and performs
no payload copy/blend. One merged frozen-bank copy is performed at setup.

Legacy outline makes 16 footprint calls per action and permits live app rendering.
Its observed 8 move / 7 resize BO_RUN calls belong to app content updates, not
reprojection of the dragged window. New preview holds/coalesces those updates.
At release the live-app trial observes 3 publications for outline versus 1 for
single tile. The earlier offline test without a coincident app update observed
2 versus 1; these counts should not be confused.

Full 4,476-word metadata upload and synchronous output acknowledgment remain.
Publication dominates the approximately 107–113 ms fast marker render. Client
acceptance and completion exchange also contribute substantially to the roughly
350 ms end-to-end result. No additional software optimization was deployed during
this measurement.

## Visual verification — recovered and passed

After the user reconnected HDMI, reopening the capture stream recovered the actual
window. The earlier black-frame observations below describe the diagnostic history,
not the final state. No WM, app or driver binary change was needed for recovery.

[Physical HDMI verification](results/single-tile-hdmi.json) passes for both move and
resize. During dragging the actual purple window bounds remain identical while the
S-colored central triangle changes position. Releasing completes the transform,
removes the marker and shows the new real window with latest content. Static title
white-pixel counts before press / frozen press / moved marker were 4789/4716/4789
for move (the central marker can cover a few text pixels), and 4763/4763/4763 for
resize. Captured bounds during frozen preview are [239,88,1042,631] for move and
[363,88,1181,535] for resize; they change only after release. The optional dot's
visibility preference is restored, with no remaining capture or pressed button.

The timestamp-preserving recording is
`D:\temp\haebancheon-single-tile-preview\single-tile-preview.mp4` (27.6 seconds).
It labels each phase, elapsed time and native command timings. Host command intervals
include HTTP/UART; native intervals exclude them. The two-second held-preview pauses
are intentional demonstrations, not processing delays. This visual check does not
turn the earlier native acknowledgment timings into optical latency measurements.

### Black-frame diagnostic history

Native output acknowledgments are not proof of visible HDMI pixels. The capture
card is detected and supplies fresh frames, but they were entirely black. Reopening
the capture device and independently using FFmpeg YUY2 instead of OpenCV MJPEG
produced the same result. The user checked the physical HDMI connection and then
unplugged/reconnected the capture card USB. Fresh frames remained black afterward.
Direct monitor testing is currently unavailable.

Readback of the active output bank found 70 visible directory entries, nonzero
RGB332 data in all 1,024 cells of the first sampled tile and its nonblack palette
color `0x4800b6`. Output status was `0x4a5d0003`, active bank 1, poison 0; DMA errors
and timeouts were both zero. The WM/app were paused for this snapshot; only scratch
memory was written for readback, never the displayed bank.

With the WM stopped, the packaged independent `bosioctl demo` successfully projected
1,024 cells on each core and acknowledged its red/green display. Capture still
reported zero nonblack pixels across the observation period. This reproduces the
problem outside the new WM. HDMI reconnection followed by capture reopening then
recovered the stream; the exact underlying link/capture failure is not identified.

The board was restored to single-tile mode, app running, pointer preference enabled,
no active capture/drag. The live capture and matching async input bridge are kept
available on ports 8766 and 8767 respectively. The successful demonstration leaves
the window moved and resized. No board power cycle was performed.

## Evidence and reproduction

- [Parsed board data](results/single-tile-board.json)
- Full native samples, command receipts and raw UART transcripts:
  `evidence/single-tile-board/` (not committed).
- `tools/benchmark_marker_board.py` runs both retained modes from the same image
  and parses `triangle-input --marker-bench 16` output. `--existing-first` uses the
  initially booted tile runtime before restarting into outline reference mode.
- Capture preview: `http://127.0.0.1:8766/`.

The first observed boot's create/map command took 5839.3 ms, followed by initial
content submission/publication taking 4630.9 ms. This is separate from drag
feedback and excludes the preceding Linux boot, RAM staging and application setup.
It is not a total boot-to-screen measurement.
