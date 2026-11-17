# BoAYO RGB 배경 DMA 연결 — rgbdma-v1

## 변경 범위

실보드 launchtrace-v1에서 런처의 640×360 RGB24 배경(691,200바이트)을 CPU로 채우는 데 약 311.6초가 걸렸다. 이는 계측·스케줄링을 포함한 기존 버전의 관측값이며 새 버전 성능값이 아니다.

이 후보는 `Surface::clear`, 창 생성 시 기본색 채우기, WM `fill`을 기존 메모리 DMA에 연결한다. CPU가 12바이트 RGB 패턴을 쓰고 DMA 복사로 12KiB까지 확장한 뒤, 고정한 사용자 버퍼 페이지에 반복 복사한다. RGB(3,5,8)처럼 서로 다른 세 바이트를 정확히 유지한다. 같은 색은 패턴을 재사용한다.

- 기존 투영코어·출력코어·DMA RTL·MIG·HDMI IP·비트스트림은 수정하지 않았다.
- 앱의 일반 가상주소를 DMA 주소로 쓰지 않는다. 커널이 `FOLL_WRITE | FOLL_LONGTERM`으로 페이지를 고정하고 DMA 주소로 매핑한다.
- 대상 버퍼는 렌더 스레드가 독점한다. ioctl은 모든 전송의 완료 응답 및 DMA 매핑 해제까지 기다린다.
- 4바이트 정렬, 4바이트 배수, 4KiB 이상 버퍼에 적용한다. 작은 버퍼·정렬 불충족·구형 커널은 정확한 CPU 대체 경로를 사용한다.
- 장치 오류 후 CPU로 조용히 재시도하지 않는다. 시간 초과 시 드라이버는 페이지·매핑·패턴을 유지하고 장치를 사용 불가로 표시한다. BoAYO는 버퍼를 해제하지 않고 전체 프로세스를 종료한다. 복구에는 보드 리셋이 필요하다.
- 기존 ABI3 구조체와 명령은 유지하고 `BO_DMA_RGB_USER` 기능 비트와 `BO_RGB_FILL`을 추가했다.
- DMA 패턴용 DDR 메모리 12KiB를 별도 사용한다. BRAM이나 FPGA 논리를 추가하지 않는다.

## 아직 CPU가 하는 작업

벡터 할당 때의 기본 0 채우기, 사각형·둥근 모서리·원·글자 그리기, 변경 영역 검색, 일부 버퍼 복사는 이번 변경 대상이 아니다. 전체 UI 렌더링을 투영코어에 넘긴 변경이 아니다. 투영코어는 완성된 원본 이미지를 기존 방식대로 투영한다.

따라서 배경 채우기 경로를 바꿨다고 전체 시작 지연이 해소됐다고 판단하면 안 된다. 기존 CPU 처리량이 극단적으로 낮았던 근본 원인도 이 변경만으로 확정되지 않는다.

## 이미지와 적용

- SD: `D:\temp\gen3bosio\boayo-rgbdma-v1\ORYUKDO-KFB-BoAYO-rgbdma-v1.img`
- 체크섬과 실제 크기: 배포 폴더의 `SHA256SUMS.txt`, `result.json`.
- 커널: `6.12.0-oryukdo-rgbdma1`
- 식별자: `/etc/oryukdo-image`에 `ORYUKDO-BoAYO-rgbdma-v1`.
- 기존 비트스트림: `D:\temp\gen3bosio\sd4-dma\candidate\ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`
- HDMI는 HDMI2. BusyBox 및 ext4 `/data` 구성은 유지한다.

보드를 정상 종료한 뒤 SD를 새 이미지로 기록·검증한다. 이 기록은 SD 전체를 덮어쓴다. 전원을 켠 뒤 기존 비트스트림을 올리고 UART 셸에서 다음을 확인한다. BoAYO는 자동 시작하지 않는다.

```sh
uname -r
cat /etc/oryukdo-image
bosio-dma-check
bosio-rgb-check
boayo-start
```

