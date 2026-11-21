# BoAYO application protocol on the triangle WM

The new `triangle-boayo` service owns the triangle renderer directly. It does not run the old BoAYO WM, compositor, decorations, or application supervisor. Applications retain the persistent Unix socket, newline JSON, two-slot RGB24 shared surface, damage submission, and presentation acknowledgement calling convention. `boayo_app.cpp` uses the existing `native/src/ipc.hpp` Client unchanged.

The experimental `triangle-wm` executable remains available for reproducing the original six-stage measurements. Both executables require exclusive ownership of `/dev/bosio0`; run only one at a time.

## Build and validation

```sh
bash updates/triangle-wm/build.sh /var/tmp/triangle-boayo-host host
TRIANGLE_INPUT=0 python3 updates/triangle-wm/tests/test_boayo.py /var/tmp/triangle-boayo-host/triangle-boayo
python3 updates/triangle-wm/tests/test_input.py /var/tmp/triangle-boayo-host/triangle-boayo
bash updates/triangle-wm/build.sh /var/tmp/triangle-boayo-rv32 rv32
```

The same test can target a `sanitize` build. It compares incremental output tile hashes with full scene rebuilds and tests ownership, invalid request atomicity, stale sequences, shared-slot reuse, no-op updates, movement, angular resizing, overlap, disconnect cleanup, and output pose changes. `triangle-app` also exercises the unchanged C++ application Client against the service.

## Run

```sh
export BOAYO_SOCKET=/tmp/bosio-wm.sock
TRIANGLE_SPARSE_UPLOAD=0 triangle-boayo > /tmp/triangle-boayo.log 2>&1 &
triangle-app --demo
# Or run the finite benchmark instead of the demo:
triangle-app
```

The demo draws its own text and rectangles in application memory. The WM now supplies split close/move/resize controls and a pointer; the app does not draw these itself. See [INPUT.md](INPUT.md). The image has the same background color as its padded triangle silhouette. The demo now defaults to native RGB332 drawing and shared slots; `--rgb24` and `--rgb32` select comparison paths. Legacy applications still receive RGB24 when they omit the format.

For the current board session, the staged executables are `/tmp/triangle-boayo-v12` and `/tmp/triangle-app-v3`; substitute those paths in the commands above and below. The live service PID is recorded in `/tmp/triangle-wm.pid`, and the demo PID in `/tmp/triangle-app.pid`. These are temporary development deployments, not boot-time service installation.

The service uses `$BOAYO_SOCKET` (default `/tmp/bosio-wm.sock`) for applications and `$BOAYO_SOCKET.control` for internal management. Socket files are mode 0600; peer UIDs must match the WM UID. Window ownership follows the live connection, not the client-provided `app` string. Disconnecting an application removes its windows and releases their shared surfaces. Applications must keep their connection open.

## Application calls

Requests: `{"id":1,"app":"example","op":"create_window","args":{...}}`. Responses: `{"id":1,"ok":true,"result":{...}}` or `{"id":1,"ok":false,"error":"..."}`.

1. `create_window`: `title`, `surface_width`, `surface_height`, `azimuth`, `elevation`, `width_deg`, `height_deg`, optional `padding_deg` (default 2), `background_index` (B, default 84), `splash_index` (S, default 228), `mapped`. See [FEEDBACK.md](FEEDBACK.md) for staged startup and input feedback.
2. `create_surface` with `window_id` and optional `format` (`RGB24`, `XRGB8888`, or `RGB332`): returns `path`, `format`, `width`, `height`, `stride`, `slots: 2`, `slot_bytes`, and `sequence`. New surfaces default to RGB24; repeated calls without a format return the existing surface. A live format cannot change. Map the entire file read/write using the returned sizes.
3. Write the changed pixels into either slot, then `commit_surface` with `window_id`, `slot`, a strictly increasing positive `sequence`, and `damage: [[x,y,w,h], ...]` (1–128 in-bounds rectangles).
4. The commit response releases the slot after copying the declared damage into the WM-owned latest packed image. It returns `generation`, `changed`, and `copied_bytes`. Reusing the slot after this response cannot affect the submitted image.
5. `wait_present` with that `generation` waits for hardware publication at least as new as that generation; a newer accepted update may supersede an intermediate image. This is the latency endpoint used in the benchmark. Unknown future generations are rejected. An unchanged image consumes the sequence but keeps its generation.

