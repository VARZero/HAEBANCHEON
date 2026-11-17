> **2026-11-17 현재 비트스트림 소스 게시:** [현재 구성·외부 IP 준비·재현 방법](docs/현재_비트스트림.md)을 먼저 읽어 주세요. 실제 사용 RTL은 `variants/l1-16k-dma3-lru-ram/`이며 L1I/D 각각16KiB·DMA3·MIG DDR1GiB·HDMI2를 포함합니다. MIG 생성물과 새 구성의 HDMI IP 원본은 제외하고 설정·연결 RTL·고정 upstream 해시를 제공합니다. [전체 주소·인터럽트·요청 구조체](docs/HAEBANCHEON_SOC_REVIEW_PROMPT_KO.txt). 아래 날짜별 항목은 당시 기록이며 최신 실보드 결과와 구분합니다.

> **2026-11-16 BoAYO v14 실보드 후속:** [변경·SD](updates/boayo-tilebatch-v14/README_KO.md), [로컬 검증](updates/boayo-tilebatch-v14/RESULTS_KO.md), [실보드 비교](updates/boayo-tilebatch-v14/BOARD_RESULT_KO.md). Linux 부팅·HDMI 출력은 확인했으나, 같은 보드·커널·사각형 UI 비교에서 Telemetry 2.618→3.444초, SDK Pulse 2.391→3.029초로 느려졌다. 작은 반복 글자 시험의 요청 수 640→80회 감소는 전체 앱 속도 개선이 아니며, v14는 현재 성능 회귀로 기록한다. RTL·드라이버·비트스트림은 유지한다.

> **2026-11-15 BoAYO v12:** [변경·새 SD](updates/boayo-mask-v12/README_KO.md), [실보드 결과](updates/boayo-mask-v12/BOARD_RESULT_KO.md). 경계 마스크 비교 90.8% 단축, 이동·크기 변경·닫기 25단계와 원상 복귀 통과. 새 위치 첫 계산은 여전히 약 217–225초로 느리며 완전 해결로 보고하지 않는다. 동일 앱의 RAM 검증 후 [새 SD 실보드 콜드 부팅·HDMI 출력](updates/boayo-mask-v12/COLD_BOOT_KO.md)도 확인했다.

> **2026-11-15 BoAYO v11/fix1:** [실보드 결과](updates/boayo-incremental-v11-fix1/BOARD_RESULT_KO.md). 정상 Telemetry 갱신 3.339→2.529초(약 24% 단축), 경계 처리·메타데이터 비교 개선, 본문 호버 에셋 누락 수정. 새 창 위치의 CPU 마스크 생성 지연은 미해결이며 전체 조작 시험 통과가 아니다. [fix1 이미지·소스](updates/boayo-incremental-v11-fix1/README_KO.md).

> **2026-11-14 BoAYO 갱신 시간 개선 v10:** [변경·실보드 계측·검증](updates/boayo-frame-v10/README_KO.md). 같은 계측 조건에서 Telemetry 갱신 간격 6.07초 → 3.73초, 앱 처리 2.59초 → 0.81초. 처리 시간을 포함한 2fps 주기로 수정하고 120개 장면 비교를 통과했다. RTL·SD는 변경하지 않았으며 보드에서는 RAM의 수정 실행 파일을 검증했다. 아래의 이전 후보·미측정 표기는 각 버전의 당시 기록이다.

> **HAEBANCHEON 동작 검증 모드:** [motion-v7 사용법](updates/boayo-motion-v7/README_KO.md). 피치·롤·요 혼합 회전, 공통 버튼을 통한 창 방위각·위도 이동과 크기 변경, 겹침·닫기를 25단계로 실행한다. 기존 L1 16KiB·DMA3 비트스트림을 사용하며 새 SD가 필요하다. 실보드 HDMI 확인은 별도다.

> **HAEBANCHEON 앱·공통 컴포넌트 후보:** [components-v6 설명](updates/boayo-components-v6/README_KO.md). Dashboard·Telemetry·SDK Pulse의 고정 그림과 BoAYO 공통 이동·크기 조절·닫기 버튼을 사전 제작한다. 37개 투영 계획과 DMA 이미지 조합을 사용한다. 기존 L1 16KiB·DMA3 비트스트림용 새 SD이며, 실제 HDMI·성능은 새 이미지로 별도 확인한다.

