# SD4 Linux v2 — 실보드에서 발견한 저장소 연결 오류 수정

## 변경 이유

2026-11-10 SD4 v1 실보드 시험은 새 커널과 BusyBox 셸까지 성공했으며 SDHC 카드와 SD 인터럽트도 확인했다.
그러나 `mmcblk` 등록이 `-EINVAL`로 실패해 `/data`를 사용할 수 없었다.

1. v1 드라이버의 `max_blk_count=1`, `max_req_size=512`가 Linux 6.12의
   `blk_validate_limits()` 최소 한 페이지(4KiB) 조건에 어긋났다. 경고 PC `0xc016034c`를
   해당 커널의 `vmlinux`와 대조했다. `max_seg_size`도 한 페이지 이상이어야 한다.
2. 보존한 BusyBox는 `CONFIG_FEATURE_MOUNT_FLAGS`가 꺼져 있어 `noatime`을 VFS 플래그로
   변환하지 않고 ext4 옵션으로 전달했다. 실제 로그에 `ext4: Unknown parameter 'noatime'`이 남았다.

## 수정 내용

이 디렉터리는 `variants/sd4-dma/`의 보존된 v1 소스에 적용하는 **소프트웨어 수정본**이다.
원래 릴리스의 소스·로그·이미지 해시를 새 검증 결과처럼 덮어쓰지 않는다.

- Linux에는 최대 4KiB/8블록 요청과 4KiB 메모리 조각을 지원한다고 등록한다.
- 드라이버가 각 요청을 CMD17/CMD24의 512바이트 전송으로 나눈다. PHY의 CMD18/CMD25 지원을 주장하지 않는다.
- `sg_pcopy_*`의 offset으로 여러 scatterlist 조각을 순서대로 처리한다.
- SDHC는 블록 주소, SDSC는 바이트 주소를 증가시킨다.
- 쓰기는 기존 PHY의 DAT0 busy 완료 후 CMD13으로 카드 상태를 확인한다.
- 완료한 블록만 `bytes_xfered`에 합산한다. 오류 시 이후 블록을 전송하지 않으며,
  소프트웨어 전송 타임아웃 시 기존 호스트 격리와 DMA 버퍼 보존 정책을 유지한다.
- 실제 전송이 단일 블록이므로 상위의 CMD23/CMD12를 카드에 그대로 보내지 않는다.
- 마운트는 지원되는 `mount -t ext4 /dev/mmcblk0p2 /data`를 사용한다.

