> **현재 진단 후보:** [launchtrace-v1 안내](../../../README_KO.md)를 우선한다. 아래 DMA/v2 설명은 상속한 기준 설명이다.

> **이번 변경:** 화면 bank 동기화와 작업 버퍼 초기화에 DMA를 연결했다. [현재 후보 안내](../../../README_KO.md)를 우선하며, 아래 v2 문서와 NEXT_SESSION은 상속한 과거 기준이다.

> v2 최적화 구조와 상태 조회 의미: [V2_OPTIMIZATION.md](V2_OPTIMIZATION.md). 실제 보드 시험은 사용자가 나중에 전원을 켠 뒤 진행합니다.

# BoAYO C++ 실행 환경 — PZ-A7200T-KFB

기존 BusyBox와 BOSIO ABI 3 드라이버를 유지하면서, 보드용 Python BoAYO의 창 관리·런처·내장 앱·UART 입력·투영 및 출력 연결을 C++17로 이식한다. 비교 기준은 `reference.sha256.json`에 고정한 기존 진단 SD의 Python 소스다. 원본 Python 이미지와 소스는 별도로 보존하며 새 SD에는 Python·NumPy·Pillow를 넣지 않는다.

## 구성

```text
UART WASD / Unix 소켓 JSON 클라이언트
                 ↓
C++ WM + 런처 + Dashboard / Telemetry / SDK Pulse
                 ↓
표면 갱신·변경 타일 추적·투영 작업 배분
                 ↓
Linux BOSIO ABI 3 → 투영코어 0 / 투영코어 1
                 ↓
결과 타일 합성 → 전체 장면 또는 BPT2 부분 갱신
                 ↓
출력코어의 DDR 장면 버퍼 전환 → 기존 HDMI IP
```

`src/wm.*`는 소유권·창 순서·포인터 캡처·입력 이벤트를, `apps.*`는 캡션·이동·크기 조절·최소화·복원·보관함을 담당한다. `ui.*`와 `font.hpp`는 기존 UI 픽셀을 재현한다. `geometry.*`는 4,220개 타일과 정수 투영 기술자를 생성한다. `backend.*`는 두 투영코어 큐와 출력 버퍼의 승인·완료를 다룬다. `runtime.*`는 Unix 소켓 서비스와 UART 명령을 제공한다.

일반 하드웨어 합성 경로에서는 전체 광선 배열을 만들지 않는다. 하드웨어 투영이 지원하지 않는 창 장식/기하 또는 외부 RGB 장면은 보존한 원본 `reference/wm/native/bosio_compositor.cpp`의 CPU 합성 경로를 사용한다. 이 경로는 필요할 때 광선 배열을 만들므로 더 큰 메모리와 CPU 시간이 필요하다. 모든 장면이 두 투영코어만으로 처리된다고 해석하면 안 된다.

SD 부트 로더는 하드웨어로 MIG에 직접 데이터를 쓴다. Linux initramfs 해제·파일 생성 및 BOSIO 드라이버의 사용자 버퍼 복사는 CPU 작업이다. `dma_alloc_coherent`는 공유 버퍼 할당이며 복사 엔진이 아니다. 이번 변경은 RTL, DMA 경로, MIG, HDMI IP, 비트스트림을 변경하지 않는다.

## 유지 범위와 차이

| 항목 | C++ 구현 |
|---|---|
| 창 생성·표면 부분 갱신·z 순서·포커스·포인터·버튼·스크롤 | 기존 JSON 인터페이스 유지, Python 기준과 비교 |
| 런처 및 세 가지 활성 앱 | C++ 구현, 고정 입력에서 픽셀 비교 |
| 캡션·이동·크기 조절·최소화·복원·닫기·보관함 | C++ 구현, 상태 및 배치 비교 |
| UART WASD·방향키·중앙 클릭·시야 초기화 | C++ 명령, CRLF 중복 클릭 방지, 종료 시 터미널 복원 |
| 두 투영코어·출력 버퍼·부분 갱신 | 기존 ABI 3 유지, 명시적 Mock에서 결과 비교 |
| 기존 Python SDK 앱 소스 직접 실행 | 새 SD에는 Python이 없어 불가. 외부 앱은 JSON 인터페이스로 연결 |
| 앱 실행 격리 | 기존 여러 Python 프로세스 대신 단일 C++ 서비스 내 내장 앱 |
| 원본에서 비활성화된 런처 항목 | 이번 이식의 실행 앱에 포함하지 않음 |
| 센서·USB 키보드·AA 제어 | 새로 추가하지 않음. UART 입력 사용 |

기능 비교 통과는 시험한 상태·입력·장면에 대한 근거다. 모든 입력에 대한 형식적 동등성 증명이나 실제 보드 HDMI 확인은 아니다.

