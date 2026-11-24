# Latest pointer input and independent completion (v15)

The v14 thin cell outlines, S/B colors, app ABI and optional pointer dot remain.
This changes input scheduling, not the FPGA or output-core cursor hardware.

## Contract

`input_submit {x,y}` is a control-only movement operation. Its response contains
`accepted_ticket`, `visible_ticket` and `coalesced_moves`; it is an acceptance
receipt, **not** display completion. The server keeps one unprocessed movement.
Later accepted coordinates from the same connection replace it. Validation and
capture ownership checks happen before replacement, so invalid/rival input cannot
destroy the pending movement. Tickets increase monotonically for the service
lifetime.

`wait_input {ticket}` waits until the latest pointer state representing that
ticket or a newer one has been published. A replaced ticket does not mean its
intermediate coordinates were drawn. `input_progress` returns accepted/applied/
visible tickets, last visible coordinates, coalesced count and the last accepted
movement's receipt-to-publication duration. Hidden/no-op movements can complete
without publishing when there is no outstanding overlay work. Timings start
inside the server for this diagnostic; they exclude time before acceptance.

Legacy `input_move`, buttons, cancel and visibility operations flush earlier
pending movement before executing. Buttons retain their synchronous completion
contract in the bridge. Movement cannot be replayed after release or cancellation.
Disconnect resolves pending movement and cancels capture through the normal
cleanup path. Application `wait_present`/`wait_feedback` generations retain their
previous meanings and are independent of input tickets.

## Scheduling

- Bounded control-message draining still precedes app requests.
- Safe renderer checkpoints accept movement receipts while content processing is
  in progress. They do not dispatch geometry/button/app operations or destroy
  objects referenced by the renderer. The final checkpoint, before UI composition,
  applies the newest accepted movement to the frame being prepared. During CPU
  pointer-mask generation and composition, additional receipt-only checkpoints
  accept newer coordinates for the next frame; they never mutate the in-flight
  pointer state. Each checkpoint handles at most four messages and parses once.
- When content is pending and the current layout is valid, one pointer-only
  publication can reuse the existing content before converting/projecting the
  pending update. It completes input tickets, never application generations.
- The next publication must process content. This bounds pointer priority so a
  continuous stream cannot indefinitely starve application updates. Shell/layout
  changes use the normal ordered S/B path instead of stale layout metadata.
- A synchronous hardware ioctl or a CPU section between checkpoints still cannot
  be preempted. This is not a dedicated hardware cursor or a real-time guarantee.

The native `triangle-input --bridge-async` maps browser movement to `input_submit`
and returns after acceptance. The Python bridge opts in with `--async-input
--client /tmp/triangle-input-v15`. Its normal mode remains compatible with v14.
The browser already replaces consecutive queued movement messages; it now sends
the next one after acceptance instead of waiting for publication. Idle progress
queries show completion separately and do not masquerade as coordinate receipts.

## Verification and comparison

`tests/test_input_mailbox.py` uses real sockets and shared app surfaces to test
latest-only bursts, separate acknowledgement/completion, exact final pixel hashes,
invalid input, capture ownership, press/release barriers, content progress,
hidden-dot tracking and disconnect cleanup. ASan/UBSan passes. The existing 160
input snapshots match v12/v14, and app ABI and three-format content coalescing
tests pass unchanged.

The same v15 native client can exercise v14 synchronously (`--stream-bench`) and
v15 with independent acceptance (`--stream-bench-async`). Each of three rounds
submits sixteen identical positions from (520,350) to (700,350), verifies the
endpoint and waits for its completion. The synchronous run displays every
position; the asynchronous run may coalesce intermediate positions. Report both
acceptance and final display time: a faster receipt alone is not a faster screen.
This burst test measures draining a backlog, not a fixed-rate mouse or FPS.

The browser queue is exercised independently with
`node tests/test_pointer_queue.cjs`: only one receipt request is in flight,
queued movements collapse without crossing buttons, and progress-query latency
never replaces the coordinate acceptance measurement.

The first v15 prototype is retained in `results/input-mailbox-v15-prototype.json`.
It reduced burst drain time but accepted no replacements in the board test:
CPU overlay composition still blocked new receipts. The final version adds
receipt checkpoints inside overlay work; prototype numbers are not final results.

## Board results: native burst

Same RGB332 240x144 demo, fresh service, pause after twelve seconds, then resume
for three seconds before the active phase. Each condition has three 16-point
bursts. The client binary is identical; only its completion policy and the WM
version differ. These are board-local durations, excluding UART and capture.

