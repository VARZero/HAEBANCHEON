# 개발 SD 이미지 검증 기록

## 보시오·보아요 통합 이미지 — 2026-11-18

통합 배포 파일 `HAEBANCHEON-BOSIO-BoAYO-SD.img.xz`는 아래 초기 개발 이미지에 최신 `bosioctl`, `bosio-tiles`, `boayo`와 BOSIO 앱용 헤더를 넣어 만들었다. MBR부터 첫 파티션까지 바이트가 동일하며, ext4 검사와 압축 해제 전후 원본 이미지 일치를 확인했다. 소스에서 다시 빌드하려면 [개발 이미지 빌드 절차](README.md#빌드)를 따른다.

| 배포 파일 | 크기 | SHA-256 |
|---|---:|---|
| `HAEBANCHEON-BOSIO-BoAYO-SD.img` | 1,081,081,856 bytes | `ec4fefb451a080a5edc41708b78b7b2d4d8226b25e9cb3e562825c6f0dd8f93d` |
| `HAEBANCHEON-BOSIO-BoAYO-SD.img.xz` | 41,712,788 bytes | `ac6600f073ba08e2d6543f3767d921840c7409668b3c1e1ee169b8a32f43d220` |

- 배포 이미지의 QEMU 시험에서 BusyBox 셸, Vim, Python 3, GCC/G++, C·Python 앱, 보시오 모의 실행, 보아요 런처와 SDK Pulse 실행·종료, 두 번째 부팅의 데이터 보존과 복구 셸을 확인했다. [QEMU 결과](validation/qemu-bosio-boayo-release.json)
- 호스트 모의 환경에서 보아요 시작·재시작, Dashboard·Telemetry·SDK Pulse 창, 보아요를 종료한 뒤 다른 앱 창이 유지되는 것을 확인했다. 동일 데이터 재전송은 무갱신이었고, 2D 픽셀 두 개를 바꾼 시험은 8바이트를 전송하고 전체 732개 중 6개 투영 작업을 실행했다. 부분 갱신 결과는 전체 렌더링과 일치했다.
- Verilator의 투영 스크래치 시험은 코어 0, 코어 1, 동시 투영을 검증했다. 기존 출력 통합 시험의 DDR 읽기 횟수 검사 실패는 변경 전 코드에서도 재현됐다.

통합 이미지의 QEMU 시험은 보시오 모의 백엔드를 사용한다. FPGA에서의 SD·DMA 동작, HDMI 출력과 프레임 성능은 확인하지 않았다.

## 초기 개발 이미지 — 2026-11-17

저장소 기준 커밋 `c526c63`에서 시작했다. Ubuntu 24.04 / WSL2
`6.6.87.2-microsoft-standard-WSL2`, 호스트 GCC 13.3.0, Python 3.12.3,
QEMU 8.2.2에서 수행했다. 새 출력 디렉터리에서 툴체인·사용자 공간·커널을 빌드했다.
기존 다운로드 캐시의 아카이브만 해시를 검증해 재사용했으며, 이전 빌드의 툴체인·rootfs는 복사하지 않았다.
OpenSBI는 의도적으로 저장소의 고정 바이너리를 재사용했다.

## 통과한 검사

- Buildroot 2025.02.19 고정 아카이브와 Linux 6.12 소스 복원·패치 해시 검사.
- GCC 13.4.0 교차 컴파일러와 RV32 네이티브 GCC/G++, musl 1.2.6, binutils 2.43.1 빌드.
- BusyBox 1.37.0, Vim 9.2.0943, Python 3.12.15, make를 포함한 ext4 rootfs 생성.
- 정적 BusyBox 복구 initramfs와 Linux Image 빌드. 해석된 Kconfig 필수 옵션 검사.
- 타깃 **141개 ELF**의 ELF32/RISC-V/soft-float 및 RVC·RVE 플래그 검사.
- OpenSBI 해시와 보드 DTS 바이트 보존, ORYSD001 CRC, MBR/적재 범위, ext4 크기 및 `e2fsck -fn` 검사.
- 호스트 단위 검사 **8개**: 유효 이미지·ELF, CRC 손상, 파티션 겹침, 커널/DTB 겹침 한도, 잘린 rootfs, RV64·RVC·float/RVE/TSO 설정 거부.
- 셸 문법 검사, Python 문법 검사, `git diff --check`.
- `legal-info` 패키지 소스·라이선스 수집. Buildroot 자체 아카이브는 `downloads/buildroot-2025.02.19.tar.xz`에 별도로 보존된다.

QEMU에서 SD 이미지의 OpenSBI와 Image를 추출해 **C/F/D/V를 끈 RV32**로 실행했다.
시험 전용 DT에서 virtio disk와 10MHz timer를 사용하며, 보드와 같은 1GiB RAM 및
24MiB의 BOSIO 예약 영역을 유지했다. 실제 SD 이미지의 보드 DT는 변경하지 않았다.

- OpenSBI → Linux → ext4 `/` → BusyBox 셸.
- `/tmp`, `/run` tmpfs 및 `/dev/pts` 마운트.
- Vim의 실제 파일 치환·저장, Python의 ctypes/json/math/sqlite3/zlib import 및 연산.
- 네이티브 GCC로 C 프로그램 **동적 링크와 정적 링크** 후 실행.
- 네이티브 G++로 iostream/vector 프로그램 빌드·실행.
- `app-install`을 통한 실행 파일 설치·실행·정리.
- 새 교차 컴파일러로 만든 `hello`, `APP_OVERLAY`로 포함한 `hello-python` 실행.
- 쓰기·`sync` 후 QEMU 프로세스를 재시작해 `/data` 파일 보존 확인.
- 디스크 없는 부팅에서 복구 셸 명령 실행 확인.

최종 결과: [qemu-result.json](validation/qemu-result.json).
로그: [첫 부팅](validation/uart-0.log), [두 번째 부팅](validation/uart-1.log),
[디스크 없는 복구](validation/uart-recovery.log).

시험 중 발견한 C++ 헤더 누락과 init의 tmpfs 마운트 누락을 수정한 뒤 위 전체 시험을 다시 통과했다.
커널 설정에서 TMPFS가 SHMEM에 의존해 빠지는 문제도 해결하고 Kconfig 검사를 추가했다.

## 측정한 결과

| 항목 | 값 |
|---|---:|
| 전체 이미지 | 1,081,081,856 bytes (1,031MiB) |
| rootfs 파티션 | 1GiB |
| 커널 Image | 약 5MiB, 기존 123MiB 적재 한도 이내 |
| rootfs 파일 사용량 (`df`) | 178.6MiB |
| rootfs 가용 공간 (`df`, 예약 블록 제외) | 728.9MiB |
| QEMU 셸 부팅 직후 MemTotal / MemAvailable | 983MiB / 약 970MiB |

전체 이미지 SHA256:

```text
a516e8a9acf66439a158602d78e1cac4d3cd692dbe99d9196bfc261e83d03ffa
```

이 이미지는 문서의 예제 두 개를 `APP_OVERLAY`로 포함한 시험 결과다.
세부 바이너리 해시는 [build-summary.json](validation/build-summary.json)에 있다.
QEMU의 메모리·속도는 실보드 측정값이 아니다. 독립적인 두 번의 완전 소스 빌드로
bit-for-bit 동일성을 비교하지는 않았다. SDK tarball 내보내기도 별도 실행하지 않았다.

## 실보드에서 남은 검증

실제 FPGA에 연결하거나 SD를 기록하지 않았다. RTL·비트스트림·기존 부트 이미지 생성기·
보드 드라이버·DTS 파일을 수정하지 않았고 합성·배선을 다시 실행하지 않았다.
실보드에서 SD4 rootfs의 페이지 읽기/쓰기·인터럽트·DMA/캐시 일치, MIG,
BOSIO/HDMI와 기존 BoAYO와의 통합, 콜드부트 시간, 큰 앱 컴파일·메모리 압박을 확인해야 한다.

로컬에 있던 기존 L1-16K DMA3 커널 소스와 재구성한 소스의 공통 파일 86,633개를 비교했다.
차이는 `init/main.c`, `init/initramfs.c`의 기존 추가 부팅 진행 로그뿐이었다.
이 관찰은 새로운 실보드 동작 검증이나 이전 커널 바이너리와의 동일성을 의미하지 않는다.
