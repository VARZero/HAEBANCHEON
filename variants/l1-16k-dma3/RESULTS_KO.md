# 해반천 L1 16KiB × 2 / DMA 3개 결과

초기 16KiB 후보는 전체 배선 후 Setup을 통과하지 못했다. LRU RAM과 출력 제어 신호의 구동 회로 분산을 적용한 [최종 후보](../l1-16k-dma3-lru-ram/RESULTS_KO.md)는 WNS +0.093ns·WHS +0.011ns로 통과했다. 아래 내용은 수정 전 후보의 비교 기록이며 최종 후보와 혼동하지 않는다.

## 구성과 보존 범위

- PZ-A7200T-KFB, `xc7a200tfbg484-2`, MIG를 통한 DDR 1GiB, 공식 HDMI2 출력, SD native 4비트.
- L1I와 L1D 각각 16KiB, 2-way, 256 sets, 32-byte line. D-cache는 write-through/no-write-allocate.
- 메모리 DMA 3개. 기존 SD 공유 DMA0와 새 DMA1·DMA2에 독립 요청·완료 IRQ를 연결했다.
- CPU 50MHz(20ns), fabric 100MHz(10ns), setup uncertainty 0.3ns 유지.
- 기준 `sd4-dma`의 스케줄러 `rtl/`, `gen2/`, BOSIO vendor RTL, HDMI IP 내부, MIG 설정은 SHA256 비교에서 동일하다.
- 선행 32KiB 구성은 보존했다. 사용자 선택으로 배선 전에 중단했으며 32KiB 배선 후 타이밍 PASS는 없다.

## 같은 단계의 합성 비교

두 구성 모두 `AreaOptimized_medium` 합성 직후 수치다. `opt_design` 이후나 배선 후 수치와 섞지 않는다.

| 항목 | I/D 각각 32KiB | I/D 각각 16KiB | 감소 |
|---|---:|---:|---:|
| 전체 LUT | 127,895 | 122,197 | 5,698 |
| 전체 FF | 78,695 | 77,098 | 1,597 |
| 전체 BRAM tile | 117.5 | 109.5 | 8 |
| DSP | 62 | 62 | 0 |
| I/D 캐시 묶음 LUT | 9,997 | 5,514 | 4,483 |
| I/D 데이터 BRAM36 | 16 | 8 | 8 |

16KiB `opt_design` 이후에는 LUT 121,560, FF 76,733, BRAM 109.5, DSP 62다. 이것도 배선 전 수치다.

## 기능 검증

`provenance/validation_summary.json`의 44개 RTL 증거 로그가 PASS다. seed는 1, 7, 23이다.

- I/D 전체 용량·2-way/LRU·부분 쓰기·오류·독립 응답·refill 무효화 경합 및 seed마다 4,000회 무작위 접근.
- 실제 CPU 명령 실행으로 FENCE.I 자기 수정 코드, AMO, 캐시된 DMA 목적지, 3개 엔진 동시 작업 및 각 엔진의 M/S 인터럽트.
- Sv32와 캐시를 함께 켜서 가상 주소 별칭, PTE 변경/SFENCE.VMA, DMA1 쓰기 후 가상 주소 읽기, 가상 주소 Store 후 DMA2 copy, paging 상태의 S-mode IRQ. 일반 AXI RAM과 실제 보드 DDR adapter를 포함한 Mock DDR 양쪽에서 확인했다.
- 실제 보드 AXI–MIG adapter/native arbiter와 Mock DDR에서 SD 전송 및 `0xbffff000..0xbffffffc` 상위 주소 접근.
- 실제 두 투영코어와 출력코어의 단색/패턴 장면, 768워드 projection write/snoop, 출력 픽셀과 역압·SOF/EOL.
- SD CRC/timeout/지연 입력, 부트로더의 잘못된 이미지 거부, DMA copy/fill·4KiB 경계·원자 잠금, PLIC 회귀.
- 전체 SoC lint. 새 캐시/DMA 인터페이스의 오류·래치·다중 드라이버·조합 루프 없음.

새 SD의 Linux·BusyBox 셸, 메모리 검사, BoAYO 앱·첫 장면·WASD/click은 **QEMU + Mock BOSIO에서 PASS**다. 실제 CPU RTL의 Linux 전체 부팅 시험과 실보드 HDMI 검증을 대신하지 않는다.

이미지 감사는 SD 헤더/CRC/실제 펌웨어 바이트/MBR/ext4, DT의 16KiB 캐시와 세 DMA 노드를 확인했다. tiles-v5와 비교해 root의 일반 파일 변경은 `/etc/oryukdo-image`뿐이며, data 파일과 OpenSBI는 동일하다. 새 DT와 커널 이미지는 별도로 재생성했다.

## 남은 판정과 한계

초기 배선은 혼잡 경고 `Route 35-447` 이후 약 3시간 19분에 완전 배선됐다. `routed_initial.dcp`를 다시 읽어 확인한 WNS는 -5.732ns, TNS -16093.087ns, WHS +0.023ns, THS 0이다. Setup 실패이므로 비트스트림을 만들지 않았다. 자동 후속 최적화도 -4ns 미만의 위반이 남아, 원본 체크포인트와 리포트를 보존하고 원인이 확인된 부분의 보완으로 전환했다.

