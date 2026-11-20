# Completion-response and diagnostic overhead

This follows the [resident-texture optimization](TEXTURE_CACHE.md). The FPGA, driver and RGB332 application v3 remain unchanged. Service v6 changes CPU work around requests and render completion.

## Changes

- Completed-frame statistics are retained in a native struct. The BoAYO service requests rendering without constructing a JSON report; `get_state` materializes the report when requested. The standalone experimental executable still returns its report normally.
- Request arguments are read by reference from the parsed request rather than copied into a second JSON object.
- Hot result objects insert members directly instead of constructing temporary JSON key/value arrays. Responses serialize the result once and add a fixed JSON envelope, avoiding a deep copy into another JSON tree. Serialized values still pass through the JSON library; titles, escapes and numeric IDs are not inserted as raw untrusted text. Encoding is completed before the output queue is modified.
- Shared-slot release still follows a private snapshot. `wait_present` still follows successful hardware publication and advances the same generation. No completion acknowledgement has moved ahead of hardware work.
- The v5 contiguous-upload selection is now the binary default. `TRIANGLE_SPARSE_UPLOAD=1` remains an explicit experimental packed-upload option.

This removes diagnostic serialization from the completion path; it does not make querying or printing diagnostics free. Applications that request `get_state` every frame still pay for those queries. Frame completion latency and total benchmark/diagnostic throughput are different quantities.

## Request accounting

`performance.last_render.request_profile` describes the most recent create/commit/configure/destroy/raise request. It is retained across diagnostic queries and may describe a no-op request separately from the last rendered frame.

- `parse_us`: request decoding and envelope checks.
- `dispatch_us`: validation and operation work up to result construction.
- `reply_us`: response serialization and queueing, excluding the subsequent socket send.
- `snapshot_us`, `pixels_us`, `damage_us`, `footprint_us`: subintervals **inside** dispatch; do not add them to dispatch again.

These integer-microsecond counters do not include client JSON work, client scheduling, socket round trips or optical delay. Render/driver timing fields retain their existing meanings; `service_render_ms` now excludes the on-demand JSON report.

## Validation and board comparison

The unchanged RGB332 application completed **45 operation samples and 61 output-pose steps** on the physical board. Both UART runs contain `BENCH_DONE` and a zero exit status. The v5 baseline uses contiguous uploads (`TRIANGLE_SPARSE_UPLOAD=0`), as does v6. These are consecutive development-session runs, not repeated statistical trials. Small-n P95 values often equal the maximum.

| Operation | n | v5 median (ms) | v6 median (ms) | Reduction | v6 P95 (ms) | v6 max (ms) |
|---|---:|---:|---:|---:|---:|---:|
| initial_content | 1 | 1602.246 | 1502.252 | 6.2% | 1502.252 | 1502.252 |
| partial_8x8 | 11 | 519.021 | 435.843 | 16.0% | 622.475 | 622.475 |
| content_move | 7 | 890.743 | 807.842 | 9.3% | 861.083 | 861.083 |
| content_resize | 7 | 961.407 | 857.706 | 10.8% | 894.481 | 894.481 |
| content_restore_size | 1 | 891.045 | 828.587 | 7.0% | 828.587 | 828.587 |
| second_content | 1 | 1468.347 | 1397.106 | 4.9% | 1397.106 | 1397.106 |
| third_content | 1 | 1600.262 | 1508.984 | 5.7% | 1508.984 | 1508.984 |
| overlap_move | 5 | 930.100 | 825.547 | 11.2% | 827.276 | 827.276 |
| raise | 1 | 696.296 | 604.932 | 13.1% | 604.932 | 604.932 |
| close | 2 | 528.316 | 450.517 | 14.7% | 520.633 | 520.633 |
| view_pose | 7 | 228.678 | 208.056 | 9.0% | 231.705 | 231.705 |
| destroy_owned | 1 | 451.280 | 356.214 | 21.1% | 356.214 | 356.214 |

Timing is application call to output-completion response, **not camera-measured optical latency**. Hardware publication still precedes completion. The benchmark performs diagnostic queries after samples; their deferred work is not eliminated and remains part of total benchmark throughput.

Create/map plus first content was **1398.956 + 1502.252 = 2901.208 ms**, versus **3146.521 ms** in v5. This sum excludes process startup and connection work before the measured create call. The first-content number alone is not total startup. Additional newly populated windows took 1397.106 and 1508.984 ms; therefore not every operation is below one second. All sampled existing-window update/move/resize/overlap operations were below one second in this run.

The precomputed output-pose sweep remained approximately unchanged: median **45.476 ms**, P95 **45.704 ms**, maximum **52.059 ms**, 61 steps; v5 median was 45.027 ms. Preparation took 1649.736 ms and is reported separately. This is not an improvement claim for the pose engine.

### Where CPU time remains

| Request stage, median (ms) | 8x8 update | Window move | Resize |
|---|---:|---:|---:|
| Decode | 24.384 | 18.815 | 17.932 |
| Dispatch | 24.212 | 101.756 | 111.127 |
| Reply serialization/queue | 8.933 | 12.897 | 12.957 |
| Snapshot (inside dispatch) | 2.253 | 0.000 | 0.000 |
| Pixels (inside dispatch) | 0.904 | 0.000 | 0.000 |
| Damage (inside dispatch) | 10.030 | 0.000 | 0.000 |
| Footprint (inside dispatch) | 0.000 | 87.225 | 95.530 |

The 8x8 pixel stage is only 0.904 ms, while publication is 132.026 ms. Movement still spends 277.908 ms in render planning and 87.225 ms in request footprint work; resize spends 293.335 and 95.530 ms respectively. These independently aggregated medians are not additive end-to-end decompositions. Client work, scheduling and socket exchanges also remain. Initial-content pixel work was 491.930 ms; allocation, clearing and conversion require separate profiling before assigning a cause.

### Correctness and evidence

- ASan/UBSan API checks passed for RGB24, XRGB8888 and RGB332, including escaped/Unicode titles and stable deferred reports under repeated queries.
- Shared-slot ownership, invalid requests, content updates, forced texture eviction and explicit sparse uploads passed. Solid-window and incremental-versus-full rendering regressions passed.
- Projection/visibility checks matched 4,321,280 reference cases; texture allocator checks covered 10,000 operations. Camera Q24 maximum error remained one unit.
- Capture inspection confirmed visible content, overlapping windows, movement and resizing. The final RGB332 demo is running on service v6; its changing counter was checked separately from timed samples.

Artifacts:

- [Measured comparison and raw samples](results/response-board-20261120.json)
- [Build/source hashes and deployment manifest](results/response-build-manifest.json)
- [v5 UART baseline](evidence/TRI_b71ce3da56.log), [v6 UART benchmark](evidence/TRI_15fef0a8ab.log)
- [Board binary hash verification](evidence/TRI_0630005b19.log), [service startup](evidence/TRI_0a65eded77.log), [live demo startup](evidence/TRI_f61f7bfcb6.log)
- [Benchmark capture](evidence/response-path-v6.avi), [overlapping-window snapshot](results/response-path-scene.jpg), [live snapshot](results/response-path-live.jpg)

The capture AVI has a nominal playback-rate hint; use `evidence/capture-timeline.jsonl` for capture timestamps. It is visual evidence, not an optical-latency measurement. Live monitoring: <http://127.0.0.1:8766/?rgb332=1>. The deployment is temporary under `/tmp`, not installed as a boot service.