`bosio-rgb-check`는 실제 DMA로 런처 크기를 채우고 RGB 전 바이트, 페이지 경계, 양끝 보호 바이트, fork 후 부모 메모리 보존(COW), 읽기 전용 페이지 거부, 완료 카운터를 확인한다. 전체 바이트를 CPU로 비교하는 과정 자체는 느릴 수 있다. `RGB_DMA_DONE ... elapsed_ms=...`는 DMA ioctl 구간만의 시간이다. 이 검사 도구를 이미지에 넣은 사실을 실보드 검사 통과로 보고하지 않는다.

`BOAYO_INITTRACE`의 `rgb.dma.begin/end`는 실제 Linux DMA 경로 진입·완료를 표시한다. 기존 `surface.clear.begin/end`와 함께 전후 시간을 비교한다. `desktop.end`, 첫 프레임 표시, 실제 HDMI 화면·WASD는 별도 확인 대상이다.

## 검증 근거와 한계

- 실제 드라이버 함수에 대한 호스트 경계 모델: 색 5종 × 크기 8종 × 페이지 시작 위치 6종 × 분산/병합 구간 2종, 총 480조합. RGB 순서, 범위 밖 보존, 패턴 재사용, 부분 pin·매핑 실패, 준비·제출·완료 오류, 시간 초과 후 늦은 쓰기의 메모리 수명을 검사한다. ASan/UBSan을 사용한다. Linux 페이지 고정·COW 자체는 호스트 모델의 검증 범위 밖이다.
- 기존 기능 회귀시험 9개 및 CPU/모의 DMA 경로의 런처 픽셀·투영 장면 데이터 비교.
- RV32 앱과 실제 Linux 커널 빌드. 빌드 소스·설치 실행 파일·내장 initramfs·SD 페이로드/CRC·ext4 일치 검사.
- SD 로더 RTL 시험은 핀 수준 SD 모델과 모의 DDR을 쓴다. CPU가 Linux를 실행하는 RTL 시험은 아니다.
- QEMU는 실제 SD 페이로드를 사용하지만 BOSIO는 모의 장치다. Linux 셸·BoAYO·WASD 회귀 확인에 쓰며 실제 DMA ioctl·HDMI 검증을 대신하지 않는다.
- 최종 실행 결과는 `evidence/` 및 `result.json` 참조. 실보드 DMA 배경 채우기 성능과 HDMI는 미검증이다.

현재 보드의 이전 진단 실행은 이 이미지 생성으로 변경하거나 재시작하지 않았다. 기존 launchtrace-v1 소스·이미지도 비교 기준으로 보존했다.

## 재현

WSL Ubuntu, 저장소 루트에서 실행한다.

```sh
bash updates/boayo-rgbdma-v1/build.sh /var/tmp/oryukdo-boayo-rgbdma-v1/release
bash updates/boayo-rgbdma-v1/software/boayo/native/build.sh /var/tmp/oryukdo-boayo-rgbdma-v1/host host
python3 updates/boayo-rgbdma-v1/software/boayo/native/tests/run_suite.py /var/tmp/oryukdo-boayo-rgbdma-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-rgbdma-v1/reports/host
python3 updates/boayo-rgbdma-v1/tests/test_trace.py /var/tmp/oryukdo-boayo-launchtrace-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-rgbdma-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-rgbdma-v1/reports/trace
```

기본 입력은 `/var/tmp/oryukdo-bosio-dma-v1/release`의 Linux 소스·설정·BusyBox root·OpenSBI다. 원본을 별도 트리에 복사한다. 앱은 기존 RV32 musl 툴체인, 커널은 `riscv64-linux-gnu-`, 이미지에는 dtc/e2fsprogs를 사용한다. 실제 드라이버 검증은 `tests/run_driver.py`, 이미지 감사는 `tests/verify_build.py`, QEMU는 `tests/test_image.py`, 배포 묶음은 `package.py`에서 수행한다.