기존 비트스트림 `ORYUKDO-KFB-SD4-DMA-v1.bit`을 그대로 사용한다.
SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`.
RTL·MIG·HDMI·CPU·DMA 하드웨어와 OpenSBI·DT는 수정하지 않았다.
BusyBox와 BoAYO native-v2 실행 파일도 보존했다. SD 전송 속도 개선을 실측했다고 주장하지 않는다.

## 검증

| 검사 | 결과 |
|---|---|
| 실제 드라이버 request/IRQ 함수, ASan/UBSan | 49개 PASS |
| Linux 6.12 RV32 커널 컴파일·링크 | PASS |
| 부트 payload CRC·MBR·ext4 fsck | PASS |
| 새 이미지의 native SD RTL 로더·mock DDR 적재 및 런타임 전환 | PASS |
| 같은 payload의 QEMU 셸·산술·상위 메모리 16페이지 | PASS |
| BusyBox·BoAYO 등 기존 root 파일 | 일반 파일 35개 동일; init과 이미지 식별 파일만 변경 |
| v2 실보드 셸·SDHC·두 파티션·ext4 RW 마운트 | PASS |
| 실보드 256KiB 기록·fsync·정상 unmount/remount·모든 바이트 재비교 | PASS |
| 실제 전원 순환 후 보존 / 독립 DDR 복사·채우기 DMA | 미수행 |

[검증 요약](evidence/result.json), [드라이버 시험](evidence/driver_test.log),
[RTL 로더](evidence/actual_loader.log), [QEMU](evidence/qemu_result.json),
[v1 실보드 오류 기록](evidence/v1_board_result.json)을 보존했다.
[v2 실보드 결과](evidence/v2_board_result.json)와 [원본 UART](evidence/v2_board_uart.log)를 추가했다.
SD 인터럽트는 기록 전 104회 → 기록 후 1,642회 → 재읽기 후 2,458회였다.
커널 빌드에는 기존 진단용 `clocksource.c`의 `pr_fmt` 재정의 경고가 남아 있으며 이번 수정 범위에서 바꾸지 않았다.

`tests/run_driver.py`는 실제 드라이버의 request/IRQ 함수를 호스트에서 컴파일한다.
커널 API·MMIO·카드는 모델이며 Linux MMC 계층 자체를 실행하는 시험은 아니다.
49개 시험에서 1~8블록, SG 경계, SDHC/SDSC, 부분 완료, CRC/응답 오류,
격리, 쓰기 busy/상태 오류, 주소 넘침, SCR 읽기를 검사한다. ASan/UBSan을 사용한다.

새 이미지의 RTL 적재 검사는 기존 실제 SD 부트 로더/호스트/DMA RTL과 카드·DDR 모델을 사용한다.
CPU의 Linux 실행이나 실제 카드 검증을 대신하지 않는다.
QEMU는 같은 부팅 payload의 셸·상위 메모리를 확인하지만 native SD 장치 모델은 없어 `/data` 실패가 예상된다.
**v2 실보드 디스크 등록·ext4 읽기·쓰기는 확인했지만, 실제 전원 순환 후 보존은 아직 확인하지 않았다.**

## 최소 BusyBox에서의 검사 도구 보완

배포한 v2 이미지의 `storage-check` 스크립트는 BusyBox에 없는 `dd`·`sha256sum`을 요구했다.
또한 이 커널에는 `/proc/sys/vm/drop_caches`와 fadvise syscall이 없다. 이미지 자체는 같은 해시로 보존했다.
검사 도구의 실패를 파일 읽기·쓰기 성공으로 처리하지 않았다.

별도 [RV32 검사 도구](tools/storage_check.c)를 만들어 UART로 전송하고, 길이와 FNV-1a로 전송 일치를 확인했다.
첫 실행에서 생성·fsync한 256KiB 파일을 두 번째 실행에서 정상 unmount/remount 후 다시 읽어 전 바이트를 비교했다.
`V2_VERIFY_RC:0`과 `PASS SD ext4 write/read: bytes=262144 exact_byte_compare`를 확인했다.
새 도구는 현재 보드의 `/bin/storage-check`와 SD의 `/data/storage-check`에 설치했다.

현재 부팅에서는 `storage-check`를 사용할 수 있다. 다음 부팅에서는 SD에 보관한 도구를 RAM으로 복사해 실행한다.
이 도구는 검사 중 `/data`를 정상 해제·재마운트하므로 다른 프로그램이 `/data`를 사용 중이면 먼저 종료한다.
SD에서 도구 자체를 실행하면 unmount가 막히므로 다음과 같이 RAM에서 실행한다.

```sh
cp /data/storage-check /tmp/storage-check
/tmp/storage-check
```

`/data/persistence-check.txt`도 기록·sync했다. 전원을 끄기 전 정상 해제는 RAM에 있는 도구의
`/tmp/storage-check --unmount-data`를 사용한다. 기존 BusyBox에는 `umount` applet도 없다.
진단용 `--identity`는 `/tmp/storage-check --identity`처럼 실행 파일 경로를 명시한다.
이 보완 도구는 v2 이미지에 내장됐다는 뜻이 아니며, 새로 카드를 기록하면 별도로 설치해야 한다.

## 빌드

WSL/Linux에서 저장소 루트를 기준으로 실행한다.

```sh
python3 updates/sd4-linux-v2/tests/run_driver.py
BASE_BUILD=/var/tmp/oryukdo-sd4-linux \
ROOT_LIST_TOOL=/path/to/boayo/root_list.py \
bash updates/sd4-linux-v2/build.sh /var/tmp/oryukdo-sd4-linux-v2
```

`BASE_BUILD`에는 기존 v1의 `linux/`, `kernel/.config`, `root/`, `firmware/`가 필요하다.
`ROOT_LIST_TOOL`은 기존 BoAYO initramfs 목록 도구다. 독립 Linux 트리에 드라이버를 적용하며 기준 트리를 수정하지 않는다.
RISC-V Linux GCC, make, dtc, ext4 도구 및 호스트 GCC·Python이 필요하다.
산출물 이름은 `ORYUKDO-KFB-SD4-DMA-v2.img`, 커널 식별자는 `6.12.0-oryukdo-sd4-dma2`다.
이미지는 263MiB, `/data`는 256MiB이며 카드 전체 용량 자동 확장은 없다.
검증한 이미지 SHA-256: `6a6b92f300faeb80c89108d94d3411c28438b185abaaca8f71dac06216ca2424`.
이 PC의 기록용 파일은 `D:/temp/gen3bosio/sd4-dma-v2/ORYUKDO-KFB-SD4-DMA-v2.img`다.

## 보드 재확인

기존 카드 자료를 보관한 뒤 보드 전원을 끄고 v2 이미지를 기록·검증한다.
전원을 켜면 기존 SD4 v1 비트스트림을 다시 올린다. SPI 전용 과거 비트스트림은 사용하지 않는다.

UART에서 `mmcblk0: p1 p2`, `ORYUKDO: SD ext4 /data mounted`와 셸을 확인한 뒤:

```sh
cat /etc/oryukdo-image
uname -r
cat /proc/partitions
cat /proc/interrupts
cat /proc/mounts
# SD에 보관한 보완 도구가 있는 기존 시험 카드에서:
cp /data/storage-check /tmp/storage-check
/tmp/storage-check
ddrcheck
```

보완 도구는 256KiB 파일을 기록·fsync하고 정상 unmount/remount 후 모든 바이트를 비교한다.
SD 인터럽트 증가도 확인한다. DDR 복사·채우기 DMA의 별도 실보드 시험과는 구분한다.
영구 보존은 파일을 기록·sync·정상 unmount한 뒤 사용자 전원 순환 후 확인한다.
