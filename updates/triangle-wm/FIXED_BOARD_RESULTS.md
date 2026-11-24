# Actual board comparison: fixed WM, binary IPC and copy publication

Measured 2026-11-22 on the connected board, kernel
`6.12.0-haebancheon-publishcopy`, image `fixed-wm-binary-v1`.
**Publication improved. Payload writes did not consistently improve. Pointer
latency regressed against the prior v15 records.** The experiment is not an
overall successful pointer-speed optimization.

The initial cold SD boot and first program faults were excluded. The fixed WM,
RGB332 app and input client were copied to RAM before controlled input trials,
matching the prior RAM deployment method. The board was not reflashed or rebooted
during the current comparisons. The default copy-mode WM/demo and live input are
restored at the end.

## Publication: same boot, direct driver calls

No WM/app, fixed pose, one visible tile, alternating inactive banks. Two warmups
then twenty samples per case. Values are median/P95/max milliseconds, measured
inside the board from ioctl request through synchronous output acknowledgement.
UART, capture and optical latency are excluded.

| Path | Metadata changes per bank visit | Median | P95 | Max |
| --- | --- | ---: | ---: | ---: |
| checked_publish | False | 121.851 | 131.847 | 138.473 |
| checked_publish | True | 121.880 | 131.980 | 132.024 |
| full_upload_then_checked_present | False | 93.728 | 99.514 | 124.006 |
| full_upload_then_checked_present | True | 93.789 | 94.198 | 99.179 |
| trusted_copy | False | 61.619 | 62.046 | 69.191 |
| trusted_copy | True | 61.738 | 62.138 | 69.200 |

The unchanged-metadata checked path is 121.851 ms versus 61.619 ms for trusted
copy (49.4% less). The previous boot's checked result was 121.884 ms, consistent
with this current control. Trusted copy uploads all 4,476 words; the warm checked
path uploads zero words but still validates and compares its snapshots. The new
scalar/camera checks remain; it is not a zero-validation or zero-wait API.

| Warm unchanged case, independent stage medians (ms) | Checked | Copy |
| --- | ---: | ---: |
| user snapshot acquisition | 8.683 | 8.767 |
| bounds/camera/directory checks | 33.439 | 4.884 |
| comparison/snapshot save OR full upload | 48.877 | 20.455 |
| submit/output acknowledgement | 25.087 | 25.101 |
| completion bookkeeping | 4.111 | 0.270 |

Stage medians are independent and do not necessarily sum to total median. Full
upload still has a material CPU/driver/DMA cost. App-active publications can be
slower than this standalone test; 61.619 ms is not a general worst-case bound.

## Tile and cell updates: same boot, checked versus copy

Two warmups and ten timed samples per case. Prebuilt masks, no WM/input/geometry
projection. Each tile is 1,024 RGB332 one-byte cells (1 KiB). A side-N native
subtriangle selects N squared cells; it is not an N-by-N HDMI pixel rectangle.
Each case's final result was read back via scratch and verified byte-for-byte.
All 56 cases / 560 timed requests passed. Preparatory background reset and result
verification are outside timing. Data-copy cell tests include a fresh full 1 KiB
scratch upload; selected cell count is not the number of bytes crossing DDR.

| Whole tile payload operation | Checked-run write median | Copy-run write median |
| --- | ---: | ---: |
| tile_dma_fill | 2.812 | 4.142 |
| tile_dma_copy | 2.906 | 2.848 |
| tile_upload | 1.593 | 1.521 |

These use the same payload-write APIs. The copy branch is selected only at
publication. There is no established payload-write speedup; some write medians
increase. DMA scheduling, cache state and frame-phase effects vary across the
sequential cases, and no outliers were removed.

| Contiguous cells | Checked write | Copy-run write | Checked write+publish | Copy write+publish |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 4.632 | 6.931 | 116.847 | 59.406 |
| 4 | 5.903 | 8.161 | 118.613 | 89.276 |
| 16 | 5.901 | 8.154 | 124.443 | 88.890 |
| 64 | 5.822 | 8.128 | 119.439 | 85.721 |
| 256 | 6.450 | 8.731 | 118.563 | 88.841 |
| 1024 | 6.088 | 8.347 | 116.901 | 73.140 |

Total improvement depends on the case and output-boundary phase; do not substitute
the standalone publish median for every cell-update total. All contiguous and
scattered raw samples, P95/maxima, write/blend/publication components are in
`fixed-board-primitives-checked.json` and `fixed-board-primitives-copy.json`.