> **해반천 고정 장면 실행 자료 후보:** [tiles-v5 설명](updates/boayo-tiles-v5/README_KO.md). 첫 장면의 투영·DMA 요청과 타일 디렉터리를 PC에서 만들고, 정상 실행의 전체 타일 재검사와 오류 문구 할당을 제거한다. 기존 비트스트림을 사용한다. 실보드 시간과 HDMI 확인은 새 SD로 별도 측정한다.

> **메타데이터 통합 전환·경계 비트마스크 후보:** [tiles-v4 설명](updates/boayo-tiles-v4/README_KO.md). CPU 스냅샷으로 검증·차이 업로드·전환을 한 요청으로 처리하여 출력 DDR 메타데이터 재읽기를 제거하고, 경계 마스크를 드라이버 비트 형식으로 미리 만든다. 87.7초 구간의 세부 계측을 추가했으며 실제 단축량은 새 SD로 측정해야 한다. 기존 비트스트림은 유지한다.

> **내장 자료 빠른 로드·정수 타일 버전·요청 묶음 후보:** [tiles-v3 설명](updates/boayo-tiles-v3/README_KO.md). 실행 중 내장 계획 전수 검사를 제작 단계로 옮기고, 타일별 JSON 식별을 정수 버전으로 바꿨다. 첫 장면의 Fill/경계 합성 요청 88회를 2회 묶음으로 전달한다. 새 Linux 드라이버를 포함한 SD 이미지이며 기존 비트스트림을 사용한다. 실보드 속도는 별도 측정해야 한다.

> **타일 버전 관리·조건부 전환 후보:** [tiles-v2 설명](updates/boayo-tiles-v2/README_KO.md). 비활성 셀 초기화 없이 필요한 타일만 생성하고, 버퍼별 버전·내용 식별 정보로 복사와 재투영을 생략한다. 메타데이터가 같으면 BS25 주소 전환만 사용한다. 기존 SD4 비트스트림을 유지하며 실보드 실행은 별도다.

> **변경 타일 직접 출력 후보:** [tiles-v1 설명](updates/boayo-tiles-v1/README_KO.md). 기본 경로의 CPU 전체 장면 초기화·픽셀 사본·투영 결과 회수를 제거하고, DDR의 변경 타일만 투영·복사한다. 새 SD 이미지와 기존 SD4 비트스트림을 사용한다. 실보드 실행은 아직 하지 않았다.

> **첫 런처 투영 사전 계산 후보:** [plan-v1 설명·검증](updates/boayo-plan-v1/README_KO.md). 기본 배치의 타일 목록·계수·모양 마스크를 PC에서 생성하고, 완전히 제외되는 타일 64개의 투영을 생략한다. 기존 비트스트림을 사용하며 새 이미지의 실보드 속도·HDMI 확인은 남아 있다.

> **원본 텍스처 직접 DMA 후보:** [한국어 설명 및 검증 범위](updates/boayo-rawdma-v1/README_KO.md). 고정 UI의 압축 해제·RGB 중간 버퍼·CPU 업로드 복사를 제거했다. 실보드 성능은 미검증이며 기존 비트스트림을 사용한다.

# 해반천 (HAEBANCHEON)

> **2026-11-14 L1 16KiB × 2 / DMA 3개 구현 완료:** [최종 소스·구성](variants/l1-16k-dma3-lru-ram/README.md), [검증·자원·임계 경로](variants/l1-16k-dma3-lru-ram/RESULTS_KO.md). MIG/HDMI를 포함한 전체 보드 배선에서 LUT 119,141 / FF 76,182 / BRAM 110 / DSP 62, CPU 50MHz·fabric 100MHz, Setup +0.093ns·Hold +0.011ns다. RTL 44건과 추가 LRU 동작 비교 3개 seed를 통과했다. 실보드 실행·성능은 미측정이며 전용 비트스트림과 SD를 함께 사용한다. [초기 16KiB 실패 기록](variants/l1-16k-dma3/RESULTS_KO.md)과 [32KiB 비교본](variants/l1-32k-dma3/README.md)은 보존했다.

해반천은 ORYUKDO CPU, BOSIO 투영·출력 시스템, BoAYO 윈도우 매니저를 통합하는 FPGA SoC 프로젝트이다. 기존 프로젝트명은 `ORYUKDO-3d5i_BOSIO_SoC`이며, 각 하위 구성요소의 이름과 RTL 모듈명·실행 파일명은 유지한다.

