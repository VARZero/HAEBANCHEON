# Whole-tile resize boundary — v4, 2026-11-24

Historical release record. Current behavior, build steps and board measurements are in [README.md](README.md) and [SOLID_RESIZE.md](SOLID_RESIZE.md); the version-specific results below remain unchanged.

Move is superseded by [v5 full-tile movement](FULL_TILE_MOVE.md). The resize design and the historical v4 results below are retained.

Move keeps the existing small central marker, as requested. Its 64 colored cells
are 1/16 of a full BS25 tile's 1,024 cells (roughly one quarter of its side length).
The tile itself was never that small; the earlier preview painted only its center.

Resize now displays the **entire boundary triangles of the proposed window region**
in splash color S. It does not use a single corner marker or a one-cell-wide line.
The proposed padded footprint uses the same geometry/trim rules as the final window.
A tile is on its boundary if a covered part of an edge has a neighboring tile
outside the footprint, including split-edge neighbors.

During capture, the real window, content and size remain frozen. Growing and
shrinking move this S-colored ring to indicate the intended size. Release performs
one real geometry/descriptor preparation and B/latest-content publication. Cancel
restores the original scene. Higher windows occlude the preview at the original
window's z-order. Latest content/damage coalescing and optional pointer preference
remain intact.

## Metadata path

All boundary tiles point to the **same immutable solid S payload**, already filled
in each bank at startup. No per-tile mask, copy, blend, texture upload or projector
job is needed for a steady resize preview. The original frozen base pixels are
never overwritten by the ring.

Each inactive bank remembers its prior boundary list. Entries leaving that bank's
ring are restored from the frozen base; newly entering entries point to the S
payload. Entries in both lists are not rewritten. CPU metadata buffers continue
alternating without a full header rebuild/copy. The driver still uploads all 4,476
metadata words and waits for output completion.

Resize must determine its new target footprint/rim. It uses the existing fixed
geometry, grouped plane rejection and a cached WindowTransform, not a content plan.
`resize_prepare_us` reports this boundary preparation separately. Identical
coordinates skip it; a changed coordinate that produces the same boundary does not
publish again. Move continues to skip footprint construction entirely.

## Validation and artifacts

The independent byte reference checks all 1,024 cells of every visible tile and all
4,220 directory entries over changing rings and alternating banks, including
occlusion, cancellation and restoration. Every exposed boundary tile is uniformly
S-colored; other tiles retain their base bytes. No steady ring payload operations,
new fills or BO_RUN submissions occur. Socket tests verify changed hashes are
confined to old/new rims, live geometry remains fixed, pending content stays held,
and release rebuilds and publishes the actual window once.

ASan/UBSan, binary/JSON input, mailbox, rollback, three pixel formats and the retained
legacy outline reference pass. The same final SD image boots and exercises move
and whole-tile resize under actual RV32 executables in QEMU. Emulator timings are
not hardware latency. [Offline operation counts](results/tile-rim-offline.json).

The new image is separate from the v3 artifact:
`D:\temp\haebancheon-tile-rim-preview\HAEBANCHEON-tile-rim-preview.img`.
SHA256: `e0e9f7606d59c1f746c2920fb8a52a8748ac2a638ee6692d215bd9cf232359c7`.
Its kernel/driver, fixed geometry and binary transport are retained. Runtime mode
reports `single-tile` for move and `tile-rim` for resize. `TRIANGLE_PREVIEW=outline`
retains the older thin-cell outline only as a reference.

## Physical-board result

The v4 program is installed on the board and in its running RAM executable. The
previous SD program/identity are backed up with `.v3` suffixes. The startup path
will stage v4 on subsequent boots. Kernel, app and native input binaries are
unchanged. This is a userspace hot update on the existing SD; the separately
prepared v4 image passed QEMU boot, not a new physical-image boot/power cycle.

[HDMI verification](results/tile-rim-hdmi.json) passes. The recorded resize has 23
complete S-colored boundary tiles. The old actual window remains in place during
preview; releasing removes S and presents the new B-colored window with content.
Static title counts are 4763/4763/4763 before press/during press/after moving the rim.
Move still uses its 64-cell central marker. Pointer preference is restored and no
pressed button, active preview or capture remains. The final live stream and async
input bridge are available at `http://127.0.0.1:8767/`.

[Native timing data](results/tile-rim-board.json): same original 20x14 degree window,
240x144 RGB332 app and active two-second content updates. Sixteen alternating
positions per action, +/-90 horizontal and +/-24 vertical pixels. Start/end are
native input-submit start through output-publication completion confirmation;
UART/HTTP transfer, USB capture and optical display delay are excluded.

| Preview command-to-completion | v3 central/corner marker median | v4 median | v4 P95/max |
|---|---:|---:|---:|
| Move, central marker retained | 349.7 ms | 351.1 ms | 449.3 ms |
| Resize, whole boundary tiles | 352.0 ms | 462.0 ms | 489.0 ms |

P95 uses nearest rank and equals max at n=16. Resize's additional work includes
target footprint/rim selection: median 112.4 ms, P95/max 126.9 ms. The older thin-cell
outline reference measured 946.2 ms for resize; v4 whole-tile rim is about 51% lower.
Those older references are the preceding v3 board runs, not remeasured v4 outline
trials. Move's performance remains effectively unchanged. The 300 ms target remains
unmet; accepted-to-publication alone (176.7 ms move / 320.6 ms resize) is not a
substitute for command-to-completion.

Press through first preview is 1771.2 ms move / 1961.9 ms resize. Release through
actual transform completion is 1629.4 ms move / 1558.2 ms resize. Each is one sample,
not a distribution. Each release makes one real publication. Across sixteen resize
inputs, there are sixteen boundary footprint calls, zero descriptor-plan rebuilds,
zero BO_RUN submissions, zero texture uploads and zero preview payload operations.
The target ring alternates between 19 and 23 tiles. Final commit rebuilds once.

Installed files were transferred with rolling checksum/gzip CRC and size checks;
staged sizes, atomic file replacement, v4 manifest, actual runtime behavior and HDMI
were checked. Full SD-file SHA256 and later optional full-file comparison were
stopped after long CPU waits; no complete SD-file hash/byte-match result is claimed.
The standalone image's full SHA256 was verified on Windows. No file diagnostic
remains running, and live input is restored.

Recording: `D:\temp\haebancheon-tile-rim-preview\tile-rim-preview.mp4`, 30.3 seconds,
with capture timestamps preserved, phases and command times labeled. Deliberate
two-second holds demonstrate frozen content; they are not processing latency.
