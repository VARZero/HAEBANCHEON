# Historical background-first / outline experiment (v12)

Current policy: [filled S resize and independent S/B colors](SOLID_RESIZE.md). Move keeps the real window frozen and moves one complete S tile. Resize hides the captured window and fills the complete proposed footprint with S. Release publishes B plus latest content once; 0–255 colors can be configured at creation or later. Initial creation retains S-first feedback. The reserved six-color list, two-publication release and timing values below describe the earlier outline experiment. `TRIANGLE_PREVIEW=outline` retains that comparison path.

The later [v14 thin-outline optimization](CELL_DELTA.md) preserves this visual
behavior while reusing masks and applying cell deltas. The measurements below
are the original v12 results.

BoAYO application calls are preserved. The WM separates the splash/preview color
S (`splash_index`, default 228) from the actual background B (`background_index`,
default 84). Colors are selected when creating a window. Both use the existing reserved palette: 84, 164, 109, 38, 228, 255.
Application buffers and projector padding are initialized with B, never S.

## Publication sequence

1. Creation publishes the whole triangle-tile silhouette filled with S. This
   publication happens even if the first content commit arrived in the same
   request batch. Content planning, conversion and projection follow afterward.
2. Normal content updates combine pending damage and read the latest accepted
   pixels. There is no timer that redraws unchanged content. See
   [CONTENT_COALESCING.md](CONTENT_COALESCING.md) for the shared-slot contract.
3. During move/resize, the original window retains its position and content.
   Only a temporary S-colored perimeter follows the proposed footprint. The
   perimeter is computed from the same spherical tile ownership used at commit,
   including partial edges at 7/8 subdivision seams. Interior cells are untouched.
4. Release commits geometry once and publishes B at the destination first. A
   subsequent publication projects the latest content. Cancel removes the
   preview and preserves geometry; content accepted during dragging is retained.
5. Normal windows have no permanent border. Pointer-dot visibility remains
   independent of tracking, capture and preview drawing.

The preview uses masked fills over clean base payloads in the inactive bank.
It does not run the content projector or rebuild application projection plans.
Unchanged tile footprints reuse their outline masks. The UI overlay cache tracks
the preview color, masks and underlying source revisions independently per bank.

## Completion and diagnostics

`wait_present(generation)` retains its existing meaning: all currently accepted
content through that generation has reached publication, or has been superseded
by a newer state. The initial S/B-only publication does not falsely complete it.
`wait_feedback(generation)` waits only for an initial feedback publication; it
does **not** establish that the content is visible. Use it for input feedback,
not for releasing shared source slots or claiming final content completion.

`get_state.performance` adds `feedback_generation`, `shell_publications` and
`last_shell_render`. Window descriptions include `splash_index` and
`background_index`. `input_status.preview` exposes whether a preview is active,
its window, color and outline tile count.

The native input client and live page report:

- `accept_ms`: board-local request to input acceptance.
- `feedback_ms`: request to the first acknowledged visual feedback. For release
  this is B before content; for ordinary motion it is the completed overlay.
- `present_ms`: request through final content publication.

These are software/device acknowledgements. The capture probe separately measures
PC HTTP input to the first observed USB HDMI frame. It includes UART transport,
capture buffering and polling, and is not an optical display measurement.

## Validation

`check-feedback` directly checks intermediate publications without timing sleeps:
S first even with ready content, B before moved content, no content projection
during preview, actual tile boundary masks, cancel restoration, resize separation,
stationary clicks without a content repaint, an initial view change without a false completion, and complete coverage of all 780 split edges. Host and ASan/UBSan builds run it.

Real socket/memfd tests cover the existing application ABI and pointer lifecycle,
plus disjoint content updates during an active preview and cancellation. In all
three source formats the resulting tile hashes match a complete rebuild of the
latest image. The 40-submission coalescing test still produces four publications
and 36 superseded intermediate preparations per format.

