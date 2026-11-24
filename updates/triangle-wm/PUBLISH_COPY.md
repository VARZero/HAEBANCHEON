# Publication stages and copy-only driver experiment

Measured on the actual board, 2026-11-22. No WM or app was running. The same
single-tile scene, fixed pose, alternate inactive banks and full 4,476-word
metadata snapshot were used. Each case has two warmups and twenty timed samples.
Times below are independent medians, so their sum need not equal total median.

| Existing BO_PUBLISH stage | Median ms |
| --- | ---: |
| User snapshot acquisition (allocation/copy included) | 8.691 |
| Bounds and directory validation | 36.931 |
| Metadata comparison and CPU bank-snapshot storage | 49.066 |
| Configuration | 0.087 |
| Full-load/patch selection | 0.134 |
| Submit and output acknowledgement wait | 20.617 |
| Final CPU metadata/camera bookkeeping | 4.120 |
| Entire userspace ioctl interval | **121.884** |

These are warm, unchanged-per-bank metadata samples: the driver uploaded zero
words in the comparison/upload phase. Its 49.066 ms therefore reflects comparison
and snapshot storage, not a 49 ms DMA transfer. The existing profiler combines
these operations; it does not establish their individual times. Initial metadata
upload/cold configuration is excluded by warmups. One palette-word change on each
bank visit produced a total median of 122.007 ms.

The existing alternate API was also measured: BO_UPLOAD copies the whole
metadata, then BO_PRESENT performs checked publication. For unchanged metadata,
full copy took **24.963 ms**, the remaining BO_PRESENT took **65.536 ms**, and total
median was **93.673 ms**. With changing metadata the total was 93.786 ms. This
removes the CPU bank comparison but **still validates the directory and saves
completion snapshots**. It is not the final copy-only implementation. BO_PRESENT
does not return an internal phase profile, so those fields are unavailable, not
zero. All eighty timed requests returned the expected active bank with no poison.
Raw samples: `results/publish-stages-before.json`.

## New driver API

`BO_PUBLISH_COPY` (0x4f, same bo_publish structure) is an explicit trusted-scene
experiment, advertised by BO_DMA_PUBLISH_COPY and gated by CAP_SYS_RAWIO. It:

1. Acquires an immutable CPU copy of the caller's metadata.
2. Checks scalar parameters, inactive bank and stable pose/resolution.
3. Uploads all 4,476 metadata words in one operation, without a directory scan,
   dirty comparison or CPU bank-snapshot copy.
4. Always requests a full metadata load, then waits for output acknowledgement.
5. Updates scalar/camera state while leaving metadata caches invalid.

The skipped directory checks make metadata validity the privileged caller's
responsibility. Existing checked BO_PUBLISH remains available. It was the WM
default for this original experiment; the current fixed-point release selects
BO_PUBLISH_COPY by default and `TRIANGLE_PUBLISH_COPY=0` restores checked publication.
Mixing APIs is supported: after a trusted copy, checked publication must
rebuild the invalidated bank snapshot. Transfer errors and output timeouts still
poison the device; copying or command acceptance alone is never reported as screen
completion. The small parameter/camera checks and user-memory copy remain in this
path. This experiment removes the large per-tile metadata scans, not all driver
work or completion synchronization.

The existing engine regression and a new ASan/UBSan publication test passed,
including exact copied metadata, forced full load, stale-cache invalidation,
checked/trusted mixing, invalid scalar/camera/active-bank requests, transfer failure
and output timeout. The built-in RV32 kernel compiles successfully. Native tools
are installed in the image as `/usr/bin/publish-bench` and `primitive-bench`.

## SD image and next board measurement

The new image is based on the reproducible userland rootfs, with the modified
kernel and the two benchmark tools. It does not clone files created on the current
SD card after that base image was built. RTL and the board DT remain unchanged.
Kernel release: `6.12.0-haebancheon-publishcopy`.

After flashing and booting, run with no other BOSIO process:

```
uname -r
publish-bench --copy > /tmp/publish-after.log
cat /tmp/publish-after.log
```

This runs checked publish, full upload plus checked present, and the new trusted
copy path, each with unchanged/changing metadata and twenty samples. The actual
copy-only performance is **pending the new SD boot**; no estimate is presented as
a hardware measurement. The image manifest marks hardware_tested=false. QEMU boot
checks validate Linux/userland only; QEMU does not emulate BOSIO/output timing.

Delivered image: `D:/temp/haebancheon-publish-copy/HAEBANCHEON-publish-copy.img`

Bytes: 1081081856

SHA256: `7e984ba0628537fd4578af6c96125b0cdfe24abd2cb9b777a48f127420a88c98`

Final delivered-image focused QEMU boot passed: release, executable tools and new
UAPI header verified. See `results/publish-copy-boot.json`. The wider inherited
mock-app smoke test was interrupted while waiting for app/socket shutdown; no
claim is made that that full suite passed.
