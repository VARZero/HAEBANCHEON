# Thin-outline mask reuse and cell updates (v14)

This keeps the v12 one-cell perimeter, S/B background sequence, optional pointer
dot and latest-content contract. It does not use the reverted whole-triangle
preview experiment. No FPGA, kernel, application ABI or input-client change is
required.

## Composition

- Cache each boundary tile's exposed-edge masks, including partial 7/8 seams.
  An unchanged exposure reuses its mask even when other footprint tiles change.
- Keep up to 256 overlay slot mappings, including dormant tiles. Evict only a
  tile not needed by the current composition. Switching display banks does not
  discard the previous contents of an otherwise reusable slot.
- Each bank/slot records the underlying tile address and content version, the
  final visible masks and splash color. Exact mask equality replaces the old
  per-frame 64-bit hash calculation. Comparison uses 32-bit mask words on RV32.
  Identical masks and background need no writes.
- For an entering/leaving/persisting outline with a matching background, restore
  cells removed from the overlay and paint
  only newly covered or changed-color cells. Overlapping color classes are
  normalized to the actual topmost color before caching.
- For black/solid backgrounds, restore selected cells directly to the known
  color. For content, the existing ABI requires staging one clean 1 KiB source
  tile into scratch before copying selected cells back. Scratch commands remain
  ordered across batch boundaries. This is not a zero-copy path for arbitrary
  content.
- A new slot, changed source address or new content version rebuilds from the
  latest base tile. It never restores stale pixels from an older application
  revision. Removed overlay tiles simply point back to clean base metadata.

A changed pointer/button tile with no old or new outline uses the existing full
base-copy path. Applying cell deltas to ordinary pointer motion regressed the
first board trial, so the final selection keeps deltas specific to outlines.
Unchanged pointer/button masks still reuse their cached payload.

Only the inactive scene bank is modified. Hardware masked operations may perform
32-bit read/modify/write while preserving unselected bytes. The full metadata
publication and synchronous content renderer remain unchanged.

`TRIANGLE_UI_DELTA=0` disables the incremental payload path for diagnosis, using
full base copies on changed masks. It retains the new mask and slot caches and is
therefore not an exact v12 performance baseline.

## Validation

`check-ui-delta` compares the actual composed bytes with an independent per-cell
renderer, covering both banks, sparse changes, solid and patterned backgrounds,
content revisions, same visible color across different mask classes, eviction
after more than 256 distinct tiles and scratch reuse across command batches.
It checks zero payload writes on identical warm masks and one removed plus one
added cell per tile on a one-cell move. `check-feedback` retains the thin-edge,
S/B, cancel and split-seam checks. Both pass with ASan/UBSan.

The socket input test produces 160 snapshots identical to v12, including pointer
visibility, capture, background updates, window overlap and output view changes.
The app ABI test and three-format content coalescing tests also pass. The latter
still produces four publications from forty submissions, coalescing thirty-six
intermediate preparations, and covers newer content during preview/cancel.

Runtime diagnostics include rebuilt/incremental/reused overlay tile counts,
selected written/restored cell counts, base transfer bytes and cumulative edge
mask cache hits/builds. Base transfer bytes include whole-tile copy/fill and
scratch staging; selected-cell counts describe logical mask operations, not bus
traffic.

## Board comparison

The reference is the verified v12 service, not the reverted whole-tile v13.
Each version starts a fresh service and the same RGB332 240x144 demo. After twelve
seconds the app is paused for the first 41-command run. It is then resumed for
three seconds before the same 41-command run with active content updates.
The native input client is identical in both runs. Each run includes nine pointer
moves, four move previews/releases, four resize previews/releases and five output
view poses. This is sequential request latency, not a frame-rate measurement.

`tools/analyze_cell_delta.py LOG OUTPUT` extracts both marked runs, retaining raw
samples and median/P95/maximum acceptance, feedback and final-completion times.
All native timings exclude UART transmission, USB capture and optical delay.
Active-render timing is sensitive to contention; four samples per window action
do not establish a general latency guarantee.

The UART command tool now rejects command text above 960 UTF-8 bytes before
opening the port. The current BusyBox build has a 1024-byte line-editing limit;
this reserves space for the completion marker and prevents truncated commands.

### Continuous drag probe

A second probe holds one handle down for sixteen consecutive requests, then
cancels. It uses a fresh service, the same RGB332 app paused after twelve seconds,
and the unchanged v12 input client. Move starts at (689,561), then adds (6,-2)
per request. Resize starts at (916,544), then adds (6,3) per request. The source
window stays fixed throughout each preview. Both probes include all sixteen
requests, including cold masks and footprint crossings.

| Board-local completion (ms), n=16 | v12 median | v14 median | v12 max | v14 max |
| --- | ---: | ---: | ---: | ---: |
| Continuous move preview | 1050.8 | 977.3 | 1275.2 | 1145.4 |
| Continuous resize preview | 910.6 | 947.4 | 1155.7 | 1008.2 |

