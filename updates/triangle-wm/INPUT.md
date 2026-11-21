# Split WM controls and pointer (v11)

`triangle-boayo` now enables internal pointer controls by default. Applications keep the existing BoAYO socket/shared-surface interface. `TRIANGLE_INPUT=0` disables the overlay and preserves the previous rendering and fast pose path.

The original measurements are in [INPUT_RESULTS.md](INPUT_RESULTS.md); v9 CPU improvements and runtime visibility are in [INPUT_OPTIMIZATION.md](INPUT_OPTIMIZATION.md), followed by [button-mask and first-content optimization](INPUT_CACHE.md) and [latest-content coalescing](CONTENT_COALESCING.md).

## Placement and behavior

- Close: upper padding. Move: lower padding. Resize: lower-right padding.
- Placement follows the window's actual owned spherical tiles, position, size and output view. It does not impose a fixed polygon or add a frame. Fully occluded/offscreen handles are omitted.
- Icons are about 16 screen pixels; hit regions are 32 pixels and also check the visible tile owner. A mixed content/background tile may carry an icon, but application-covered samples are protected.
- Pointer display is independently switchable at runtime with **포인터 표시** or `input_visibility`. Hiding the dot retains coordinates, hover and capture; window operations still work. Hidden motion outside handles does not publish an unchanged frame. This is separate from disabling PC input transmission or setting `TRIANGLE_INPUT=0`.
- Pointer: approximately 5 screen pixels of white with a black surround; amber while pressed. The triangle sampling grid can change its apparent shape/size. This is a software overlay, not a hardware cursor.
- Dragging moves the pointer; releasing commits the window move or angular resize once. Resize preserves the opposite top-left angular corner and the application surface resolution.
- Close requires press and release on the same close handle. Releasing outside cancels. Capture survives leaving a handle. Escape/cancel, input disconnection, window destruction and 30 seconds without input release capture.

## Rendering

Base scene payloads remain clean. The 4,220-slot scene reserves 256 slots for overlays (leaving 3,958 content slots plus six shared solid colors). At most 256 reserved payload slots hold copies of the few tiles touched by controls/pointer. Masked RGB332 fills draw the overlay in the inactive bank, then its directory is published. The next pointer position starts from clean base tiles, including any newer application content. Each bank caches overlay masks and source revisions. A cached integer projective inverse bounds the few cells around each icon/dot. Cofactors are computed once per tile/view, normalized to 46 bits, and evaluated with integer dot products/divisions. A three-cell guard and conservative full-tile fallback cover quantization and mappings near a pole. When pointer/view/layout stay unchanged, the CPU reuses the complete paint masks while still refreshing changed underlying content. Unchanged application content is not reprojected just to move the pointer. Projected cell coordinates are now cached lazily per output view (4 KiB per visited tile). Integer floor, fractional-edge and rounding bits preserve the original sample coverage exactly. For visible coordinates, a normalized 32-bit division supplies an estimate that is corrected against the original 64-bit numerator and denominator; the small-denominator fallback retains exact division. A view change discards this cache and uses the original sparse calculation for its first publication, avoiding a cache warm-up penalty; subsequent motion uses the cache. Pointer-only publications reuse the existing scene ownership and content plan; any pending application or geometry mutation still forces normal scene planning.

The present ABI has no independent cursor plane. Pointer-only publication still pays for scene metadata validation/publication. With input enabled, output pose changes first issue the required `BO_SET_POSE` transaction, then rebuild the screen-positioned overlay and publish a scene; the earlier pose-only latency is not applicable. The two transactions are not atomic together, so an overlay can briefly shift before its recomposed frame arrives. Board measurements are reported separately from HDMI/capture optical delay.

## Input and live monitor

The current board kernel exposes no input device. The development input route is PC mouse → loopback HTTP → COM4 → persistent board control socket. It does not require changing the kernel, FPGA or SD boot installation.

```sh
TRIANGLE_SPARSE_UPLOAD=0 /tmp/triangle-boayo-v11 > /tmp/triangle-v11-live.log 2>&1 &
/tmp/triangle-app-v3 --rgb332 --demo > /tmp/triangle-app-input-v8.log 2>&1 &
# Finite board-local test (optional --close actually closes the first window):
/tmp/triangle-input-v8 --close
```

On the PC, keep `tools/monitor.py` running on port 8766, then start:

```powershell
python updates/triangle-wm/tools/pointer_bridge.py --output updates/triangle-wm/evidence
```

