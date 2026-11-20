# Whole-tile outline experiment

The v7 experiment removes unsupported background tips while preserving every tile
that can contain application content. It keeps the BoAYO application ABI, RGB332
application v3, FPGA, driver, texture cache and publication protocol unchanged.
No per-pixel silhouette mask, outline stroke or rounded-corner rendering is added.

## Selection rule

1. Select the original conservative padded rectangle footprint.
2. Count how many distinct edges of each selected tile touch another selected tile.
   A 7/8 subdivision seam may have two neighbors along one edge; this still counts
   as one supporting edge.
3. A tile supported on fewer than two edges is a candidate for removal. Test that
   candidate against the unpadded content frustum with the existing Q13 tolerance.
   Keep it if it may intersect content. Remove only unprotected background tips.
4. Repeat simultaneous removal until stable. Keep sorted tile IDs for the existing
   projection cache and ownership code.

This is a deliberately modest cleanup. It does not guarantee symmetry, equal
screen-space triangle sizes, straight horizontal edges or a stable silhouette as
a window moves across the spherical grid. The default 20 by 14 degree window with
2 degree padding changes from 72 to 70 tiles. The 7/8 mesh seams and the larger
side zigzags remain. Simply filling local notches or applying graph closing did
not improve the default silhouette in the offline prototypes, so those methods
are not in the runtime implementation.

Adjacency is generated at build time from the pinned physical geometry. The
generator verifies all 12,660 edges have reciprocal, complete, non-overlapping
coverage, including 780 split edges. Opposite-side spherical branch cuts are
excluded from adjacency. The target uses only integer neighbor IDs for pruning.
There is no runtime topology search. Content-protection planes are calculated
once per footprint update; only candidate tips are tested against them.

The pass runs when the footprint changes: create, move and resize. Pixel-only
commits and output-pose changes reuse the footprint. `TRIANGLE_OUTLINE=raw`
disables the pass; `TRIANGLE_OUTLINE=trim` selects it, and trim is the v7 default.
This does not change the configured padding value, but it intentionally removes
some outer background tiles that the old padded-intersection rule selected.

## Correctness checks

- 600 randomized/edge-case windows: longitude seams, high latitude, varied sizes,
  zero padding, sorted subsets and idempotence. 377 footprints changed; 675 removed
  tiles were checked against the independent projection reference, and none
  contained visible content. Zero-padding footprints were unchanged.
- 4,321,280 projection/visibility comparisons remained exact.
- ASan/UBSan BoAYO ABI tests passed for RGB24, XRGB8888 and RGB332, including
  partial/full equivalence, ownership, resize, overlapping windows and pose calls.
- Solid-window and incremental-versus-full content regressions passed.

## Physical comparison

Three fresh-service runs used the identical RGB332 app v3 and contiguous texture upload: v6 original, v7 trim, then v7 raw control. Each completed 45 operation samples and 61 precomputed output-pose steps, with `BENCH_DONE` and exit status zero. The v7 raw/trim comparison uses the **same executable**, changing only `TRIANGLE_OUTLINE`. One sequential run per condition is not a statistical confidence study; small differences include scheduling/publication variation.

Application-call to publication-completion response, milliseconds; optical latency excluded:

