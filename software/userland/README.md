# HAEBANCHEON Linux 개발 이미지

현재 `variants/l1-16k-dma3-lru-ram` 하드웨어에 사용할 **별도 개발용 SD 이미지**를 만든다.
BusyBox, Vim(런타임 포함), Python 3, 네이티브 GCC/G++와 binutils, make, C/C++ 헤더·라이브러리를 포함한다.
Linux 소스와 툴체인을 처음부터 준비하며 과거 `/var/tmp/oryukdo-*` 작업 폴더가 필요하지 않다.
BoAYO v14 이미지와 기존 빌드 스크립트는 그대로 보존한다. 이 개발 이미지는 BoAYO 앱·에셋을 포함하거나 자동 실행하지 않는다.

## 부팅 경로와 보존 범위

```text
기존 FPGA SD loader (ORYSD001, LBA 2048)
  → 기존 OpenSBI fw_jump.bin @ 0x80000000
  → Linux 6.12 + 작은 복구 initramfs @ 0x80400000
      DTB @ 0x87f00000
  → /dev/mmcblk0p2 (ext4) 마운트 → switch_root → BusyBox init
  → 직렬 콘솔 셸 → Vim / Python / GCC / 사용자 앱
```

- RTL, FPGA 초기화, 클럭, 캐시, SD loader, 주소·인터럽트, DDR 예약 영역은 수정하지 않는다.
- `sources.json`의 SHA256으로 저장소의 OpenSBI 바이너리를 확인한다. 이것은 **기존 바이너리 재사용**이며 OpenSBI 재빌드를 주장하지 않는다. v1.8.1 소스 커밋·라이선스는 `../../provenance/firmware_origin.json`에 있다.
- 현재 구성의 `gen3-bosio.dts`, SD·DMA·BOSIO 드라이버와 storage 커널 설정을 사용한다. 커널은 고정 Linux 6.12 아카이브 + 저장소의 trace-v2 패치를 복원해 만든다.
- 파일 잠금, tmpfs, 스크립트 실행, PTY와 ext4를 켠다. QEMU 시험용 virtio-mmio/블록 드라이버도 넣지만 보드 DT에 virtio 장치를 추가하지 않는다.
- SD가 없거나 rootfs를 마운트하지 못하면 20초 후 정적 BusyBox 복구 셸로 들어간다. 자동 포맷은 하지 않는다. DT의 bootargs에 `haebancheon.recovery=1`을 넣으면 강제로 복구 셸을 사용할 수 있다.
- 이 이미지의 두 번째 파티션은 `/`다. `/data`는 그 안의 영구 디렉터리다. 이전 이미지의 독립 `/data` 파티션과 용도가 다르므로 **새 SD 이미지 생성 전에 기존 데이터를 백업**한다. 제공 스크립트는 이미지 파일만 만들며 물리 디스크에는 쓰지 않는다.

## 빌드

Ubuntu 24.04 또는 해당 WSL2, Python 3.12 이상, Linux 파일시스템의 빌드 경로를 사용한다.
출력 경로와 저장소 경로에 공백을 넣지 않는다. WSL에서는 출력 경로를 `/mnt/c`나 `/mnt/d` 아래에 두지 않는다.

```sh
sudo apt-get update
sudo apt-get install build-essential git wget curl ca-certificates \
  bc bison flex patch rsync cpio unzip file perl python3 \
  libncurses-dev libssl-dev device-tree-compiler e2fsprogs \
  qemu-system-misc

git clone https://github.com/VARZero/HAEBANCHEON.git
cd HAEBANCHEON
# 이 문서/구현이 있는 브랜치 또는 커밋을 사용한다.
OUT=/var/tmp/haebancheon-userland
set -o pipefail
JOBS=6 bash software/userland/build.sh "$OUT" all 2>&1 | tee build-userland.log
```

