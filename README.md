# HAEBANCHEON (해반천)

ORYUKDO CPU, BOSIO 투영·출력 코어, BoAYO 윈도우 매니저를 통합한 FPGA SoC 프로젝트입니다. PZ-A7200T-KFB에서 Linux와 BusyBox를 실행하고 HDMI2로 화면을 출력합니다.

## 현재 구성

**현재 비트스트림의 RTL은 [`variants/l1-16k-dma3-lru-ram/`](variants/l1-16k-dma3-lru-ram/README.md)입니다.** 루트와 다른 `variants/` 폴더는 이전 구성·비교본이므로 소스를 섞지 않습니다.

| 구성 | 내용 |
|---|---|
| 보드 | PZ-A7200T-KFB · Artix-7 XC7A200T |
| CPU | ORYUKDO RV32IMA · 3 Decode / 5 Issue · Sv32 |
| 캐시 | L1I / L1D 각각 16KiB · 2Way |
| 메모리·저장장치 | MIG DDR3 1GiB · 4비트 SD · DMA 3개 |
| 그래픽 | BOSIO 투영코어 2개 · 출력코어 · HDMI2 |
| 주변장치 | UART 2개 · SPI · I2C · PLIC · 센서 허브 |
| 소프트웨어 | Linux 6.12 · BusyBox · BoAYO C++ |

## 시작하기

저장소 루트에서 Python 3으로 현재 구성의 외부 HDMI IP를 준비하고 소스 일치를 확인합니다.

```sh
python scripts/prepare_current_hdmi.py
python scripts/prepare_current_hdmi.py --check-only
python scripts/verify_current_rtl.py --require-tracked
```

이 명령은 파일 준비·무결성 검사입니다. 기능 시뮬레이션이나 합성·배선을 실행하지 않습니다.

- **빌드:** [현재 비트스트림 안내](docs/현재_비트스트림.md)의 Vivado 재구현 절차를 따릅니다.
- **외부 IP:** HDMI 원본은 고정 버전으로 내려받고, MIG는 Vivado 2022.2에서 보존 설정으로 생성합니다. [전체 구성의 IP 준비 방법](docs/외부_HDMI.md)
- **현재 WM 개발판:** [삼각 타일 WM·빌드·실측 안내](updates/triangle-wm/README.md). 고정소수점·바이너리 통신·게시 드라이버 개선을 바탕으로, 이동은 S색 타일 하나, 크기 조절은 S색 빈 영역 전체를 표시합니다. S/B 색 분리와 최신 콘텐츠 반영을 지원합니다.
- **SD 이미지:** 현재 WM 개발판은 [v13 이미지·보드 검증 기록](updates/triangle-wm/SOLID_RESIZE.md)을 확인합니다. [BoAYO v14](updates/boayo-tilebatch-v14/README_KO.md)는 이전 비교본입니다. 새 환경의 SD 빌드에는 추가 커널·userspace 준비가 필요합니다.
- **통합 개발 SD 이미지:** [Linux·보시오·보아요 빌드 안내](software/userland/README.md). 투영 완료 IRQ가 PLIC 5·6번에 연결된 RTL로 BusyBox, Vim, Python 3, GCC/G++와 자체 앱을 실행합니다. 보시오는 2D 32×32 타일 변경 추적, 배경 복원, 두 투영코어와 DMA를 통한 부분 갱신을 제공합니다. [타일 미들웨어·측정 도구](software/userland/BOSIO_TILES.md)
- **보아요 WM 데몬:** [공유 메모리·별도 앱 프로세스](software/userland/WM_DAEMON.md). 빈 WM에서 시작하고 런처와 앱을 각각 실행합니다. 작은 변경은 RGB24 공유 버퍼와 변경 사각형 알림으로 전달하며 `boayo bench-small`로 요청부터 출력 완료까지 측정합니다.

비트스트림·SD 이미지·MIG 생성물·외부 HDMI IP 원본은 Git에 포함하지 않습니다.

## 확인된 결과와 남은 문제

2026-11-25 삼각 타일 WM 개발판에서 실제 입력→USB HDMI 캡처 중앙값은 이동 타일 **240ms**, S색 전체 채우기 크기 조절 **529ms**였습니다(각 3회). 같은 조건의 이전 외곽 타일 방식은 497ms여서 **이번 변경의 크기 조절 속도 개선은 확인되지 않았습니다.** 최종 본창 확정은 별도 검사에서 1.5–1.8초였고, 1초 목표는 남아 있습니다. [현재 동작·측정 범위·한계](updates/triangle-wm/SOLID_RESIZE.md)

다음은 이전 개발 이미지의 결과입니다.

기존 v14 이미지에서 Linux 셸과 BoAYO의 실보드 HDMI 출력을 확인했습니다. **v14의 앱 갱신 성능은 비교본보다 느렸습니다.** 최신 개발 SD는 QSPI에서 FPGA 구성을 시작해 Linux ext4 부팅, 두 투영코어 완료 IRQ와 보시오 부분 갱신을 실보드에서 확인했습니다. 보아요 창 이동은 23.4초, 8×8 픽셀 수정은 4.3초였으며, 기본 런처의 첫 화면 게시와 실제 HDMI 캡처는 검증이 남아 있습니다. [v14 실보드 비교 결과](updates/boayo-tilebatch-v14/BOARD_RESULT_KO.md) · [통합 이미지 검증 기록](software/userland/VALIDATION.md)

| 2026-11-17 polling 기준본의 보존된 배선 결과 | 값 |
|---|---:|
| CPU / fabric 클럭 | 50 / 100 MHz |
| LUT / FF | 119,141 / 76,182 |
| BRAM / DSP | 110 tiles / 62 |
| Setup WNS / Hold WHS | +0.093 ns / +0.011 ns |
| TNS / THS | 0 / 0 |

위 수치는 MIG와 HDMI를 포함한 이전 polling 기준본입니다. 투영 IRQ 후속 비트스트림은 Setup +0.140ns, Hold +0.011ns를 확인했습니다. 소스 변경과 해시는 [IRQ 후속 기록](variants/l1-16k-dma3-lru-ram/provenance/projection_irq_sources.json)에 있습니다. 타이밍 여유가 작으므로 새 빌드는 해당 배선 결과로 판단해야 합니다. [검증·자원·임계 경로 상세](variants/l1-16k-dma3-lru-ram/RESULTS_KO.md)

## 문서와 소스 위치

| 목적 | 안내 |
|---|---|
| 현재 RTL·빌드·검증 범위 | [현재 비트스트림](docs/현재_비트스트림.md) |
| 전체 SoC 구조·주소·인터럽트·요청 구조체 | [상세 검토 자료](docs/HAEBANCHEON_SOC_REVIEW_PROMPT_KO.txt) |
| 현재 하드웨어 소스 | [`variants/l1-16k-dma3-lru-ram/`](variants/l1-16k-dma3-lru-ram/) |
| BoAYO·드라이버 변경 및 버전별 검증 | [`updates/`](updates/) |
| 외부 소스와 라이선스 | [THIRD_PARTY](THIRD_PARTY.md) |
| 이전 공지·성능 수치·릴리스 링크 | [이전 README 기록](docs/이전_README_기록.md) |

이전 프로젝트명은 `ORYUKDO-3d5i_BOSIO_SoC`입니다. 하위 구성요소의 이름과 RTL 모듈명·실행 파일명은 유지합니다.
