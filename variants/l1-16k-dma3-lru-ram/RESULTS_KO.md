# 해반천 — L1I/D 각각 16KiB·2Way, DMA 3개 최종 결과

2026-11-14, PZ-A7200T-KFB / `xc7a200tfbg484-2`의 전체 보드 합성·배선·비트스트림 생성을 통과했다. MIG/DDR 1GiB, 공식 HDMI2, SD4, 두 투영코어와 출력코어를 포함한다. 실보드 업로드와 부팅·HDMI·성능 측정은 아직 수행하지 않았다.

## 배선 후 자원과 타이밍

| 항목 | 기존 SD4·DMA 1개 | 이번 L1 16KiB × 2·DMA 3개 |
|---|---:|---:|
| LUT | 114,075 | 119,141 |
| FF | 74,189 | 76,182 |
| BRAM tile | 101.5 | 110 |
| DSP | 62 | 62 |
| 사용 Slice | 32,792 | 33,131 |
| Setup WNS / TNS | +0.034 / 0 ns | +0.093 / 0 ns |
| Hold WHS / THS | +0.027 / 0 ns | +0.011 / 0 ns |
| CDC Warning·Critical | 0 | 0 |

CPU 50MHz(20ns), fabric 100MHz(10ns), SD 25MHz, pixel 약 74.25MHz, serializer 약 371.25MHz다. CPU/fabric의 setup uncertainty 0.3ns를 유지했다. 완전 배선, routing error 0, 내부 무제약 endpoint 0, bus skew 통과, timing waiver 없음이다. WPWS/TPWS도 0/0이다.

예약 영역을 제외한 사용 가능 Slice 33,450개 중 33,131개(99.05%)를 사용한다. LUT는 사용 가능 133,800개 중 119,141개(89.04%)다. LUT 비율만 보고 넉넉한 설계라고 판단하면 안 된다. CPU 100MHz는 이번 결과에 포함되지 않는다.

DRC Error·Critical은 0이다. 원본 MIG 내부의 BUFC-1 4건, REQP-1709 1건은 남아 있으며 waiver로 숨기지 않았다. 새 사용자 로직 DRC는 없다. Vivado의 일반 실행 경고와 DRC 개수는 구분한다.

## 주요 모듈별 배선 후 자원

| 모듈 | LUT | FF | BRAM36 / BRAM18 | DSP |
|---|---:|---:|---:|---:|
| MIG | 5,926 | 5,025 | 0 / 0 | 0 |
| HDMI 출력 어댑터·IP | 200 | 139 | 0 / 0 | 0 |
| I/D 캐시 묶음 | 4,009 | 1,484 | 8 / 0 | 0 |
| L1I | 2,154 | 655 | 4 / 0 | 0 |
| L1D | 1,823 | 690 | 4 / 0 | 0 |
| DMA0 / SD 공유 | 608 | 351 | 0 / 0 | 0 |
| DMA1 | 500 | 338 | 0 / 0 | 0 |
| DMA2 | 499 | 338 | 0 / 0 | 0 |
| 투영코어 0 | 3,763 | 6,062 | 1 / 3 | 11 |
| 투영코어 1 | 3,939 | 6,023 | 1 / 3 | 11 |
| 출력코어 | 21,594 | 24,620 | 20 / 2 | 36 |

계층별 수치이므로 캐시 묶음과 그 하위 I/D 행을 중복 합산하지 않는다. 표는 주요 모듈만 표시하며 전체 합계 표가 아니다.

## 최종 임계 경로

| 클럭 영역 | 경로 | 데이터 지연 | logic / routing | Setup slack |
|---|---|---:|---:|---:|
| CPU 50MHz | CPU `ctrl_valid` → FDU window 0 / slot 2 `o_sidefx` | 19.495ns | 5.264 / 14.231ns | +0.099ns |
| fabric 100MHz | SD boot state → SD DAT 출력 레지스터 | 9.076ns | 1.595 / 7.481ns | +0.093ns |
| pixel 74.25MHz | 영상 Y 좌표 → 공식 HDMI encoder | 11.862ns | 1.551 / 10.311ns | +1.341ns |

실제 경로 이름과 전체 STA는 `reports/board/timing_*.rpt`, 요약은 `provenance/board_manifest.json`에 있다. slack을 단순 역산한 값을 실제 최대 동작 주파수로 주장하지 않는다.

## 검증