| Condition / metric (median ms) | v14 synchronous | v15 latest-input |
| --- | ---: | ---: |
| paused: coordinate acceptance, n=48 | 133.7 | 175.9 |
| paused: 16-point burst to final display, n=3 | 9084.9 | 4285.6 |
| paused: last point request to completion response, n=3 | 559.3 | 707.1 |
| active: coordinate acceptance, n=48 | 236.3 | 249.7 |
| active: 16-point burst to final display, n=3 | 15319.4 | 5920.7 |
| active: last point request to completion response, n=3 | 550.0 | 1091.4 |

The server coalesced 31 of 48 paused movements and 29 of 48 active movements.
Two input-priority publications occurred during the active phase. Burst drain
median fell by 52.8% / 61.4%. **Individual acknowledgement did not get faster**:
new submissions now overlap rendering instead of waiting until the previous
publication finishes. The final point still waits for in-flight work; active
last-point completion reached 1519.6 ms in these three rounds. This is a backlog
throughput improvement, not proof that every isolated pointer move is quicker.
The existing browser already coalesced pending movements, so a separate paced
HTTP/HDMI test is required to assess actual interaction.

Full samples and P95/maxima are in `results/input-mailbox-v15-before.json` and
`results/input-mailbox-v15-after.json`. `tools/analyze_input_mailbox.py` validates
complete marker blocks, all rounds and endpoints before extracting the metrics.

## Paced input with actual HDMI capture

Fresh services and the same continuously updating RGB332 240x144 demo were used
for each version. Three trials generated twenty positions at 10 Hz, moving along
(200,680) to (580,680). **Both** paths kept one HTTP request in flight and replaced
queued movements with the newest coordinate, matching the browser's existing
queue. Thus the baseline was not forced to replay obsolete mouse events. Each
trial first warmed its starting coordinate. Endpoint detection used white cursor
pixels in the otherwise black bottom margin of real 1280x720 HDMI capture; all
trials began with that endpoint region empty.

| Median, n=3 (ms) | v14 | v15 |
| --- | ---: | ---: |
| Last source event to its HTTP request starting | 802.0 | 215.8 |
| Last source event to final cursor first seen in HDMI | 1725.0 | 1696.2 |
| Last source event to completion confirmation response | 1916.3 | 2403.8 |

HDMI worst observations were **1807.3 -> 2027.6 ms**. The median HDMI difference
is only 28.8 ms across three trials and does not establish a meaningful latency
improvement; the worst case regressed. Transmitted movement counts per twenty
source events were 4/3/3 before and 5/6/5 after. Input queue wait decreased, but
accepting additional coordinates and publishing their overlays still consumes CPU
and display time. Completion polling can itself return after the cursor is visible;
its response must not replace the actual capture observation.

These are PC monotonic intervals including HTTP/UART and USB capture/polling
buffering. They are not optical input-to-photon measurements. They cannot be
compared directly to the shorter board-local receipt durations. Raw source-event,
HTTP and native timings are in `results/input-mailbox-v15-paced.json`; frame JPEGs
and full capture timelines remain under `evidence/input-mailbox-v15/paced-before`
and `paced-after`. Reproduce with `tools/benchmark_pointer_live.py LABEL --output
OUTPUT` against the prepared demo and a matching legacy/async bridge.

## Final validation and limits

Actual HDMI also confirms the final queued pointer at (700,680), thin outlines,
hidden-dot dragging and cancellation. The hidden preview changed 1,971 red pixels;
cancellation restored the red mask exactly (0 residual pixels). Pointer display
preference is restored and the app continues updating. See
`results/input-mailbox-v15-live.json`.

The requested acceptance/completion separation, latest-coordinate mailbox and
bounded input priority are implemented. **The overall interactive latency problem
is not solved.** CPU composition and synchronous publication still take substantial
time, and receipt checkpoints cannot preempt an active hardware ioctl. The native
burst gain must not be presented as a comparable improvement in mouse feel.

The final service is `/tmp/triangle-boayo-v15-tuned`, with the v15 input client.
Artifacts are copied under `/root/triangle-wm-v15`; boot startup is unchanged.
Transfer validation used receiver checksums, gzip CRC/size and output FNV/size
checks in the patch helper. Service SHA-256 in the manifest is the host artifact's
hash, not an independent board SHA readback. The client did receive a board SHA
check. Source changes remain in the existing worktree.