## Native pointer backlog

Fresh service per mode, same 240x144 RGB332 demo, pause after twelve seconds, then
resume three seconds before active trials. Three rounds of sixteen sequential
coordinate submissions, same positions and verified final endpoint (700,350).
Clock intervals exclude UART/capture. Latest-only intermediate skipping is allowed.
The old column is a **historical v15 run from a different boot**, not a new
same-boot run of its old executable. It changes both numeric and socket code, so
it cannot isolate fixed math from CBOR/object-model cost.

| Condition / metric (median ms) | Old v15 | New fixed + checked | New fixed + copy |
| --- | ---: | ---: | ---: |
| paused: coordinate acceptance, n=48 | 175.935 | 266.271 | 264.435 |
| paused: 16-coordinate burst to final confirmation, n=3 | 4285.636 | 6093.585 | 5623.150 |
| paused: final coordinate request to confirmation, n=3 | 707.101 | 1107.234 | 878.831 |
| active: coordinate acceptance, n=48 | 249.653 | 307.879 | 303.068 |
| active: 16-coordinate burst to final confirmation, n=3 | 5920.719 | 7526.015 | 7694.275 |
| active: final coordinate request to confirmation, n=3 | 1091.386 | 1302.361 | 1130.278 |

Copy mode improves paused performance relative to the same new checked WM, but
active burst medians do not show a consistent improvement (7.526 vs 7.694 seconds).
Coalescing counts differ with receipt/render timing. Versus historical v15, new
copy-mode burst medians regress 31.2% paused and 30.0% active. These are three
rounds, not statistically established workload-independent guarantees.

## Actual HDMI pointer delay

Three fresh live-app trials per mode. Twenty source events at 10 Hz, coordinates
(200,680) through (580,680). Both versions keep one HTTP request in flight and
replace pending moves with the latest coordinate. Starting coordinates are warmed
before each trial. Endpoint detection uses actual white cursor pixels in the
otherwise black bottom margin; all trials start with an empty endpoint region.
PC intervals include HTTP/UART, queueing, USB capture/polling buffering. They are
not optical input-to-photon measurements and are not interchangeable with the
native timings above. Completion polling may return after the cursor is visible.

| Last source event to first final-position HDMI observation | Median ms | Max ms | Raw three samples ms |
| --- | ---: | ---: | --- |
| Old v15, historical | 1696.159 | 2027.553 | 1696.159, 2027.553, 1597.143 |
| New fixed + checked | 3682.127 | 4440.502 | 4440.502, 3135.769, 3682.127 |
| New fixed + copy | 2618.013 | 3668.379 | 3668.379, 2602.952, 2618.013 |

The new copy path improves the current checked path's median by 28.9%, but is
54.4% slower than historical v15 (1.696 -> 2.618 seconds). The current CPU/binary
bundle therefore needs further work; removing floating geometry and JSON text did
not by itself yield a faster mouse. Raw events/native replies and source timestamps
are retained in the paced JSON files; capture frames/timelines are under evidence.

## Diagnostic scope and remaining cost

A separate paused single-move diagnostic with the new checked WM reported tile
pick 16.217 ms, mask phase 405.785 ms, overlay payload phase 174.328 ms, and total
UI composition 581.970 ms. Tile picking is inside the mask interval. The mask phase
also includes controls and input-checkpoint processing; the payload phase includes
CPU bookkeeping and ordered ioctl calls. These are **one diagnostic record**, not
pointer medians or isolated pure geometry timings. The checkpoint implementation
can decode a blocked non-movement control message repeatedly while it remains
queued, so the mask interval cannot be attributed solely to arithmetic. No causal
claim that Q24 math alone caused the regression is supported by these comparisons.

A later content diagnostic confirms the copy-mode WM and healthy scene publication.
The final live check restores the new copy-mode app, enabled dot and no active
capture/preview, and verifies the actual dot near (700,680). Initial cold-boot
records are retained separately and excluded. The board remains on with interactive
input available at http://127.0.0.1:8767/ and live capture at port 8766.

Files: `fixed-board-comparison.json`, `fixed-board-publish.json`,
`fixed-board-input-checked.json`, `fixed-board-input-copy.json`,
`fixed-board-paced-checked.json`, `fixed-board-paced-copy.json`,
`fixed-board-pointer-profile-checked.json`, `fixed-board-content-profile-copy.json`,
and `fixed-board-final-live.json` in the results directory.
