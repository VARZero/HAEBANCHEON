# Shared pixel formats and CPU projection optimization

The application calling sequence stays `create_window → create_surface → commit_surface → wait_present`. `create_surface` now accepts an optional `format`. Omitting it on a new surface preserves the existing RGB24 ABI. Repeating the call without a format returns the existing surface; changing a live surface's format is rejected.

| Format | Shared pixel layout | One 240×144 slot | Application drawing | WM conversion before existing projector |
|---|---|---:|---|---|
| `RGB24` | R, G, B bytes | 103680 B | Legacy demo draws RGB32 and packs RGB24 | RGB24 → RGB32 |
| `XRGB8888` | Little-endian word `0x00RRGGBB` | 138240 B | Native 32-bit pixels | Row copies, no color conversion |
| `RGB332` | Byte `RRRGGGBB` | 34560 B | Native 8-bit indices, including fills and bitmap text | Changed rectangles expanded through a 256-entry RGB32 table |

For XRGB8888, write zero in the unused high byte. The projector ignores that byte; bytewise dirty comparisons include it. RGB332 uses 8 red levels, 8 green levels and 4 blue levels. The expansion table matches the existing renderer's `source_color` exactly. This UI's palette-based text and rectangles therefore retain the same source colors; arbitrary RGB24 images can lose color precision when quantized before interpolation.

Example application request:

```json
{"id":2,"app":"example","op":"create_surface","args":{"window_id":1,"format":"RGB332"}}
```

Use the returned `stride`, `slot_bytes`, and `format`; do not assume three bytes per pixel. Both slots and the strictly increasing sequence/damage rules are unchanged. The acknowledgement still releases a privately snapshotted slot, and `wait_present` still acknowledges actual publication.

## What RGB332 changes on the current FPGA

```
8-bit application image → 8-bit shared slots → changed-area lookup expansion
                                             ↓
                                  cached RGB32 source → projection cores
                                             ↓
                                   RGB332 output tiles → output palette
```

The checked-in projector RTL reads source channels from `gmem_RDATA[23:16]`, `[15:8]`, and `[7:0]`. It has no supported packed 8-bit texture mode. Consequently, initial GPU texture upload remains 138240 bytes for a 240×144 source in all three cases. The v3 comparison uploaded 7680 bytes of full-width RGB32 rows for an 8-row update. The v5 [resident-texture implementation](TEXTURE_CACHE.md) adds a sparse staging/DMA path and separately measures its latency. Moving a cached source does not repeat RGB332 expansion.

A direct packed RGB332 texture mode would require projector RTL and driver bounds/format support. It should expand each sampled index before the existing bilinear interpolation. This change does not modify the FPGA or claim that GPU texture bandwidth has fallen by four times.

Persistent WM memory reservations are 6, 10, and 12 bytes per source pixel for RGB332, RGB24, and XRGB8888 respectively: two shared slots plus the private 4-byte source. They remain capped at 64 MiB. Temporary snapshots and application-owned image memory are separate.

## Projection planning

The following describes the first optimization (v3). The v4 service replaces per-tile floating-point calculations with guarded Q44 integer calculations, adds conservative content filtering and reuses plan buffers; see [CPU_OPTIMIZATION.md](CPU_OPTIMIZATION.md).

The sphere does not change when a window moves. The generator now precomputes its four linear coefficient vectors per triangle. The runtime computes the window's projection basis once and takes dot products against those vectors. This removes per-tile source-coordinate divisions and repeated coefficient assembly.

Reassociation can affect floating-point rounding. Values near a descriptor half-integer or a scale boundary fall back to the original calculation. A deterministic host comparison checked all 4220 tiles across 320 poses/FOV/source-size cases: 1350400 descriptors, zero differences in any descriptor word or visibility result, 5521 guarded fallbacks. The generated table uses hexadecimal double literals. RGB332 and RGB32 painters also matched across all 256 palette colors.

## Commands

```sh
triangle-app --rgb24 --formats-only
triangle-app --rgb32 --formats-only
triangle-app --rgb332 --formats-only
triangle-app --rgb332             # full movement/resize/overlap/pose benchmark
triangle-app --rgb332 --demo      # live indexed UI demo
```

`triangle-app` defaults to RGB332. RGB24 and XRGB8888 remain explicit alternatives; a smaller shared buffer alone does not prove lower end-to-end latency. Board comparisons are reported separately from host correctness checks.