- RTL 증거 로그 44건 PASS. 캐시 전체 용량·LRU·부분 쓰기·오류·무효화/refill 경합과 seed별 4,000회 무작위 접근을 포함한다.
- 실제 CPU로 FENCE.I 자기 수정 코드, AMO, 세 DMA 동시 작업·각 IRQ, 캐시된 목적지, SD 왕복, M/S 인터럽트를 검증했다.
- Sv32 별칭, PTE 갱신/SFENCE.VMA, 가상 주소 Store와 물리 DMA copy, paging 중 인터럽트를 일반 AXI RAM과 실제 보드 DDR adapter + Mock DDR 양쪽에서 검증했다.
- 실제 두 투영코어→출력코어의 단색·패턴 장면, 768워드 쓰기/snoop, 픽셀·역압·SOF/EOL을 검증했다.
- 상위 DDR 주소, SD CRC·timeout·오류 부트 이미지 거부, DMA 4KiB 경계와 원자 잠금 회귀를 포함한다.
- LRU 변경 전후의 별도 3개 seed 시험에서 모든 클럭의 CPU handshake·유효 결과·AXI 요청이 일치했다. 후보의 초기 LRU를 기준본과 다르게 설정했고 reset·flush·오류도 포함했다.
- 전체 SoC lint의 오류·래치·다중 드라이버·조합 루프 0, 새 캐시/DMA 인터페이스 경고 0이다. 모든 기존 lint 경고가 0이라는 뜻은 아니다.

Linux 셸·메모리 검사·BoAYO 앱/첫 장면/WASD/click은 동일 SD 이미지의 QEMU + Mock BOSIO에서 통과했다. 이 시험은 실제 CPU 캐시나 HDMI 하드웨어 검증이 아니다. LRU 후보의 실행 소프트웨어·DT 20개 파일은 이미지 빌드 입력과 동일하며, QEMU를 중복 실행했다고 주장하지 않는다.

## 변경과 실패 원인

I/D 각각 32KiB였던 후보를 16KiB·256 sets로 줄이고, LRU 정보를 reset FF에서 작은 분산 RAM으로 변경했다. 유효한 두 way가 존재하기 전에 line 할당이 LRU를 기록하므로 LRU payload 초기화가 필요 없다. 데이터 BRAM 전체 초기화도 하지 않는다. LRU 저장 방식 변경 전후에는 16KiB geometry·coherence·CPU 동작 클럭 수가 같다.

초기 16KiB 후보의 완전 배선 결과는 WNS -5.732ns였다. 출력코어의 한 enable이 3,841개 부하를 구동했고, 15.279ns 경로 중 배선이 14.375ns를 차지했다. LRU 후보의 배치에서 이 enable의 구동 LUT만 12개 추가 복제한 뒤 배선했다. [AMD의 지정 net 복제 방법](https://docs.amd.com/r/2022.1-English/ug949-vivado-design-methodology/Use-Physical-Optimization)을 사용했다. 최종 후속 물리 최적화는 UART 출력과 SD DAT 경로의 작은 위반을 해소했다.

원본 스케줄러, Gen2 파일, BOSIO vendor RTL, HDMI IP 내부, MIG 설정은 기준 `sd4-dma`와 바이트 단위로 같다. RS MUXF 비교안은 최종 선택하지 않았다. 별도로 시험한 CSR 카운터 계산 변경도 최종 RTL에 넣지 않았다. 클럭 완화, 기능 비활성화, timing/DRC waiver는 사용하지 않았다.

같은 합성 직후 단계로 비교하면 32KiB 후보는 LUT 127,895 / FF 78,695 / BRAM 117.5, 최종 16KiB LRU 후보는 121,319 / 76,558 / 110이다. 32KiB의 배선 후 PASS는 없으므로 이 수치를 배선 후 비교와 섞지 않는다. 캐시 외 CPU AXI 응답 FIFO의 RAMB18→RAMB36 매핑 변화가 전체 BRAM 0.5타일 차이에 포함된다.

## 산출물과 남은 실보드 확인

- 비트스트림: `D:/temp/haebancheon-l1-16k-dma3/HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit`
- SD: `D:/temp/haebancheon-l1-16k-dma3/image/HAEBANCHEON-KFB-L1-16K-DMA3-v1.img`
- 최종 원본 구현: `D:/temp/haebancheon-l1-16k-dma3/board_route_lru_fanout_v1`
- 배포 파일·입력 체크포인트·소스 해시: `provenance/release_manifest.json`, `implementation_chain.json`, `board_manifest.json`.

새 비트스트림과 전용 SD를 함께 사용한다. 기존 단일 DMA 비트스트림과 이 SD를 혼용하지 않는다. 새 구성의 실보드 Linux 부팅, DDR 검사, HDMI 표시, BoAYO 첫 화면 시간과 16/32KiB 성능 비교는 남아 있다. 이번 작업에서는 실제 CPU RTL로 Linux 전체 콜드 부팅을 끝까지 실행하지 않았다. 외부 HDMI receiver/PCB의 타이밍 예산도 임의로 가정하지 않았다.
