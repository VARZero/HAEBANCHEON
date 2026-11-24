# Filled splash resize — v13

The approved interaction separates the S splash color from the B window
background. Move preserves the real window and moves one complete S triangle.
Resize hides the captured window and fills the entire proposed footprint with S.
There is no outline or content during resize. Release publishes the final B window
and the latest accumulated application content together, once. Cancel restores
the original geometry; pending application damage follows the existing policy.

All preview triangles share one immutable 1 KiB S payload per scene bank. Only
directory entries change during held input. No boundary-neighbor extraction,
cell masks, pixel filling, or projection jobs run for recurring resize previews.
The existing exact fixed-point footprint calculation still runs when the target
geometry changes. Metadata transmission and synchronous publication still cost
time; filling the region does not eliminate those costs.

At resize acquisition, the base scene is composed once without the captured
window. This reveals lower windows, including previously covered content, and
removes the captured window's controls. It is necessary for shrinking: painting
the proposed footprint over the old window would leave stale content outside the
smaller preview. That underlying scene is then frozen and synchronized between
banks. Higher windows retain occlusion. Bank-local changed-entry restoration
restores the base as the target grows and shrinks. Cancel forces a complete
ownership recomposition and restores the captured window's controls and pixels.

`create_window` and `configure_window` accept independent `splash_index` and
`background_index`, each an RGB332 index from 0 through 255. The WM prepares all
256 immutable solid payloads in both banks at startup using DMA fill batches,
before either bank is visible. This consumes 256 KiB per bank and reserves 256
payload slots. Color changes therefore need no writes to active payloads. Invalid
colors are rejected before mutating live state. B changes invalidate projection
plans so background samples use the new color; application-owned pixels remain
application-owned. The local monitor offers separate native color pickers,
quantized to the output palette (including its channel ordering and gray rule).
Its `input_colors` operation is restricted to the control socket and rejects
color changes while a drag is held. Applications can use `configure_window`.

Diagnostics report resize `mode: solid-resize`, `filled_tiles`,
`visible_filled_tiles`, `footprint_updates`, and `resize_prepare_us`.
`outline_tiles` is zero. Move remains `single-tile`. Pointer dot visibility,
latest-coordinate coalescing, BWM1/PTR1, fixed-point arithmetic and
`BO_PUBLISH_COPY` are preserved.

Validation includes byte comparisons across both banks, grow/shrink/cancel,
underlying content restoration, higher-window occlusion, independent colors,
invalid-request atomicity, content updates during previews, single final
publication, failed-transform rollback, hidden-pointer tracking, binary/JSON
socket compatibility, and native fast-client ordering. Physical timings and
capture evidence are recorded separately in `results/solid-resize-board.json`.

## Physical results — 2026-11-25

The v13 WM was SHA256-verified on the physical board before execution. The
kernel/publication driver and application/input binaries are unchanged. Actual
USB HDMI checks passed for full solid filling, hidden content and controls,
grow/shrink, release, cancellation, and non-default independent S/B colors.

| Held-drag final distinct input to actual HDMI capture | Median | Observed range | Trials |
|---|---:|---:|---:|
| v12 resize, outer tile rim | 497.3 ms | 465.9–513.0 ms | 3 |
| v13 resize, full S region | 529.5 ms | 458.3–674.3 ms | 3 |
| v13 move, one full S triangle | 239.6 ms | 215.7–277.8 ms | 3 |

Both resize versions start at azimuth/elevation zero, size 20×14 degrees, with
the same active 240×144 RGB332 demo. Twenty coordinates at 10 Hz end in the same
distinct +120,+70-pixel jump. The final geometry and the same real final-only
capture pixel probe are checked. This includes HTTP, UART and USB capture
sampling, and excludes monitor optical latency. Three trials do not establish
tail latency or a small improvement. **This change does not demonstrate a resize
speed improvement.** Its median is 32.2 ms higher and observed ranges overlap.

A separate native socket benchmark measured resize footprint preparation at
99.4 ms median and publication at 74.3 ms median (8 samples). No held-preview
projection jobs, descriptor plan builds, or pixel payload operations occurred.
Final actual-window completion was 1,520.8 ms in that native resize test and
1,817.6 ms in a separate larger-grow visual check, one sample each. Each final
transform had one publication; neither result meets a one-second completion
target. Normal input-submit socket timings are recorded separately from the
compact PTR1 held-drag path and must not be substituted for its visual latency.

The saved timeline comparison is
`evidence/solid-resize/video/resize-live-comparison.mp4`. Crosshairs in that video
are PC input targets, not board-rendered pointer dots. The live PC monitor is
`http://127.0.0.1:8767/?v=13`. The separately checked SD image is
`D:\temp\haebancheon-solid-resize-preview\HAEBANCHEON-solid-resize-preview.img`.