위의 `set -o pipefail`은 로그 저장 중에도 빌드 실패 상태를 유지한다.
Buildroot **2025.02.19** 배포 아카이브는 SHA256으로 고정된다. 이 릴리스의 패키지 버전과 해시가 BusyBox/Vim/Python 및 GCC 13.4.0·musl·binutils를 결정한다.
RV32IMA + Zicsr/Zifencei, **ILP32 soft-float**를 사용하며 C/F/D/V 확장을 끈다. 일반 RV64 또는 RV32GC/ILP32D 바이너리는 이 보드에 적합하지 않다.

```sh
# rootfs/툴체인만, 전체 빌드, 라이선스/소스 수집, 재배포 가능한 교차 SDK
bash software/userland/build.sh "$OUT" rootfs
bash software/userland/build.sh "$OUT" all
bash software/userland/build.sh "$OUT" legal-info
bash software/userland/build.sh "$OUT" sdk
```

`DL_DIR=/absolute/download-cache`로 다운로드를 공유할 수 있다. 캐시를 사용해도 upstream 해시 검사를 수행한다.
호스트 패키지 설치·소스 다운로드는 인터넷이 필요하다. 미리 `make -C "$OUT/buildroot" source`를 수행하고 다운로드 캐시를 보관하면 재빌드에 활용할 수 있다.
빌드가 중단되면 같은 명령으로 이어갈 수 있다. 툴체인/ABI/패키지 선택을 바꾸거나 앱 오버레이에서 파일을 삭제했다면 **새 OUT**에서 빌드한다. Buildroot는 패키지 제거와 이전 오버레이 파일 삭제를 자동으로 정리하지 않는다.

주요 결과물:

| 경로 (`$OUT` 기준) | 내용 |
|---|---|
| `images/HAEBANCHEON-userland.img` | ORYSD001 raw boot + ext4 root, SD 기록 대상 |
| `images/manifest.json` | 이미지·펌웨어·설정·소스 해시, 파티션·적재 정보 |
| `images/buildroot.config`, `images/linux.config` | 실제 해석된 설정 |
| `buildroot/images/rootfs.ext4` | 루트 파티션 단독 이미지 |
| `buildroot/images/elf-audit.json` | ELF32 RISC-V·soft-float·RVC 플래그 검사 |
| `buildroot/host/bin/riscv32-buildroot-linux-musl-*` | 호스트 교차 컴파일 도구 |
| `buildroot/legal-info/` | `legal-info` 실행 시 패키지 라이선스·소스 |

버전·해시·환경 시각을 고정하고 실제 입력을 기록한다. 서로 다른 호스트에서의 완전한 bit-for-bit 동일성까지 보장하지 않는다. ext4 생성 도구와 소스 경로 등도 결과에 영향을 줄 수 있으므로 비교할 때 같은 호스트 패키지 버전과 경로를 사용하고 manifest를 비교한다.

### 네이티브 GCC가 별도 패키지인 이유