> **미리 만든 2D UI 후보:** [assets-v1 안내](updates/boayo-assets-v1/README_KO.md). 132개 고정 UI를 재사용하고, 투영 입력 변환과 중복 업로드를 줄였습니다. 기존 비트스트림을 사용하며 실보드 성능·HDMI 검증은 별도입니다.

> **RGB 배경 DMA 후보:** [rgbdma-v1 안내](updates/boayo-rgbdma-v1/README_KO.md). CPU가 수행하던 런처·창의 전체 배경 채우기를 기존 DMA에 연결한 별도 SD 후보입니다. 기존 진단본은 보존하며 실보드 성능·HDMI 확인은 별도로 진행합니다.

> **런처 초기화 진단:** [launchtrace-v1 SD 안내](updates/boayo-launchtrace-v1/README_KO.md). BOSIO DMA·SD는 실보드에서 통과했지만 런처 표시는 아직 미검증이며, CPU의 창 생성·그리기·복사를 UART로 구분하는 별도 진단 이미지입니다.

> **BOSIO DMA 후속 후보:** [버퍼 복사·초기화 연결](updates/bosio-dma-v1/README_KO.md)을 추가했습니다. 기존 SD4 비트스트림은 유지하며 새 커널·BoAYO·SD 이미지로 사용합니다. 로컬 검증과 이번 후보의 미실행 실보드 검증을 구분합니다.

> **2026-11-10 실보드 후속:** SD4 v1은 Linux 셸·SDHC 탐색까지 성공했지만 디스크 등록 및 `/data` 마운트에 소프트웨어 오류가 확인됐습니다. [Linux v2 수정본](updates/sd4-linux-v2/README_KO.md)은 4KiB 요청 분할과 마운트 옵션을 수정하며 기존 비트스트림을 유지합니다. v2 실보드 셸·파티션·ext4 마운트 및 256KiB 기록/재마운트/재읽기를 확인했습니다. 최소 BusyBox용 보완 검사 도구는 해당 안내를 따릅니다.

## 최신 추가: SD 4비트·DMA·ext4 (2026-11-10)

[SD4·DMA 구성](variants/sd4-dma/README.md)에 native 4비트 SD, CPU 제어 DMA, PLIC 외부 인터럽트,
Linux MMC/DMAengine 드라이버와 영구 ext4 `/data`를 추가했다. BusyBox와 BoAYO native-v2는 보존했다.
소스·테스트·원본 검증 로그·Vivado 리포트는 `variants/sd4-dma/`에 함께 있다.

**최초 SD4 v1 게시 당시에는 실보드 SD 파일시스템이 미검증이었다. 이후 Linux v2에서 실보드 `/data` 기록·재마운트·재읽기를 통과했다.**
기존 검증본과 혼동하지 않도록 루트의 2026-11-08 소스와 증거를 보존했다. 두 구성의 소스를 섞지 않는다.

| 항목 | SD4·DMA 전체 보드 배선 결과 |
|---|---:|
| CPU / fabric / SD 최대 | 50 / 100 / 25 MHz |
| LUT / FF / BRAM / DSP | 114,075 / 74,189 / 101.5 / 62 |
| Setup WNS / Hold WHS | +0.034 ns / +0.027 ns |
| TNS / THS | 0 / 0 |
| CDC Warning·Critical | 0 |
| DRC | Error·Critical 0, 기존 MIG Warning 5건 |

