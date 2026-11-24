# Full-tile movement preview — v5

Historical release record. Current behavior, build steps and board measurements are in [README.md](README.md) and [SOLID_RESIZE.md](SOLID_RESIZE.md); the version-specific results below remain unchanged.

Movement now paints one **complete 1,024-cell BS25 triangle** with splash color S.
The earlier 64-cell central patch is removed. The selected triangle and final
precise target-coordinate rules are unchanged; only the visual footprint and its
payload path change. Resize continues to paint all whole boundary triangles of
the proposed region, as implemented in v4.

The move triangle points directly to the same immutable S-color tile already
available in each bank. There is no central mask, marker base copy, per-cell blend
or marker payload construction. Each inactive bank restores its previous marker
entry from the frozen scene and redirects the new entry to S. Higher-window
occlusion, latest-coordinate replacement, pending content coalescing, cancellation
and one final real transform/publication remain intact.

The output driver still transfers the entire 4,476-word header and waits for its
publication acknowledgment. No new hardware latency is claimed. A whole tile can
temporarily cover more of the old content; all its original bytes are restored
when the preview moves away or is canceled.

Validation checks every one of the 1,024 bytes of every marked/restored tile over
both banks, including content, background, no-background and higher-window cases.
Move preview must always reuse the solid payload, report 1,024 colored cells and
perform zero payload operations/builds. Socket tests exercise move, resize,
cancel, held/latest content, rollback and latest-input completion with both binary
and JSON reference transport. The image also boots actual fixed RV32 binaries in
QEMU and checks the reported 1,024-cell, zero-payload-work path.

New artifact (separate from v4):
`D:\temp\haebancheon-full-tile-preview\HAEBANCHEON-full-tile-preview.img`.
The kernel, improved driver, fixed-point geometry and binary transport remain.
The image was initially prepared offline. Subsequent same-session physical board
measurements are recorded in `results/full-tile-board.json`: move command to
publication median 359.8 ms (v4) versus 355.8 ms (v5), 16 samples per build. No
clear median speed gain was measured; v5 eliminates the remaining marker payload
work while making the marker larger. Tail variability is preserved in the report.
Continuous host-to-HDMI input was separately measured and motivated the v6
latest-coordinate transport described in `FAST_MOTION.md`.
