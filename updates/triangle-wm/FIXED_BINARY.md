# Fixed-point WM and binary IPC image

The current [filled splash resize v13](SOLID_RESIZE.md) retains this driver/math/IPC foundation and has been validated on the physical board and saved for boot. The original bundle and board results below refer to the earlier release; the current build/validation/image steps are in [README.md](README.md#build-validate-and-package-the-current-release).

[Initial board results](FIXED_BOARD_RESULTS.md): publication improves, but the original fixed/binary bundle regresses pointer latency versus v15. The subsequent [controlled comparison and codec fix](FACTORIAL_RESULTS.md) isolates the transport overhead and measures the optimized implementation on the same board.

This release combines the CPU geometry path, latest-input mailbox and thin-cell
outline compositor with the BO_PUBLISH_COPY driver. The original image was
prepared offline. It has since been booted and tested on the physical board;
the linked reports contain actual native timings and HDMI capture observations.
Host/QEMU timings are not substituted for board results.

## Numeric formats

- Angles on the wire: signed quarter degrees (integer raw / 4), constrained to
  int16 range. Window angles/dimensions/padding are rounded to this grid. Ties are
  rounded away from zero. The angle API's finer fractional semantics have changed.
- 0..360 degrees needs nine unsigned integer bits plus two fractional bits
  (eleven total). If signed and +360 must be representable, add a sign bit (twelve
  total). Normalized -180..180 needs only eleven signed total bits. The protocol
  uses a practical signed 16-bit container rather than custom bit fields.
- Vectors, trigonometry, projection, rays and intermediate values use signed int64
  scaled by 2^24. Quarter-degree storage precision is not used for cell-coordinate
  calculations. Pointer x/y remain full-resolution integers, not snapped angles.
- CORDIC replaces sin/cos/tan/atan/atan2/asin; integer square root replaces sqrt and
  hypot. Host-generated immutable geometry replaces floating startup geometry.
  There is no floating reference fallback in the fixed descriptor implementation.
- Fixed-point candidate rejection precedes descriptor construction. Tests assert
  that sampled reference-visible tiles are never discarded. Bounds and clipping
  retain guard margins; all geometry parameters retain range validation.
- Timestamps in WM, demo app and input client are calculated from integer clock
  values using the fixed scalar. Per-update demo formatting/state reports are
  removed; initial and explicitly requested diagnostic output remains available.

The numerical core audit compiles a live RV32 camera/descriptor path and checks
undefined symbols for floating helpers and libm calls. It passes. This is **not**
a claim that every executable/library contains no float/double type or helper:
the nlohmann object model and diagnostic CLI/HTTP/UART text bridge still contain
numeric formatting/parsing code. At compatibility boundaries IEEE numbers are
extracted/reconstructed by integer bit operations; normal app/WM wire payloads
contain no floating CBOR values. No JSON text parser/formatter runs on that socket
path. Full library removal would require replacing the dynamic value model and
diagnostic interfaces as a further change.

## Binary protocol BWM1

Each Unix-stream message starts with eight bytes: little-endian uint32 magic
0x314d5742 (ASCII BWM1), then little-endian uint32 payload length. The payload is a
definite CBOR object, at most 65,536 bytes. Stream fragmentation and concatenated
frames are supported. Nesting is limited to 32 at the value conversion boundary.
Unknown magic/oversized frames are disconnected; malformed payloads produce a
binary error response without mutating a window.

Existing operation/argument names remain in the binary object. Named angular
fields (yaw/pitch/roll/azimuth/elevation/width_deg/height_deg/padding_deg) are encoded
as a one-key map with reserved key NUL-a2 and signed quarter-degree raw value.
Other fractional numbers use reserved key NUL-q24 and signed raw/2^24. Those tags
are decoded into the existing in-process value model. Native floating CBOR values
are rejected. Reserved tag keys cannot be supplied by an application value.
Integer coordinates, IDs, dimensions and booleans keep their native types.

Application pixel buffers still use shared memory; they do not travel as CBOR.
The C++ Client API is source-compatible, but the socket protocol is incompatible
with old text-JSON executables: rebuild them with the bundled ipc.hpp and
binary_wire.hpp. A minimal SDK is installed at /usr/share/triangle-wm/sdk/src
with its vendor dependency next door. The packaged app and input client are
rebuilt for this protocol. The host browser HTTP and UART diagnostic bridge still
use JSON outside the app/WM socket; a matching new /usr/bin/triangle-input bridges
them. The old v15 bridge executable must not be reused.

## Driver, runtime and profiling

The service defaults to BO_PUBLISH_COPY on the new kernel. It performs full
metadata transfer and synchronous output completion without the per-directory
validation/dirty comparison/CPU snapshot saves. Scalar bounds, inactive bank,
stable pose and failure/timeout handling remain; CAP_SYS_RAWIO is required.
TRIANGLE_PUBLISH_COPY=0 selects the original checked path for controlled tests.
No performance gain is claimed before physical-board measurement.

New diagnostics ui_layout_us, ui_pick_us, ui_masks_us and ui_payload_us distinguish
control placement, tile picking, mask construction and overlay composition/write
calls. ui_pick_us is a subset of ui_masks_us; do not sum them. ui_payload_us includes
CPU bookkeeping and synchronous batch calls, not pure bus-transfer time.

The image starts triangle-boayo-fixed plus the RGB332 demo through S99triangle-fixed
only when /dev/bosio0 exists. Runtime commands:

```
triangle-fixed-start stop
triangle-fixed-start start
triangle-boayo-fixed --ctl '{"op":"get_state"}'
```

The control CLI text above is a human-facing diagnostic front end. It sends BWM1
binary IPC to the service. It does not restore the old text socket protocol.

## Validation

- ASan/UBSan: binary framing/fixed tags, numeric multiply/divide/CORDIC, geometry
  reference, socket permissions, window lifecycle/overlap, pointer visibility,
  move/resize/cancel, capture ownership, latest-coordinate replacement and content
  progression all pass. App API tests pass against the new binary client helpers.
- Numeric reference: maximum sampled sin/cos absolute error 1.08e-7; descriptor
  source-coordinate error 0.000320 pixels across 3,068 covered cell samples and
  the tested poses/dimensions. This excludes intentional quarter-degree input
  quantization and is not a bound for every possible configuration.
- RGB332/RGB24/XRGB8888 coalescing: forty requests still produce four publications
  and coalesce thirty-six intermediate preparations, preserving latest pixels.
- The final SD kernel, actual RV32 WM, app and async input client boot and exchange
  BWM1 messages under QEMU mock BOSIO; native stream endpoint checks pass. QEMU
  does not emulate BOSIO performance or actual HDMI.
- Image partition bounds, boot payload CRCs, ext4 fsck and installed-file byte
  readback all pass. Whole-image SHA256 is supplied with the artifact.

Reproduction: tools/build_fixed.sh generates an isolated fixed service from the
maintained state-machine sources and the fixed_math/fixed_geometry implementations.
Generated sources are build artifacts, not a separate manually edited code fork.
Then tools/package_fixed.py packages the new binaries and startup script with the
previously built publish-copy kernel. tools/test_publish_image.py --fixed runs the
focused RV32/QEMU integration check. All scripts operate on regular image files;
no board or physical SD is modified by this build.