Open <http://127.0.0.1:8767/> and enable pointer input. The separate **포인터 표시** checkbox controls only the rendered dot and reads its current setting from the board when the page loads. The displayed image is actual HDMI capture; the browser does not draw a substitute pointer. Motion requests are coalesced while preserving button edges. The page shows the board-local request-to-presentation latency, excluding browser/UART/capture transport time.

The bridge exclusively owns COM4. Use **입력 연결 종료** before running other UART tools. It sends `quit` to the board input process and restores shell echo. After an unexpected PC process failure, reconnect to COM4 and send a literal `quit\n`, waiting for `INPUT_EXIT`, before issuing shell commands. The WM's idle lease prevents a stuck button while disconnected.

## Restoring after a reboot

The v11 service is retained in `/root/triangle-wm-v11/`; the unchanged app/input executables remain in `/root/triangle-wm-v8/` on the board's persistent filesystem. This does not install a boot service. Restore the temporary launch paths, then use the run commands above:

```sh
cp /root/triangle-wm-v11/triangle-boayo-v11 /tmp/triangle-boayo-v11
cp /root/triangle-wm-v8/triangle-app-v3 /tmp/triangle-app-v3
cp /root/triangle-wm-v8/triangle-input-v8 /tmp/triangle-input-v8
```

Copying to `/tmp` avoids cold SD-backed executable page faults during measured interactions. The copy and executable startup are outside the application request-to-publication measurements.

Keep one service and one UART owner at a time. After `poweroff`, the physical board must be booted before reconnecting. Restart the PC capture monitor if its process ended.

## Control API

Only the trusted `.control` socket accepts these operations:

| Operation | Arguments | Behavior |
|---|---|---|
| `input_status` | none | Coordinates, effective visibility, `display_enabled`, capture, handles and tile IDs |
| `input_visibility` | boolean `visible` | Persistently toggle dot rendering; preserve input tracking and capture |
| `input_move` | integer `x` 0–1279, `y` 0–719 | Move/show pointer |
| `input_button` | `x`, `y`, boolean `pressed` | Capture or release/commit |
| `input_cancel` | optional `hide` | Cancel capture; optionally hide pointer |

Visual mutations return a generation; `wait_present` acknowledges completion. Motion with the dot hidden and no handle hover change updates coordinates immediately and reuses the existing visual generation, without an unnecessary frame publication. One input connection owns an active capture. Invalid coordinates are rejected before changing state. Application widget input events remain outside this version; `app_events`/`poll_events` remain empty.

## Validation

```sh
TEST_INPUT_VISIBILITY=1 python3 updates/triangle-wm/tests/test_input.py /var/tmp/triangle-v11-host/triangle-boayo
TEST_INPUT_VISIBILITY=1 python3 updates/triangle-wm/tests/test_input.py /var/tmp/triangle-v11-san/triangle-boayo
TRIANGLE_INPUT=0 python3 updates/triangle-wm/tests/test_boayo.py /var/tmp/triangle-v11-san/triangle-boayo
```

The mock enforces the driver rule that a changed camera requires `BO_SET_POSE` before `BO_PUBLISH`. The original bounded-raster check uses `TRIANGLE_UI_FULL_SCAN=1`. For v10/v11, a trace comparison against v9 checks identical tile hashes across 160 presented states, including tile-edge round trips, queued geometry followed by pointer motion, first partial RGB332 content, and cached button states with the dot hidden and shown. The optional visibility checks also cover hidden tracking without redundant publication, capture retention and restoring the original pixels. `check_ui_samples.cpp` checks one million random quotients plus integer/normalization boundaries against exact 64-bit division.

The input test uses real Unix sockets/shared RGB332 buffers with a mock hardware backend: invalid input atomicity, control ownership, cursor trail restoration, content updates beneath the cursor, overlap restoration, canceled close, move/resize on release, disconnect, view changes, 36 size/position layouts, and final close. The compatibility suite covers all three surface formats with the prior UI-disabled pose contract. ASan/UBSan builds run both suites.

The monitor tolerates a transient Windows snapshot-file lock, and the input page displays capture frame age to make a frozen capture visible.

For explicit live browser acceptance (moves a real window and stops the bridge):

```powershell
python updates/triangle-wm/tests/test_pointer_page.py --url http://127.0.0.1:8767 --output updates/triangle-wm/results/content-v11-browser.json
```
