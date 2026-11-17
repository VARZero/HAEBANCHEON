> **HDMI IP 준비:** 이 구성도 [공통 외부 다운로드 방식](../../docs/외부_HDMI.md)을 사용한다. 저장소 루트에서 `python scripts/prepare_current_hdmi.py --all` 실행 후 빌드한다. IP 본문은 Git에 포함하지 않는다.

# 해반천 — L1I/D 16KiB·2Way, DMA 3개

> **2026-11-17 게시 안내:** 이 폴더가 현재 비트스트림의 RTL 기준이다. [외부 HDMI IP 준비·MIG 생성·구조와 재현](../../docs/현재_비트스트림.md), [후속 v14 실보드 결과](../../updates/boayo-tilebatch-v14/BOARD_RESULT_KO.md)를 확인한다. 아래의 “실보드 미확인”은 최초 제작 당시 기록이다. 이번 게시에서 RTL 또는 기존 결과 해시는 변경하지 않았다.

`../l1-16k-dma3`와 동일한 I/D 각각 16KiB·2Way·DMA 3개 구성이다.
캐시 LRU를 reset FF 대신 작은 분산 RAM으로 구현하고, 실제 배선 위반을 일으킨 출력 enable의 구동 회로를 분산했다. 전체 보드 배선·비트스트림 생성은 PASS다. [최종 결과·임계 경로·한계](RESULTS_KO.md)를 확인한다.
LRU는 두 way가 유효할 때만 교체 선택에 사용되고, 각 라인 할당이 그 전에 LRU를 기록한다.
따라서 reset 때는 기존대로 valid를 지우고 LRU payload는 초기화하지 않아도 된다.

단독 비교에서 기존 RTL과 모든 클럭의 AXI 요청·응답·CPU 결과가 일치했다(seed 1/7/23).
이 후보도 통합 RTL 44개 로그가 PASS이며, 기존 후보와 PASS 행의 실행 클럭·관측 결과가 모두 일치한다.
기존 후보의 검증 로그를 복사하지 않고 다시 실행했다. 실제 보드 동작은 아직 확인하지 않았다.

전체 보드 합성 직후 LUT 121,319, FF 76,558, BRAM 110타일, DSP 62다.
`opt_design` 이후에는 LUT 120,680, FF 76,193이다. 모두 배선 전 수치다.
기존 16KiB 후보보다 BRAM이 0.5타일 늘어난 부분은 CPU AXI CDC의 R 응답 FIFO가 RAMB18 대신 RAMB36으로 매핑된 데서 왔다.
해당 CDC RTL은 동일하며, 캐시 데이터 BRAM36은 I/D 각각 4개다.

합성 후 CDC Warning/Critical은 0, DRC는 기존 MIG의 BUFC-1 4건과 REQP-1709 1건이다.
CPU 50MHz·fabric 100MHz·setup uncertainty 0.3ns를 유지한다.
`board_route_lru_v1`의 배치를 보존하고, 초기 16KiB의 실제 배선에서 9.55ns net 지연을 만든 출력 enable 구동 LUT를 12개 추가 복제했다.
해당 신호의 3,841개 부하를 분산하는 `prepare_output_fanout.tcl`을 사용하며 RTL·파이프라인 단계는 동일하다.
[AMD의 지정 net 복제 방법](https://docs.amd.com/r/2022.1-English/ug949-vivado-design-methodology/Use-Physical-Optimization)에 따른 물리 최적화다.
`output_fanout_prepare/fanout_optimized.dcp`에서 `route_from_placed.tcl`로 배선했다.
최종 LUT 119,141 / FF 76,182 / BRAM 110 / DSP 62, WNS +0.093ns / WHS +0.011ns, TNS/THS 0이다.
MIG·DDR 1GiB·HDMI2·SD4·두 투영코어·출력코어를 모두 포함한다. Slice 사용률은 예약 영역 제외 99.05%로 높다.

실행 소프트웨어·DT 20개 소스는 기존 16KiB SD 이미지 빌드와 해시가 같다.
따라서 `HAEBANCHEON-KFB-L1-16K-DMA3-v1.img`를 함께 사용한다.
그 이미지의 QEMU/이미지 감사 결과를 재사용하며, LRU 후보에서 QEMU를 다시 실행했다고 주장하지 않는다.
관련 근거는 `provenance/validation_summary.json`, `lru_comparison.json`, `software_compatibility.json`이다.

## 구조와 재현

CPU → Sv32 물리 주소 → L1I/L1D → 기존 AXI CDC → 7-master fabric → AXI–MIG adapter → native arbiter → DDR 순서다. 두 투영코어, 출력코어, DMA 3개가 fabric을 공유한다. 영상 scan 경로는 native arbiter에서 DDR을 읽어 공식 HDMI2 IP로 출력한다. 캐시와 DMA 주소·일치 정책의 자세한 설명은 [공통 16KiB 설계](../l1-16k-dma3/README.md)를 따른다. 이 최종 구성에서는 LRU payload를 reset하지 않는 차이가 있다.

- WSL: `bash software/storage/test_all_l1.sh`, `bash software/storage/test_lru_equivalence.sh`.
- 합성: `build_area_medium_synth.tcl`에 출력 폴더와 동일 MIG XCI를 전달한다.
- 배치: `place_only.tcl`에 합성의 `optimized.dcp`와 출력 폴더를 전달한다.
- 국소 fanout 보완: `prepare_output_fanout.tcl`에 `placed_optimized.dcp`와 출력 폴더를 전달한다.
- 배선: `route_from_placed.tcl`에 `fanout_optimized.dcp`와 출력 폴더를 전달한다.
- 최종 감사: `collect_board.py ROUTE_DIR SYNTH_DIR PROVENANCE_DIR FANOUT_OPTIMIZED_DCP`.

모든 Tcl은 `soc/board/`에 있다. 원본 실행에서는 `route_guarded.tcl`이 생성한 배치 체크포인트에서 이어갔다. `place_only.tcl`은 같은 배치·DSP 보호·제약을 적용하고 route 직전에 멈추는 재현용 진입점이다. 배치의 PASS를 최종 배선 PASS로 간주하지 않는다.

비트스트림과 SD 경로·해시는 `provenance/release_manifest.json`, 구현 체크포인트 연결은 `implementation_chain.json`, 원본 리포트는 `reports/`에 있다. 실보드 업로드·Linux 부팅·HDMI·첫 화면 시간은 아직 측정하지 않았다.
