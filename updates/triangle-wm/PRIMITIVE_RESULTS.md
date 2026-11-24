# Direct tile and cell update measurements

Measured 2026-11-22 on the connected board, rebooted and without WM/app/input
processes. RGB332: one logical triangle tile contains 1,024 one-byte cells (1 KiB).
A cell is a sample inside a spherical tile; its size in HDMI pixels depends on
projection and location. A side-N subtriangle selects N squared cells in native
triangular storage, not an N-by-N screen-pixel rectangle.

Each case has two warmups (both scene banks), then ten samples. Masks, camera,
metadata and source bytes are prepared before timing. Every sample writes only
the inactive bank, then calls BO_PUBLISH to switch it into display. The reported
completion is the synchronous output acknowledgement, not optical pixel latency.
Preparatory reset fill, prepared-source upload, printf and verification are
outside the timed interval. The masked-copy operation deliberately includes a
fresh 1 KiB scratch upload inside its write interval, matching the source staging
requirement; `blend_only_ms` separately retains the portion after staging.
The source data alternates a uniform byte value per sample; copy/upload use the
arbitrary RGB332 byte-data path, not conversion or projection. No BO_RUN, geometry
planning, WM, IPC, JSON, browser, pointer, content conversion or UART transmission
is included in the intervals. This is user-to-kernel ioctl wall time, including
CPU/driver/DMA overhead; it is not raw DMA bus time.

## Whole tile and publication floor

Median milliseconds, n=10. Median components are computed independently; they
need not sum to the median total.

| Operation | Payload write | Publication | Write + publication |
| --- | ---: | ---: | ---: |
| publish_only | 0.351 | 116.514 | 116.881 |
| tile_dma_fill | 2.802 | 114.015 | 116.838 |
| tile_dma_copy | 2.914 | 113.986 | 116.906 |
| tile_upload | 1.592 | 115.342 | 116.964 |

The publish-only write column is the empty timed block/clock overhead (0.351 ms),
not a memory operation. Its median publish is 116.514 ms. Warm publications upload
zero metadata words in this test: the directory/palette are unchanged per bank,
but the driver still copies/validates/compares the complete 4,476-word snapshot and
waits for publication. This distinguishes the fixed software/driver completion
cost from the 1 KiB data operation. Do not subtract independently sampled medians
to claim a precise hardware-only cost.

## Cell count and placement

Median milliseconds, n=10. Contiguous selection is a side-N subtriangle anchored
at the tile vertex, yielding a contiguous N-squared-byte prefix. Scattered
selection uses cell `(index * 257) mod 1024` with exactly the same selected count.
At 1 and 1,024 cells the two layouts select identical masks; differing values
there reflect run/scheduling variation, not different work. Copy includes scratch
staging; the final column is the contiguous copy plus publication.

| Cells | Triangle side | Solid contiguous | Solid scattered | Copy contiguous | Copy scattered | Contiguous copy + publish |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 3.073 | 4.260 | 4.607 | 5.766 | 116.988 |
| 4 | 2 | 6.648 | 3.169 | 8.161 | 4.746 | 123.906 |
| 16 | 4 | 5.473 | 3.583 | 5.911 | 7.584 | 116.857 |
| 64 | 8 | 4.342 | 6.297 | 5.894 | 9.726 | 118.112 |
| 256 | 16 | 4.208 | 13.674 | 6.498 | 20.379 | 116.765 |
| 1024 | 32 | 4.491 | 4.507 | 6.061 | 7.032 | 116.713 |

The driver scans 256 mask nibbles regardless of selected count. Fully covered
32-bit words can be grouped into DMA runs; partial words require CPU read/modify/
write (and source reads for copy). Thus fewer selected cells are not guaranteed
to be faster, and scattered partial-word changes can be slower than whole-tile
DMA. Small-sample differences are noisy: these sequential ten-sample runs are
not a statistically established ranking. No outliers were removed; all raw
samples, P95 and maxima are retained in `results/primitive-updates.json`.

The main observed floor here is publication (~110-126 ms median), while cell
writes range from a few ms to ~20 ms. The earlier 0.5-2 second pointer figures
include substantial WM/input work not present in this test. These measurements
identify the primitive cost without attributing all prior delay to one unmeasured
component.

## Verification and reproduction

The final sample of every case was copied from the scene bank to a separate
scratch location, downloaded through the supported BO_DOWNLOAD scratch API and
checked byte-for-byte: selected cells match the new value, unselected cells retain
the base. All 28 cases / 280 timed samples completed with PRIMITIVE_DONE and rc 0.
The initial harness preflight failures (alignment/header and unsupported direct
scene download) are preserved in evidence and are excluded; the final harness
uses aligned scene size, correct bank header, and scratch verification.

Sources: `tests/primitive_bench.c`, `tests/generate_primitive_init.cpp`.
Generate `primitive_init.h` with the native Geometry implementation, then compile
the C program with the existing RV32 toolchain and BOSIO UAPI include path.
Run exclusively with `/dev/bosio0` and no WM/app; it stops the output at the end.
The program is `/tmp/primitive-bench` on the board. No firmware/kernel/WM changes
were made by this experiment. Run analysis using:

```
python tools/analyze_primitives.py evidence/primitive/TRI_bccbfe57ba.log results/primitive-updates.json
```

Final host binary SHA256: `e3fa81a488426b5a31461ecea01cb990937e5a33f4255cb784249c247134b591`.
Transport was verified by receiver checksum, gzip CRC and extracted size; this
is not an independent board SHA256 check. The host HDMI monitor is available on
port 8766. There was no live WM at task start; the benchmark leaves the board on
with output stopped after testing.