[새 사전 릴리스: 비트스트림·SD 이미지·Linux 전체 소스](https://github.com/VARZero/HAEBANCHEON/releases/tag/sd4-dma-20261110-candidate)
에서 파일을 받는다. 새 비트스트림과 새 SD 이미지는 한 쌍으로 사용해야 한다.

- [추가 기능과 검증 범위](variants/sd4-dma/README.md)
- [상세 구조·수정 내역·타이밍 분석](variants/sd4-dma/README_STORAGE_KO.md)
- [실보드 확인 절차](variants/sd4-dma/BOARD_STORAGE_CHECK_KO.md)
- [결과·소스·산출물 해시](variants/sd4-dma/provenance/board_manifest.json)

게시본 무결성 확인: `python scripts/verify_sd4_publication.py`.
대용량 이미지는 Git 이력에 넣지 않고 사전 릴리스 첨부파일로 관리한다.

## 기존 실보드 부팅 검증본 (2026-11-08, 루트 디렉터리)

PZ-A7200T-KFB에서 Linux 6.12와 BusyBox UART 쉘의 부팅을 확인한 ORYUKDO Gen3 + BOSIO SoC 소스이다. 기준은 **2026-11-08 실보드에서 검증한 TLB8 비트스트림과 trace-v2 진단 SD**의 조합이다.

비트스트림, SD 이미지, 전체 Linux 소스 압축본은 [실보드 검증본 Release](https://github.com/VARZero/HAEBANCHEON/releases/tag/board-20261108-tlb8-trace-v2)에서 받는다. 첨부한 `SOC-SHA256SUMS.txt`로 다운로드한 파일의 해시를 확인할 수 있다.

ORYUKDO 스케줄러는 3 Decode / 5 Issue이며 실행기는 Branch·CSR 1개, ALU 2개, MULDIV 1개, LSQ 1개로 구성한다. CPU에는 Sv32 MMU와 명령·데이터 측 각각 8엔트리 TLB가 있다. MIG를 통한 DDR3L 1GiB, SD 부트 로더, UART 콘솔, BOSIO 투영코어 2개·출력코어·센서 허브, HDMI 출력 연결, 추가 AXI UART·SPI·I2C를 포함한다.

## 문서

아래 내부 문서는 현재 SD4 후보와 보존한 2026-11-08 기준본을 구분해 설명한다.
최신 주소 맵·native SD 핀·파일 저장 절차는 SD4 구성을 기준으로 읽는다.

- [SoC 구조와 주소 맵](docs/구조.md)
- [소스 구성과 Linux 변경 내역](docs/소스_구성.md)
- [빌드 및 검증 방법](docs/빌드.md)
- [보드 사용 방법](docs/보드_사용.md)
- [검증 결과와 한계](docs/검증.md)
- [라이선스와 외부 소스](THIRD_PARTY.md)

## 확인한 결과

| 항목 | 결과 |
|---|---|
| 실보드 부팅 | JTAG 구성 → SD 적재 → OpenSBI → Linux → BusyBox 쉘 |
| UART 명령 | 산술 연산, 커널 버전, 메모리 용량 확인 PASS |
| 상위 DDR | 0xbee00000..0xbef08ffc의 16페이지 읽기·쓰기 PASS |
| CPU / fabric | 50 / 100 MHz |
| LUT / FF / BRAM / DSP | 108,798 / 72,969 / 101.5 / 62 |
| 배선 후 Setup WNS / Hold WHS | +0.101 ns / +0.023 ns |
| TNS / THS | 0 / 0 |
| CDC Warning·Critical | 0 |
| DRC | Error·Critical 0, 생성된 MIG 내부 Warning 5개 |

이 수치는 MIG·HDMI를 포함한 보드 전체 구현의 결과다. HDMI의 실제 화면, BoAYO WM, Linux용 BOSIO 드라이버는 검증 완료 항목이 아니다. 전체 RTL의 SD→쉘 부팅 역시 아직 PASS로 인정하지 않는다. 진단 표시 없는 일반 커널의 실보드 재시험도 별도이다.

## 디렉터리

| 경로 | 내용 |
|---|---|
| `rtl/`, `gen2/` | 사용 중인 오륙도 스케줄러와 을숙도 기반 의존 모듈 |
| `soc/rtl/` | CPU, EX, CSR, MMU/TLB, SoC 및 보드 적용 스케줄러 |
| `soc/bosio/`, `soc/vendor/` | AXI 연결, BOSIO 투영·출력·센서 RTL |
| `soc/board/` | 보드 최상위, SD, DDR 연결, HDMI 연결, 핀 제약, Vivado 스크립트 |
| `software/linux/` | Linux 6.12 패치, 수정 파일 전체, 실제 설정, 원본 해시 |
| `software/userspace/` | BusyBox·musl 빌드, init와 상위 메모리 검사 소스 |
| `software/firmware/` | 검증한 OpenSBI·Linux·DTB 및 initramfs 실행 파일 |
| `scripts/` | 소스 복원, Linux 빌드, 입력 일치 검사 |
| `evidence/` | 실제 UART 기록과 배선 후 보고서 |
| `provenance/` | 보드 입력 및 산출물 해시 |
| `artifacts/` | 로컬 비트스트림·SD·전체 Linux 소스 압축본. Git 추적 제외 |

처음에는 `python scripts/verify_sources.py`로 소스 일치를 확인한다. Linux 전체 소스는 `scripts/prepare_linux.py`로 정확한 원본과 패치에서 복원하거나, 함께 준비한 전체 소스 압축본을 사용한다. RTL과 공식 HDMI IP 본문은 게시 준비 과정에서 수정하지 않았다.