## 보드에서 실행

새 이미지도 BusyBox 쉘로 부팅하며 BoAYO를 자동 시작하지 않는다. 현재는 사용자 요청대로 보드에 접속하거나 SD에 기록하지 않는다. 향후 사용자가 SD를 교체할 때 `images/ORYUKDO-KFB-BoAYO-native-v2-raw.img` 전체를 Etcher로 기록·검증한다. 압축하지 않은 initramfs로 CPU의 gzip 해제 작업을 줄이는 후보이며 실제 부팅 시간은 보드에서 측정해야 한다. `gzip.img`는 용량 및 동작 비교용이다.

```sh
cat /etc/oryukdo-image
bosioctl info
boayo-selftest
boayo-start
boayo-status
boayo-app dashboard
boayo-keys
# WASD/방향키: 시야, Space/Enter: 중앙 클릭, R: 초기화
# +/-: 각도 간격, Q: 키 입력 종료(BoAYO는 계속 실행)
boayo-stop
cat /tmp/boayo-wm.log
```

`boayo-start --mock`와 `boayo-dual-selftest`는 모델 검사다. 실제 HDMI 출력 검증에는 `--mock` 없이 시작해야 한다. 상태의 `mock_backend`, `render_error`, `app_error`를 구분한다. 쉘과 파일 변경은 RAM에 있으므로 재부팅 시 사라진다. Bash·Vim·SD 영구 파일시스템은 이번 범위에 추가하지 않는다.

## 빌드와 검증

WSL Ubuntu의 기존 RV32 musl 도구 체인, 진단 Linux 소스 및 기존 BusyBox 루트를 사용한다. `build.sh`는 컴파일 전에 소스 복사본을 만들어 빌드 중 수정과 분리하고 바이너리별 `.build.json`에 해시를 기록한다. `prepare_image.py`는 소스와 빌드 해시를 대조하며 빈 출력 루트만 허용한다.

```sh
native=/mnt/d/ORYUKDO-Gen3_AB_integrated_unverified/ORYUKDO-Gen3/work/bosio-linux-sw/software/boayo/native
out=/var/tmp/oryukdo-boayo-native-v2-final
bash "$native/build.sh" "$out" host
bash "$native/build.sh" "$out" rv32
bash "$native/build.sh" "$out" sanitize
python3 "$native/tests/run_suite.py" "$out/boayo-native-host" "$out/reports/host"
python3 "$native/tests/run_suite.py" "$out/boayo-native-sanitize" "$out/reports/sanitize"
bash "$native/build_images.sh" "$out"
python3 "$native/tests/test_image.py" "$out/images/ORYUKDO-KFB-BoAYO-native-v2-raw.img" "$out/reports/qemu-raw"
python3 "$native/tests/test_image.py" "$out/images/ORYUKDO-KFB-BoAYO-native-v2-gzip.img" "$out/reports/qemu-gzip" --boot-only
```

이미지 재생성은 기존 출력 루트를 덮어쓰지 말고 새로운 출력 디렉터리를 사용한다. 도구 체인: `/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-g++`. Linux 소스: `/var/tmp/oryukdo-boayo-boottrace-v1/linux`. 루트 기준: `/var/tmp/oryukdo-boayo-boottrace-v1/root`. 호스트 비교 시험에는 기존 Python 기준 구현의 NumPy/Pillow 의존성이 필요하지만 새 SD에는 들어가지 않는다.

`test_core.py`, `test_ui.py`, `test_apps.py`는 Python 기준 상태·기하·픽셀을 비교한다. `test_compositor.py`는 두 정수 투영 모델의 전체 장면 및 BPT2 부분 갱신을 비교한다. `test_fallback.py`는 원본 CPU 합성과 비교한다. `test_runtime.py`는 실제 Unix 소켓과 가상 터미널에서 런처·앱·WASD를 검사한다. ASan/UBSan으로 동일 시험을 반복한다.

`test_image.py`는 SD에서 OpenSBI·커널·DTB를 CRC 확인 후 추출해 QEMU RV32IMA에서 실행한다. BusyBox 쉘·상위 메모리·C++ 자체 검사·모델 기반 BoAYO 구동·UART 입력을 검사한다. QEMU는 SD SPI, MIG, ORYUKDO CPU RTL, 실제 투영코어·출력코어·HDMI를 실행하지 않는다. 사용자가 중단한 전체 CPU RTL 부팅 시험은 재시작하지 않는다.

최종 실행 결과와 용량·해시는 산출물의 `RESULTS.md`, `reports/`, `release.json`에 기록한다. Python 원본 및 JSON 라이브러리 라이선스는 `reference/`와 `vendor/`에 보존하고 이미지에도 포함한다.