| Operation | n | v6 raw median | v7 raw median | v7 trim median | trim minus v7 raw | trim P95 / max |
|---|---:|---:|---:|---:|---:|---:|
| initial_content | 1 | 1455.920 | 1500.872 | 1464.786 | -36.086 | 1464.786 / 1464.786 |
| partial_8x8 | 11 | 431.107 | 430.465 | 434.918 | +4.453 | 623.089 / 623.089 |
| content_move | 7 | 829.511 | 830.684 | 837.237 | +6.553 | 887.821 / 887.821 |
| content_resize | 7 | 854.421 | 878.565 | 892.658 | +14.093 | 901.097 / 901.097 |
| content_restore_size | 1 | 815.555 | 802.444 | 828.308 | +25.864 | 828.308 / 828.308 |
| second_content | 1 | 1387.698 | 1407.071 | 1414.327 | +7.256 | 1414.327 / 1414.327 |
| third_content | 1 | 1549.729 | 1531.723 | 1509.652 | -22.071 | 1509.652 / 1509.652 |
| overlap_move | 5 | 815.223 | 806.446 | 824.974 | +18.528 | 850.224 / 850.224 |
| raise | 1 | 593.209 | 600.505 | 572.907 | -27.598 | 572.907 / 572.907 |
| close | 2 | 454.676 | 449.450 | 441.538 | -7.912 | 499.619 / 499.619 |
| view_pose | 7 | 204.548 | 201.415 | 200.312 | -1.103 | 242.383 / 242.383 |
| destroy_owned | 1 | 351.267 | 340.524 | 341.757 | +1.233 | 341.757 / 341.757 |

The controlled v7 comparison shows +4.453 ms for the 8x8 update, +6.553 ms for movement, +14.093 ms for resize and +18.528 ms for overlapping-window movement. Pixel updates do not execute the pruning pass; the small update difference should not be attributed to pruning work. **This is a visual cleanup with a small CPU cost, not a speed optimization.**

| Footprint computation, median ms | v7 raw | v7 trim | Added |
|---|---:|---:|---:|
| content_move | 94.443 | 111.232 | +16.789 |
| content_resize | 94.076 | 116.161 | +22.085 |
| overlap_move | 87.682 | 107.055 | +19.373 |

The added footprint interval includes content-protection plane construction and trimming; they have not been timed separately. End-to-end and component medians are independently aggregated and cannot be added/subtracted as an exact latency decomposition.

Default owned background tiles decreased from 72 to 70. Movement median owned tiles changed from 60 to 58; resize from 73 to 71; overlapping scenes from 112 to 107. Changed ownership can expose underlying content, so overlap workload is not necessarily pixel-identical even though each window retains all of its content tiles.

The trim run kept all measured existing-window updates/moves/resizes below one second (largest resize 901.097 ms). New-content initialization still took 1.4–1.51 seconds. Create/map plus first content was **1428.017 + 1464.786 = 2892.803 ms**, versus 2902.280 ms for v7 raw and 2846.565 ms for v6 raw; these single startup observations do not establish a startup speed improvement and exclude process/connection work before the timer.

Output-pose sweep median was **45.517 ms** with trim versus **45.480 ms** with v7 raw (61 steps each). Pose calls reuse the scene and do not trim again.

## Visual result and deployment

The physical capture confirms the upper-left and lower-right protruding triangles disappear in the default window. Middle side zigzags and small steps at subdivision seams remain. Movement, resizing and overlap were exercised in the recorded benchmark. This does **not** yet deliver a uniformly shaped or symmetric triangular-window design.

- [Before, physical capture](results/outline-before-live.jpg)
- [After, physical capture](results/outline-after-live.jpg)
- [Actual-geometry outline preview](results/outline-prototype-prune.png)
- [Measured runs and raw samples](results/outline-board-20261121.json)
- [Source/binary/deployment manifest](results/outline-build-manifest.json)
- [v6 benchmark UART](evidence/TRI_f037535c68.log), [v7 trim UART](evidence/TRI_d35716e0a2.log), [v7 raw control UART](evidence/TRI_1932227fb7.log)
- [Trim capture recording](evidence/outline-v7-trim.avi), [raw control recording](evidence/outline-v7-raw-control.avi)

The live temporary deployment is `/tmp/triangle-boayo-v7` in trim mode (PID 391), with the unchanged RGB332 demo `/tmp/triangle-app-v3` (PID 393). Live capture: <http://127.0.0.1:8766/?rgb332=1>. The service and app PID files remain `/tmp/triangle-wm.pid` and `/tmp/triangle-app.pid`. No boot-time installation or FPGA/driver replacement was performed. AVI playback-rate metadata is not optical timing evidence; capture timestamps are in `evidence/capture-timeline.jsonl`.