[Buildroot는 타깃 네이티브 컴파일러를 기본 지원하지 않는다](https://buildroot.org/downloads/manual/manual.html#faq-no-compiler-on-target).
`package/native-gcc`는 고정된 교차 GCC와 같은 소스·패치를 사용해 보드에서 실행되는 GCC/G++를 만든다.
Buildroot의 최종 정리가 개발 파일을 제거하므로 `post-build.sh`가 **타깃 sysroot**의 헤더, 시작 오브젝트, 정적 라이브러리와 툴체인의 타깃 C++ 헤더를 복원한다. 호스트 x86 라이브러리를 복사하지 않는다.
LTO, 플러그인, OpenMP, sanitizers는 이 네이티브 GCC 프로필에서 제공하지 않는다. 일반 C/C++, 동적·정적 링크를 위한 개발 환경이다.

## 자체 앱 빌드·포함·실행

호스트에서 예제 C/Python 앱을 별도 오버레이로 설치하고 이미지에 포함한다.

```sh
OUT=/var/tmp/haebancheon-userland
APP=/var/tmp/haebancheon-app-overlay
make -C software/userland/examples \
  CROSS_COMPILE="$OUT/buildroot/host/bin/riscv32-buildroot-linux-musl-" \
  OUTPUT=/var/tmp/haebancheon-hello DESTDIR="$APP" install
APP_OVERLAY="$APP" bash software/userland/build.sh "$OUT" all
```

보드 콘솔에서 `hello`와 `hello-python`을 실행한다. `APP_OVERLAY`는 rootfs에 마지막으로 복사되므로 기본 파일을 덮어쓸 수도 있다. 자신의 앱은 `/data/apps`, 소스는 `/data/src`에 둔다. 재빌드할 때마다 같은 `APP_OVERLAY`를 지정한다.
자체 네이티브 앱은 위 교차 컴파일러와 동일 sysroot로 빌드한다. 추가 공유 라이브러리가 필요하면 Buildroot 패키지로 포함하고 새 OUT에서 재빌드한다.

보드에서 직접 개발할 때:

```sh
mkdir -p /data/src
cd /data/src
vim hello.c
gcc -O2 hello.c -o hello
app-install ./hello my-app
my-app
python3 /data/src/my_app.py
# C++: g++ -O2 app.cc -o app
```

`app-install FILE [NAME]`은 신뢰하는 실행 파일/실행 가능한 스크립트를 `/data/apps`에 원자적으로 설치한다. 자동 실행하지 않는다.
스크립트에는 `#!/bin/sh` 또는 `#!/usr/bin/env python3`을 붙인다. PATH에 `/data/apps`가 들어 있다.
앱이 오래 실행되어 콘솔로 돌아오지 않으면 Ctrl-C로 중단한다. 자동 실행 서비스는 앱별 시작·종료 정책을 정한 뒤 `/etc/init.d`에 추가한다.

실보드에는 네트워크 연결을 가정하지 않는다. 기존 카드에 앱만 옮기려면 보드를 정상 종료한 뒤 Linux 호스트에서 **그 카드의 두 번째 ext4 파티션**을 마운트해 `/data/src`로 복사하고 `sync`·마운트 해제한다. 디스크 이름을 추측해 기록하지 않는다. Windows에서는 ext4 파일을 직접 편집할 수 없으므로 Linux/WSL에 장치를 명시적으로 연결하거나 새 이미지에 오버레이를 포함한다.

개발 이미지의 직렬 콘솔은 암호 없이 root 셸을 제공한다. 네트워크 로그인 서비스는 설정하지 않는다. Python은 선택한 표준 모듈을 포함하지만 pip, TLS/SSL, 모든 PyPI 패키지를 제공하는 일반 배포판은 아니다. 필요한 모듈은 Buildroot 설정/패키지로 명시한다.

## 메모리·저장장치 한계

| 항목 | 한계/권장 |
|---|---|
| 물리 DDR | 1GiB, 그중 BOSIO arena 16MiB + framebuffer 8MiB 및 OpenSBI/커널 예약이 있다. 1GiB 전체가 앱 메모리는 아니다. |
| 커널 적재 범위 | `0x80400000`부터 DTB `0x87f00000` 전까지 **123MiB (251,904 sectors)**. 이미지 생성기가 초과를 거부한다. |
| rootfs | 기본 ext4 **1GiB**. 전체 SD 이미지는 부트 영역만큼 더 크므로 최소 2GB 카드, 작업 데이터가 많으면 4GB 이상을 권장한다. 이미지를 큰 카드에 기록해도 파티션은 자동 확대되지 않는다. |
| 임시 공간 | `/tmp` 64MiB, `/run` 16MiB, `/dev/shm` 32MiB tmpfs 상한. GCC 임시 파일은 `TMPDIR=/data/tmp`로 SD에 둔다. |
| 컴파일 | CPU 50MHz이므로 GCC·링크·Python 시작이 느릴 수 있다. 큰 C++ 파일은 수백 MiB 이상을 사용할 수 있다. 보드에서는 `make -j1`, 큰 앱은 호스트 교차 빌드 권장. |
| 호스트 빌드 | 권장 8~16GiB RAM, 여유 공간 30GiB 이상. 첫 소스 빌드는 수십 분 이상 걸릴 수 있다. `JOBS`를 줄이면 동시 메모리 사용이 감소한다. |

SD 루트는 RAM으로 전체 복사되지 않는다. 사용 중인 코드·데이터·페이지 캐시가 메모리를 사용한다.
기본 스왑은 설정하지 않는다. SD 스왑은 지연과 쓰기 수명을 악화시킬 수 있다. `/proc/meminfo`, `free -m`, `df -h / /tmp /data`로 실측한다.
ext4 저널이 있어도 실행 중 전원을 끄면 최근 데이터가 손실될 수 있으므로 `sync` 후 정상 종료한다.

## 검증

```sh
# 호스트 검사 (툴체인·보드 없이 가능)
python3 -m unittest discover -s software/userland/tests -p 'test_*.py' -v
bash -n software/userland/build.sh
sh -n software/userland/bootstrap/init software/userland/post-build.sh

# 새 이미지에서 사용자 공간 동작/재부팅/복구 경로 시험
python3 software/userland/tests/qemu_smoke.py \
  "$OUT/images/HAEBANCHEON-userland.img" "$OUT/qemu"

# 실제 보드의 직렬 콘솔에서 실행
userland-check
free -m
df -h / /tmp /data
echo persistent > /data/board-test
sync
# 정상 종료·재부팅 후 cat /data/board-test
```

`userland-check`는 Vim 실제 파일 편집, Python 표준 모듈, GCC 동적·정적 C 프로그램, G++ 프로그램, 앱 설치·실행을 검증한다.
QEMU는 이미지에서 추출한 **동일 OpenSBI와 커널**을 C/F/D/V 확장을 끄고 실행하지만 QEMU용 DT(10MHz timer, virtio disk)를 사용한다. 실제 보드 DT는 이미지 안에서 그대로 보존된다. 시험 복사본에서만 쓰며 원본 SD 이미지는 변경하지 않는다.
파티션 없는 경우의 복구 셸과 두 번 부팅 사이의 `/data` 보존도 검사한다. UART 로그·시험 DT·`result.json`을 출력 폴더에 남긴다.

**QEMU 통과는 보드 검증이 아니다.** 물리 SD4/인터럽트/DMA·캐시 일치, MIG, ORYUKDO CPU, BOSIO/HDMI, 콜드부트 시간과 대규모 컴파일 성능은 해당 비트스트림과 실제 보드에서 별도로 확인해야 한다. RTL을 수정하지 않았으므로 이번 작업에서 합성·배선을 다시 수행할 필요는 없지만, 새 사용자 공간의 실보드 실행 가능성을 그것만으로 보장하지 않는다.

실제 수행한 검증과 측정은 [VALIDATION.md](VALIDATION.md)에 기록한다. Linux 6.12와 OpenSBI를 보존한 개발 구성으로, 커널 보안 업데이트까지 수행한 배포판은 아니다.

## 관련 원본

- [Buildroot 외부 트리·후처리 문서](https://buildroot.org/downloads/manual/manual.html#outside-br-custom)
- [GCC build/host/target 및 configure 옵션](https://gcc.gnu.org/install/configure.html)
- 기존 보드 이미지 형식: `../../variants/l1-16k-dma3-lru-ram/soc/board/make_sd.py`
- 기존 storage integration: `../../variants/l1-16k-dma3-lru-ram/software/storage/`
- 기존 소스 복원: `../../scripts/prepare_linux.py`

바이너리를 배포할 때는 `legal-info` 결과와 해당 소스·패치·빌드 지침을 함께 보관하고 각 라이선스 의무를 확인한다. 저장소의 Linux/BusyBox/OpenSBI 라이선스도 계속 적용된다.