## Limits

Rendering remains synchronous and cannot preempt an ioctl already in progress.
Ready requests are drained with a bounded budget. This change provides earlier
feedback; it does not guarantee a particular frame rate or sub-second input.
Two-stage geometry commits add a publication and can increase total completion
time. Native and captured timings must be assessed separately.

The live input page is <http://127.0.0.1:8767/>; HDMI-only monitoring is
<http://127.0.0.1:8766/>. These loopback services run on the connected PC.

## Board measurements

Same boot, RAM executables, a freshly recreated RGB332 240x144 demo, updates
paused after detection of `demo_update`. Values are board-local request-to-response
medians; UART, capture and optical delay are excluded. Pointer has nine samples;
move/resize have four each. The v12 resize sequence adds preview motion before
release, so the sequences are not identical. Raw samples and P95/max values are
in [v11 baseline](results/feedback-v12-before.json) and
[v12 results](results/feedback-v12-after.json).

| Operation | v11 completed ms | v12 first feedback ms | v12 final completion ms |
|---|---:|---:|---:|
| Pointer motion | 520.412 | 567.816 | 570.912 |
| Drag: v11 pointer only / v12 outline | 485.507 | 966.961 | 971.575 |
| Move release | 1033.512 | 946.661 | 1738.013 |
| Resize release | 1122.645 | 959.344 | 1717.348 |

The first B feedback is modestly earlier than v11's complete moved/resized
content. Total completion is slower, and adding an actual perimeter makes dragging
slower than v11's pointer-only drag. This is not an overall speedup or a sub-second
guarantee. Acceptance medians are 134 ms for pointer motion, 341 ms for move
preview, and 379 ms for resize preview; acceptance is not screen feedback.

Actual USB HDMI capture confirms S first, B/content afterward, an unfilled
S-colored preview over the stationary original, B before moved/resized content,
and removal of the temporary outline on cancellation. The four capture samples
observe first changes at 1,148 / 1,172 / 1,118 / 1,158 ms for move preview/release
and resize preview/release. These include PC HTTP/UART and capture buffering;
one sample per operation is visual verification, not a latency distribution.
See [capture observations](results/feedback-v12-capture/capture.json).

The first cold application run reports 4,180 ms for create/map plus source setup
and 3,382 ms for initial content; these include cold application/renderer work.
The S screen is visible during that preparation. The capture timestamps establish
ordering and do not measure the request-to-first-S latency.

Small 8x8 updates complete in **459.547 ms median** (n=8, P95 637.183 ms).
The fragmented stress case—16 submissions, each with 128 disjoint 4x4 regions—
completes the entire group in **19,499.204 ms median** (n=3). Every group uses two
publications and coalesces 14 intermediate preparations. This is group latency,
not per-frame latency. [Content samples](results/feedback-v12-content.json).

With the live demo updating every two seconds, the shown pointer takes
**1,121.834 ms median** to complete (n=9, P95 1,630.363 ms). Acceptance is
361.812 ms median; PC HTTP/UART round trip is 1,369.463 ms median. This is slower
than the paused comparison and demonstrates the remaining contention with a
non-preemptible content render. [Active-input samples](results/feedback-v12-active.json).

Board binaries are persisted under `/root/triangle-wm-v12/`; hashes and runtime
verification are in [the deployment manifest](results/feedback-v12-manifest.json).
[Browser acceptance](results/feedback-v12-browser.json) covers hidden-dot dragging,
active preview state, release, Escape, and UART handback. The live services are
left running with the demo updating and the pointer bridge connected.

A final active-run background-frame profile takes 679 ms to render: UI overlay
work accounts for 496 ms, publication for 119 ms, and content projection is zero
tiles. This single sample identifies substantial remaining WM control/overlay
cost even after removing content projection from the feedback frame. It is not
a median. [Final runtime state](results/feedback-v12-final-state.json).
