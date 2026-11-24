# Separating fixed math from binary IPC

Measured on 2026-11-22 on the connected board, without rebooting or changing
kernel `6.12.0-haebancheon-publishcopy`. Every case uses `BO_PUBLISH_COPY`.
The publication-driver improvement is retained throughout this experiment.

## Controlled comparison

One RV32 executable contains separately named float and fixed WM engines.
`TRIANGLE_NUMERIC=float|fixed` selects the server. The RGB332 demonstration app
and native input client are identical in all four cases. Their diagnostic clock
uses double in this comparison executable; application content and pointer
coordinates are integers. This holds client-side changes constant while testing
server math. Startup and executable loading are excluded.

`TRIANGLE_WIRE=json|binary` selects newline JSON or BWM1/CBOR on both ends.
The baseline binary codec is the previously shipped deep-copy implementation,
selected with `TRIANGLE_WIRE_OPT=0`. Other new optimizations are also disabled
for the four baseline cases. Angles and dimensions are integral in this test,
so binary quarter-degree quantization is not a changing variable.

Each case starts a fresh WM and app. Idle IPC is `input_progress`, with two
warmups and twenty retained round trips. Input tests use three sequential
16-coordinate bursts with the app paused, then three with content updating.
There are 48 acceptance samples and three last-point completion samples per
condition. Native timings exclude UART, capture and optical latency. A
last-point sample includes waiting for its display completion, not merely
acceptance. Intermediate moves may be coalesced.

All values below are medians in milliseconds. Smaller is better.

| Server math | Socket codec | Idle round trip | Input accepted, content paused | Input accepted, content active | Last point complete, paused | Last point complete, active |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| float | JSON | 156.464 | 203.609 | 288.276 | 913.864 | 988.508 |
| fixed | JSON | 153.801 | 200.391 | 253.141 | 607.017 | 1286.788 |
| float | old binary | 220.348 | 261.763 | 325.079 | 958.448 | 1102.235 |
| fixed | old binary | 223.988 | 246.065 | 344.716 | 786.989 | 1081.638 |

The old binary codec raises idle round-trip latency by 40.8% with float and
45.6% with fixed math. Switching math changes that same metric by -1.7% for
JSON and +1.7% for binary. Fixed math does not show a consistent large
acceptance penalty. This isolates the codec as a substantial cause of the
regression; fixed-point arithmetic itself is not the main acceptance bottleneck
in this workload.

Completion depends on coalescing and contention with rendering. Only three
completion samples are available per condition and their ordering varies; the
acceptance finding must not be promoted into a claim that every fixed-math
render or every display completion is faster.

## Changes under test

* Binary encode/decode traverses the existing object in place instead of
  rebuilding every map and array. Encoding a temporary request consumes it.
  Binary bytes, numeric tags, size/depth limits and error checks are preserved.
* A control request that cannot run inside rendering is decoded once and
  retained for the normal request pump. Tile checkpoints skip that blocked
  request while still flushing replies. Button and geometry barriers retain
  their order.
* Pointer ray construction caches the two constant field-of-view coefficients.
  Fixed math no longer repeats constant CORDIC tangent and division work on
  each pick. Multiplication order is unchanged.

Default flags enable these changes. Individual switches remain available for
reproduction: `TRIANGLE_WIRE_OPT`, `TRIANGLE_CHECKPOINT_OPT`, and
`TRIANGLE_MATH_OPT`. `0` selects the original implementation for that part.
`TRIANGLE_WIRE` defaults to binary. JSON is a comparison/debug option.

## Optimization measurements

Same executable and boot, fixed math and BWM1 binary in every row:

| Changes enabled | Idle round trip | Input accepted, paused | Input accepted, active | Last point complete, paused | Last point complete, active |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original binary codec | 223.988 | 246.065 | 344.716 | 786.989 | 1081.638 |
| In-place codec only | 181.870 | 192.629 | 267.821 | 734.430 | 919.580 |
| Codec + checkpoint cache + ray constants | 180.805 | 193.556 | 259.703 | 747.990 | 800.618 |

The codec-only change cuts active acceptance by 22.3%. The combined change
cuts it by 24.7%, with a 26.0% reduction in median last-point completion.
Most of the acceptance gain is attributable to the codec. These trials do not
separately establish a latency contribution for each of the other two changes.
Optimized active acceptance is close to fixed+JSON's 253.141ms, but the optimized
binary idle round trip remains slower than JSON. This is not a claim that CBOR
is intrinsically faster than JSON.

The tail is not consistently improved: active acceptance P95 is 522.290ms
before, 953.467ms with codec only, and 941.674ms with all changes. Maxima are
1258.286ms, 1015.427ms and 997.009ms respectively. Faster receipts change their
phase relative to content rendering; these samples cannot establish why the
P95 ordering changed. Content work can still delay input. A stable subsecond
end-to-end interaction guarantee has not been achieved.

## Actual HDMI

The same fixed/binary matrix builds were also tested through the live UART/HTTP
bridge and USB capture. Three active-app trials per configuration use twenty
source positions at 10Hz, one request in flight, and only the newest queued
coordinate. The final point starts from a different position in every trial;
the first observed white cursor in its HDMI region marks completion.

| Configuration | Final source event to first observed HDMI frame, all three samples | Median | Maximum |
| --- | --- | ---: | ---: |
| Original binary | 4285.922, 3128.045, 3666.237ms | 3666.237ms | 4285.922ms |
| All changes | 1429.765, 1752.819, 1877.366ms | 1752.819ms | 1877.366ms |