가장 큰 위반은 출력코어의 DDR 캐시 상태→투영 설정 레지스터 enable 경로다. 전체 15.279ns 중 logic 0.904ns, routing 14.375ns이고, `u_projector/E[0]`의 3,841개 부하를 연결하는 한 net이 9.550ns를 차지했다. CPU는 FCL head→CSR `minstret[63]` 경로에서 -0.822ns였다. 원본 근거는 `routed_failure_review/timing_clk_pll_i.rpt`, `timing_cpu_raw.rpt`다.

`placement_review/qor_suggestions.rpt`는 RS FIFO 주변의 MUXF 배치와 LUT 결합, 부트 로더 주변의 동등 회로 복제를 혼잡 후보로 제시한다. 캐시 metadata FF 1,536개의 CE는 모두 상수 1로 합성돼 있어, 캐시에 단순히 `CONTROL_SET_REMAP ENABLE`을 추가하는 방식은 주요 해결책으로 보지 않는다. 이 분석은 [AMD QoR Suggestions 기능](https://docs.amd.com/r/2022.2-English/ug835-vivado-tcl-commands/report_qor_suggestions)을 사용했다.

첫 배선은 그대로 두고, `rs_mux_prepare_local`/`rs_mux_placement_v1`에서 보고서가 지목한 `U_READY_STATION/genblk1[1].U_RS_FIFO`의 MUXF 1,536개만 LUT3로 바꾼 배치를 비교했다. [AMD MUXF_REMAP](https://docs.amd.com/r/2022.2-English/ug912-vivado-properties/MUXF_REMAP) 속성을 사용하며 다른 MUXF는 제외한다. `opt_design -muxf_remap`만 실행해 기본 power/BRAM 최적화를 다시 적용하지 않는다. RTL·레지스터 수·BRAM·DSP·클럭 제약은 같고, 배치 전 LUT는 123,096개다.

이 비교 배치의 결과는 LUT 120,877, FF 76,721, Slice 33,128, 예상 WNS +0.158ns다. 기존 배치의 예상 WNS +0.016ns보다 좋아졌고 Slice는 49개 줄었다. 실제 실행 보고서의 예약 영역 제외 사용 가능 Slice는 33,450개이므로 여유는 여전히 작다. 체크포인트를 다시 연 보고서와 실행 중 보고서는 예약 영역 표시가 다를 수 있어, 비율만으로 서로 비교하지 않는다. `board_route_rs_mux_v1`의 중간 WNS는 -1.875ns로 나왔으며, 작은 배치 면적 개선에 비해 타이밍이 나빠져 LRU RAM 비교에 자원을 집중하도록 이 실행을 중단했다. `stopped.json`에 기록했고 최종 배선 실패/통과 어느 쪽의 근거로도 사용하지 않는다.

별도 `../l1-16k-dma3-lru-ram` 후보는 캐시 교체 정보만 분산 RAM으로 저장한다. LRU는 두 way가 모두 유효할 때만 선택에 사용하고 각 line 할당 시 먼저 기록되므로, valid reset 이후 사용 전에 초기 값이 결정된다. 단독 시험에서 후보 LRU를 모두 1로 시작하고 기준 RTL은 reset으로 0으로 시작해도, seed 1/7/23의 모든 CPU handshake·유효 결과·AXI 요청이 매 클럭 일치했다. 캐시 묶음 단독 합성은 LUT 6,037→5,222, FF 2,031→1,485, BRAM36 8개 동일이다. 이 수치를 전체 보드 합성 수치와 섞지 않는다. 새 후보도 전체 RTL 44개 로그와 합성을 통과했다. 현재 폴더의 RTL/기존 증거를 대체하지 않았다.

새 후보의 배치 체크포인트에서 위 출력 enable의 구동 LUT만 12개 추가 복제했다. RTL이나 클럭, 파이프라인 단계는 바꾸지 않았고 DRC는 기존 MIG 5건이다. `board_route_lru_fanout_v1`에서 최종 Setup/Hold·완전 배선·CDC/DRC 감사를 통과하고 비트스트림을 생성했다. 최종 수치는 위의 별도 결과 문서를 따른다.

최종 후보는 배선 후 Setup/Hold·DRC·CDC·완전 배선과 비트스트림의 소스 일치를 확인했다. 실보드 업로드, 새 캐시의 Linux 부팅, 첫 화면 시간과 16/32KiB 성능 비교는 아직 수행하지 않았다.

외부 I/O 제약 범위는 기존 보드와 같다. 비동기 입력은 동기화기까지의 최대 지연을 제한하며, HDMI receiver/PCB의 외부 output delay 예산은 회로도만으로 새로 가정하지 않는다. 내부 STA 통과와 실제 HDMI 수신 검증은 구분한다.

## 원본 증거 위치

- RTL/소프트웨어: 이 variant 및 `validation/`, `soc/reports/`, `provenance/`.
- 전체 합성: `D:/temp/haebancheon-l1-16k-dma3/board_synth_v1`.
- 초기 Setup 실패 배선: `D:/temp/haebancheon-l1-16k-dma3/board_route_v1`.
- 최종 선택 후보의 통과 배선: `D:/temp/haebancheon-l1-16k-dma3/board_route_lru_fanout_v1`.
- SD 및 소프트웨어 검증: `D:/temp/haebancheon-l1-16k-dma3/image`, `software_reports`.
