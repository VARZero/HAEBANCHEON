# Fast whole-tile resize preview — v12

Resize now uses the same direct latest-coordinate WM path as move. During a drag,
the real window/content remain frozen and the target region's complete boundary
triangles share the immutable S-colored payload. Release uses the existing exact
final transform, B background and latest content. No per-input pixel mask, payload
construction or projection submission is introduced.

The capture caches invariant starting axes, tangents, anchor and press-ray ratios.
Only pointer-dependent calculations repeat. This preserves the original quarter-
degree target and final transform, including grow/shrink and cancellation.

Quarter-degree sine/cosine and eighth-degree tangent values used by window
transforms are generated on the host with the original integer functions. The
board reads their exact raw Q24 values, without a warm-up calculation. Non-grid
or out-of-range inputs retain the original arithmetic. Every entry and fallback
was checked against that original implementation.

Footprint preparation also uses exact 13-bit plane normalization without general
64-bit division in its normal bounded domain. It matches the original Q24 divide,
scale and rounding result exactly; large/unusual values retain the old fallback.
The generated mesh's 132 group bounds are checked through 33 coarse bounds first.
Each coarse bound is a compile-time union of four original group boxes. Rejecting
it cannot discard an original hit; surviving groups retain the original test and
ordering, including background-tip trimming.

Verification includes 2,279,068 exact plane-coefficient comparisons, 1,024 random
poses/sizes/paddings comparing complete footprints against the original flat scan
with both trim settings and cached transforms, and old-server vs new-server tests
of exact target, rim, pixel hashes and final release for grow/shrink in three poses.
ASan/UBSan, legacy BWM1/JSON input/content/rollback suites, raw fast resize packet
ordering and the final RV32 image boot/marker suites also pass.

The physical comparison starts both versions at azimuth/elevation 0, size 20x14
degrees, with the same active 240x144 RGB332 demo and scheduling priorities. Twenty
inputs at 10 Hz describe grow/shrink, followed by a distinct final grow jump. Both
versions must match the exact initial/final geometry and final rim IDs. The same
captured final-only S-colored pixel mask detects arrival. Calibration and progress
queries occur outside the timed input stream. Each version retains three trials.
Timing includes PC input, HTTP, UART and USB HDMI capture, but not optical display
latency. Acquisition and final full-window transform are separate operations.

An earlier baseline used the user's then-current larger live window. It is kept in
evidence but excluded from the comparison after the geometry mismatch was detected.
The first direct/cached-only intermediate build showed no clear end-to-end gain;
its unpooled measurements are also retained. Final hardware results are recorded
separately in `results/fast-resize-board.json` after board validation.

App/input binaries, BWM1/PTR1 application/control interfaces and the improved
publication driver/kernel are retained from v9. The image is available separately
at `D:\temp\haebancheon-fast-resize-preview\HAEBANCHEON-fast-resize-preview.img`.
Build manifests describe offline image scope; board reports describe live validation.

Final physical comparison (three trials per version): original median 524.1 ms
(range 497.6-584.5), final median 469.5 ms (range 442.0-580.9). This is a modest
observed median change with overlapping ranges, not evidence that resize matches
move latency or guarantees 300 ms. Target/rim/probe were identical. Native final
footprint preparation median changed from 129.6 to 117.4 ms. Move/resize physical
release, frozen content and pointer preference checks passed.