Move median improves by 7.0%; resize median regresses by 4.0%. The maximum drops
in both probes. With sixteen samples nearest-rank P95 equals the maximum. These
are single paired runs, not statistically established guarantees or sub-second
worst-case operation. Raw samples and requests are in
`results/cell-delta-v14-drag-before.json` and
`results/cell-delta-v14-drag-after.json`.

The broad first prototype and the intermediate outline-only implementation are
retained separately in `cell-delta-v14-prototype.json` and
`cell-delta-v14-drag-intermediate.json`; neither represents the final binary.

### Paired mixed-input benchmark

All values below are median board-local request-to-final-publication milliseconds.
The two runs use identical native commands, with fresh services per version.

| Condition | Action | n | v12 | v14 | Change |
| --- | --- | ---: | ---: | ---: | ---: |
| paused | pointer_move | 9 | 532.6 | 615.0 | +15.5% |
| paused | drag_pointer | 4 | 1015.4 | 936.6 | -7.8% |
| paused | resize_preview | 4 | 1055.8 | 957.1 | -9.3% |
| paused | move_release | 4 | 1750.1 | 1770.2 | +1.1% |
| paused | resize_release | 4 | 1745.1 | 1718.2 | -1.5% |
| active | pointer_move | 9 | 1009.6 | 949.6 | -5.9% |
| active | drag_pointer | 4 | 1457.3 | 1482.8 | +1.8% |
| active | resize_preview | 4 | 1796.7 | 1868.9 | +4.0% |
| active | move_release | 4 | 2957.0 | 2369.9 | -19.9% |
| active | resize_release | 4 | 2914.6 | 2475.6 | -15.1% |

A negative change means less latency. This is a mixed result, not a general
speedup: paused single move/resize previews improve, but paused ordinary pointer
motion and active preview medians regress. Active move/resize final completion
improves in this run. Content scheduling introduces substantial variability and
only four samples were collected per window action; these figures do not prove
an improvement under arbitrary workloads. Full samples, P95, maxima and separate
acceptance/feedback/completion durations are retained in the before/after JSON.

The change eliminates repeated outline-mask construction and unnecessary payload
writes where the scene-bank cache is valid. It does not make dragging smooth:
input handling, CPU mask work, complete metadata publication and synchronous
content rendering still contribute to end-to-end delay. No hardware timing or
optical input-to-photon guarantee is inferred from these measurements.

Final RV32 binary SHA-256 (host artifact):
`f6f59d289fa55cbdc1eae348f4cd7b18578000876a0476bbb4368765bb2fc42f`.
The board delta application validated output size and FNV (`PATCH_OK`); transfer
also validated the patch checksum. This is not an independent board SHA check.

### Live validation and deployment

The final binary runs as `/tmp/triangle-boayo-v14-tuned` and was copied to
`/root/triangle-wm-v14/triangle-boayo-v14-tuned`. Boot startup was not changed.
See `results/cell-delta-v14-manifest.json` for the artifact and verification scope.

Real 1280x720 HDMI capture confirms the thin S-color perimeter. Dragging still
works with the pointer dot disabled. The preview changed 1,971 red pixels; after
cancellation the red-pixel mask matched the pre-drag frame exactly (0 changed).
This checks residual S outline while allowing the application counter to update.
The original enabled dot-display preference was restored. Detailed input states
are in `results/cell-delta-v14-live.json`.

### Interactive recording: additional slow cases

The subsequent real-time HDMI recordings use the HTTP/UART bridge, a live app,
three move steps totaling (-120,-30) pixels and three resize steps totaling
(60,36) pixels. They are a different path and workload from the paired benchmark,
so they are not included in its before/after statistics. Videos are not sped up;
each command shows its PC start, response completion and first detected HDMI
change. Four intentional 450 ms gaps are included in the full sequence duration.

This recording exposed substantially slower requests: the move previews took
7.02-8.34 seconds at the PC, including 6.55-7.54 seconds inside the board client.
Resize previews took 1.92-3.28 seconds at the PC. Move release took 8.14 seconds;
resize release took 4.59 seconds. The single sequences lasted 40.38 and 15.29
seconds from initial press to final response, including the intentional gaps.
The board-internal increase means UART transfer alone cannot explain the gap.
The exact cause has not been isolated; it must not be attributed to capture or
cold caches without further controlled measurements. The later diagnostic found
957 MB available RAM, no swap, no writeback, and only the expected WM/demo user
processes. This optimization does not establish acceptable interactive latency.

`results/cell-delta-v14-recording.json` retains all event timings and measurement
scope. Local videos are `evidence/cell-delta-v14/video/move.mp4` and `resize.mp4`.
Both decode successfully. The native comparison excludes capture/UART latency;
the videos expose those separate PC/HDMI observations rather than substituting
the shorter native benchmark numbers for actual interaction.