Own-window `configure_window`, `destroy_window`, `raise_window`, and `focus_window` are also available. `get_state`, `sync_scene`, `app_events`, and `poll_events` provide state; event arrays are currently empty because application widget input routing is deferred. WM pointer actions use the trusted control socket. Old rounded `shape` and decoration hints are not used. Legacy full-image/base64 APIs, app launching, workspace management, application widget input dispatch, and roll are outside this implementation's compatibility scope.

## Internal management

The `--ctl` command connects to the internal socket and waits for the returned generation to be presented. Replace `1` with a live `window_id` from `get_state`.

```sh
triangle-boayo --ctl '{"op":"get_state"}'
triangle-boayo --ctl '{"op":"configure_window","args":{"window_id":1,"azimuth":8,"elevation":2}}'
triangle-boayo --ctl '{"op":"configure_window","args":{"window_id":1,"width_deg":24,"height_deg":16}}'
triangle-boayo --ctl '{"op":"raise_window","args":{"window_id":1}}'
triangle-boayo --ctl '{"op":"destroy_window","args":{"window_id":1}}'
triangle-boayo --ctl '{"op":"view_set","args":{"yaw":5,"pitch":2}}'
triangle-boayo --ctl '{"op":"pose_sweep","args":{"samples":61,"amplitude":8}}'
triangle-boayo --ctl '{"op":"shutdown"}'
```

Resizing changes the angular projection dimensions and preserves the source resolution and shared mapping. Apps that need a different pixel resolution must create a replacement window. Moving/resizing content rebuilds its projection descriptors, so it has a different cost from moving a solid background. Closing an externally managed window invalidates its ID; applications should handle its absence/errors and release their mapping. The supplied demo observes its window's disappearance and exits.

## Limits and timing

- Up to 64 windows and 64 peer connections; source dimensions 1–1024 in each direction. Initial reservations charge 13 bytes per pixel. Surface creation adjusts this to 7/13/16 bytes per pixel for RGB332/RGB24/XRGB8888 (two slots, one latest packed image, and private RGB32), capped at 64 MiB.
- Window yaw ±180°, pitch ±80°, angular width/height 1–100°, padding 0–10°. Output yaw ±180°, pitch ±85°, roll 0.
- Background palette indices: 84, 164, 109, 38, 228, 255. These are reserved before the first hardware publication.
- Queued damage is accumulated per window; overlapping rectangles merge, while disjoint rectangles remain separate up to 128 regions. Overflow falls back to a conservative bounding rectangle. Conversion reads the latest packed image once per accumulated region, and each affected tile is invalidated once per batch. See [CONTENT_COALESCING.md](CONTENT_COALESCING.md). v5 keeps multiple source textures in the existing DDR arena. Source uploads use full-width rows or a bounded sparse staging path. See [TEXTURE_CACHE.md](TEXTURE_CACHE.md) for cache replacement and upload measurements.
- A single service event loop owns rendering. Commit responses are flushed before rendering. Each publication drains up to four nonblocking I/O sweeps, with at most 32 requests total and four per peer per sweep; ready control peers run first and app peers rotate. There is no debounce sleep. New requests arriving during projection still wait for it to finish. With `TRIANGLE_INPUT=0`, output pose avoids scene recomposition. With controls enabled it republishes the screen-positioned overlay. Neither path preempts a running render. This is not a 60 FPS interaction guarantee.
- Benchmark `accept_ms` ends at the commit/configuration reply. `present_ms` includes the subsequent `wait_present` round trip. Neither includes HDMI/capture optical delay. Source image generation is outside content submission measurements; the app's source-to-memfd copy is inside them.

See [BOAYO_RESULTS.md](BOAYO_RESULTS.md) for the original baseline, [PIXEL_FORMATS.md](PIXEL_FORMATS.md) for format details and projection optimization, and [OPTIMIZATION_RESULTS.md](OPTIMIZATION_RESULTS.md) for the RGB332 comparison. The fixed-point CPU implementation is in [CPU_OPTIMIZATION.md](CPU_OPTIMIZATION.md), followed by [resident-texture and transform-cache measurements](TEXTURE_CACHE.md).

Completion-response and on-demand diagnostic changes are documented in [RESPONSE_PATH.md](RESPONSE_PATH.md). The original application call sequence remains unchanged.

Whole-tile background tip removal and its measured cost are documented in [OUTLINE.md](OUTLINE.md). Set `TRIANGLE_OUTLINE=raw` to compare the original silhouette with the same v7 binary.