Median observed delay decreases by 52.2%. These are actual capture observations,
including PC/UART/USB/polling delays, not optical monitor measurements. There are
only three trials; they do not establish a general percentile distribution.
They are separate from the native acceptance and completion numbers above.

Raw live results are retained in
[`factorial-paced-matrix-baseline.json`](results/factorial-paced-matrix-baseline.json)
and [`factorial-paced-matrix-optimized.json`](results/factorial-paced-matrix-optimized.json).

## Dedicated fixed build

The final dedicated fixed WM, fixed demo and fixed input client were rebuilt
separately from the comparison executable and exercised on the board. With the
RGB332 demo active, 48 coordinate submissions had median acceptance 215.147ms,
P95 464.654ms and maximum 1069.807ms. The three last-point completion samples
had median 633.757ms and maximum 838.229ms. Latest-coordinate endpoints and
completion tickets were checked. The runtime reports `fixed-q24`, `BWM1` and
the original `6.12.0-haebancheon-publishcopy` kernel.

This changes the diagnostic app/client arithmetic relative to the controlled
matrix, so it is deployment validation, not another isolated server-math result.
Its twenty `input_progress` calls were made with the app running, not idle;
they must not be compared with the matrix's idle IPC metric. `/tmp` was verified
as a 64MiB tmpfs; binaries ran there for these latency measurements. SD loading
and installation are excluded. Binary hashes, runtime state and all samples are
in [`factorial-final-fixed.json`](results/factorial-final-fixed.json).

The connected SD now contains the dedicated binaries under
`/usr/bin/triangle-boayo-fixed`, `/usr/bin/triangle-app` and
`/usr/bin/triangle-input`, with SDK headers and the v2 build manifest installed.
Previous executables are retained with `.before-codec` suffixes. The startup
script's shell syntax was checked; the board was not rebooted during this
comparison. The earlier downloadable v1 image remains a v1 artifact.
[`factorial-install.json`](results/factorial-install.json) records installation
checks and expected build hashes. Transport checks retain receiver FNV, gzip CRC
and reconstruction validation; file-size checks now use inode metadata rather
than a full BusyBox `wc` scan, with a board transfer probe passing.

The active dedicated WM/app run from `/tmp/triangle-boayo-opt` and
`/tmp/triangle-app-opt`; the live bridge uses `/tmp/triangle-input-opt`. PID files
under `/run` refer to these processes. The optional pointer dot is enabled at
(700,680), no drag/resize is active, and seventeen white pixels were confirmed
in its actual HDMI region. [Live handoff evidence](results/factorial-final-live.json)
is separate from the latency trials. Interactive input is available at
<http://127.0.0.1:8767>, with the capture stream at <http://127.0.0.1:8766>.

## Additional display verification

An initial visual suspicion of missing static labels was disproved by direct
pixel checks of the saved original frames. Four captures taken before and after
reapplying the current view contain exactly the same title/control region
pixels, with identical hashes and maximum pixel difference zero. White-pixel
counts are 4,789 for the titles and 73, 139 and 58 for the three controls.
No display-loss event or recovery was demonstrated; the earlier visual
interpretation was incorrect. No periodic reset or driver rollback was added.

A paused-runtime diagnostic additionally borrowed the existing BOSIO file
description with `pidfd_getfd`, staged scene/texture reads through scratch, and
read the app's existing memfd. It found 1,328 white top-text pixels in both the
texture and app slot, and 1,030 white cells across the visible scene, consistent
with the intact capture.

A 100-second active-demo/SD-read trial sampled the unchanged title and three
controls once per second. Every sample retained the expected white pixels.
The final handoff checks these four regions as well as the cursor endpoint.
[Pixel equality, readback and observation evidence](results/factorial-display-stability.json)
is retained. This is a bounded display check, not a general endurance claim.

## Validation and reproduction

The matrix passed 24 socket regression suites under ASan/UBSan: input,
visibility, mailbox replacement, ordered button barriers, ownership, lifecycle,
mixed pixel formats and content coalescing. For each math/codec pair, all 160
recorded screen-state hashes match with optimizations disabled versus enabled.
Binary tests also compare 128 nested messages against the old pack/unpack
implementation, including exact serialized bytes, in each codec mode.

The isolated fixed build passes the integer-core symbol audit. It still uses
Q24 for geometric calculations and quarter degrees at the angle boundary;
diagnostic/library serialization is not claimed to be entirely FP-free.

```sh
bash updates/triangle-wm/tools/build_matrix.sh /var/tmp/triangle-matrix rv32
bash updates/triangle-wm/tools/build_matrix.sh /var/tmp/triangle-matrix-san sanitize
python3 updates/triangle-wm/tools/validate_matrix.py /var/tmp/triangle-matrix-san/wm-matrix
```

After verified UART deployment as `/tmp/wm-matrix`, with the input bridge
stopped so it does not own COM4:

```powershell
python updates/triangle-wm/tools/benchmark_matrix.py
python updates/triangle-wm/tools/benchmark_matrix.py fixed-binary-codec fixed-binary-optimized
python updates/triangle-wm/tools/analyze_matrix.py
```

Raw timing samples, configurations and matrix SHA256 are preserved in
[`results/factorial-comparison.json`](results/factorial-comparison.json).
Ignored UART logs, executable, screen traces and capture frames are retained
under `evidence/factorial/`.
