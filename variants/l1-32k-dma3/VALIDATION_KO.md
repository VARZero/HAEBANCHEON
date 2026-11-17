# L1 32KiB 2Way 및 DMA 3개 검증

## 검증 계층

| 계층 | 실행 | 확인 대상 |
|---|---|---|
| 캐시 단독 RTL | `software/storage/test_cache.sh` | I/D 각각 1024 line, 2-way/LRU, 부분 쓰기, DMA 무효화, refill 충돌, 독립 응답, 오류, 4000회 무작위 접근 |
| 실제 CPU RTL | `software/storage/test_cpu.sh` | FENCE.I 자기 수정 코드, AMO, 캐시된 DMA 목적지, 3개 DMA 동시 요청, 각 엔진 M/S IRQ, SD read/write |
| 실제 CPU + 보드 DDR adapter | `KFB_NATIVE_BACKEND=1 software/storage/test_cpu.sh` | 위 항목과 실제 AXI–MIG adapter/native arbiter, mock DDR의 `0xbffff000..0xbffffffc` 읽기·쓰기 |
| 실제 CPU + BOSIO 실코어 | `KFB_BUILD_DIR=/var/tmp/haebancheon-l1-bosio-integration soc/tools/test.sh` | 단색/패턴 장면, 두 투영코어의 768워드 쓰기·snoop, 출력 픽셀·역압·SOF/EOL; native backend도 별도 실행 |
| 기존 저장장치 RTL | `software/storage/test.sh` | SD4/CRC/timeout/지연 입력, 부트로더, 잘못된 이미지 거부, DMA copy/fill/4KiB 경계/원자 잠금, PLIC |
| Linux/BoAYO | `software/storage/test_l1_image.py` | 새 SD의 BusyBox shell, 메모리 검사, BoAYO 앱/첫 장면/WASD/click; QEMU와 Mock BOSIO |
| 이미지 바이트 검사 | `software/storage/audit_l1_image.py` | SD 헤더·CRC·펌웨어 일치, ext4 검사, 기존 v5 앱/데이터 보존, 캐시 및 세 DMA DT |
| 보드 전체 구현 | `soc/board/build_area_medium_synth.tcl`, `route_guarded.tcl` | MIG/HDMI 포함, `xc7a200tfbg484-2`, 기존 CPU 50MHz/fabric 100MHz 및 0.3ns 여유 유지 |

RTL 무작위 검증은 seed 1, 7, 23을 사용한다. CPU 시험은 실제 ORYUKDO 명령 실행을 사용한다. QEMU는 이 CPU RTL을 실행하지 않으며, Mock의 첫 프레임 완료는 실제 HDMI 화면 검증이 아니다. 새 하드웨어의 Linux 콜드 부팅 및 실보드 첫 화면 시간은 아직 측정하지 않았다.

## 이번 작업에서 발견하고 수정한 항목

- FENCE 명령의 원래 instruction bit가 실행 단계까지 보존되지 않아 `FENCE.I` 식별이 불가능했다. 기존 ordered system completion에서 I-cache 무효화를 발생시키도록 연결했다.
- 캐시 Fetch 응답을 CPU가 소비하지 못하는 동안 Load까지 막히지 않도록 I/D 상태와 응답을 분리했다.
- refill 도중의 무효화가 늦게 완료된 refill에 의해 취소되지 않도록 installation kill 상태를 추가했다.
- 512-bit valid/LRU 벡터의 동적 쓰기를 set별 clock enable로 바꿔 불필요한 선택 회로를 줄였다.
- 전체 합성 DRC에서 새 캐시 BRAM 16개에 REQP-1839가 확인됐다. AXI 응답 valid의 비동기 reset gate가 BRAM WE로 전달되는 경로다. refill 쓰기 데이터·주소·enable을 동기 레지스터에 저장한 뒤 BRAM에 쓰도록 수정했다. 캐시 payload 전체 초기화는 추가하지 않았다.
- 일반 합성 설정은 LUT 초과를 일으켰다. 기준 SD4 보드와 같은 `AreaOptimized_medium` 합성 설정으로 재실행한다. 클럭이나 RTL 기능을 낮춘 결과와 혼동하지 않는다.
- MIG XCI 파일만 복사하면 기존 상대 경로를 찾지 못했다. 원본과 동일한 `mig_a.prj`로 독립 프로젝트에서 공식 MIG를 생성했다. IP 내부는 수정하지 않았다.
- 첫 배치에서 출력코어 센서 좌표 계산의 `product_lane[1].mul/p11_reg` DSP 입력 AREG가 풀려 DPIP-1 한 건이 발생했다. 이를 보호하자 다음 배치에서 같은 곱셈기의 `p10_reg`로 변환이 이동했다. 이 32×32 곱셈기를 구성하는 부분곱 DSP 4개에 한정해 `DONT_TOUCH`를 지정했다. RTL 변경이나 경고 면제는 아니다. 이후 실행은 직전 배치를 참조하는 incremental 구현을 사용하며, 배선 후 전체 Setup/Hold·DRC·CDC 검사를 생략하지 않는다.

## 원본 보존과 로그 해석

`software/storage/audit_sources.py`는 스케줄러 `rtl/`, `gen2/`, BOSIO vendor, HDMI IP 내부, MIG 설정이 기준과 같음을 확인하고 source SHA256 목록을 저장한다. 보드 구현과 검증 결과는 이 목록으로 연결한다.

실패한 첫 시도도 삭제하지 않는다. `l1_seed.log`는 RTL 오류가 아니라 잘못 전달된 빈 seed 인수로 실행조차 시작하지 못한 기록이다. 정식 seed별 로그와 구분한다. `board_synth_v2`는 일반 합성의 면적 초과와 BRAM 경고를 보존한 실패 후보다. 그 로그의 합성 완료 문자열은 보드 구현 PASS를 뜻하지 않는다.

검증 펌웨어 linker의 RWX LOAD 경고는 bare-metal 시험 코드의 linker script에 관한 것이다. AMD XPM의 시뮬레이터 assertion 지원 안내도 기록된다. 정상/오류 응답 및 CDC 순서는 별도의 RTL assertion과 scoreboard로 검사한다. 물리 CDC/DRC는 Vivado 보고서로 별도 판정한다.

Incremental 참조를 읽을 때 `Vivado_Tcl 4-426`은 보호한 `p10_reg`의 이전 register-unpack 최적화를 재현하지 못했다는 뜻이다. 바로 앞 `Physopt 32-896`이 `DONT_TOUCH` 때문에 해당 변환을 제외했음을 명시한다. 이 메시지를 숨기지 않고, 현재 설계의 실제 DSP AREG 및 DRC를 별도로 확인한다. 참조는 배치까지만 진행됐으므로 재사용할 배선이 없다는 메시지도 예상된다.

`route_timing_closure.tcl`은 참조 결과의 음수 WNS를 목표로 삼는 기본 `RuntimeOptimized` 대신 WNS=0을 목표로 하는 `TimingClosure`를 선택한다. 타이밍 제약을 변경하는 옵션이 아니다. 근거는 [AMD Vivado 2022.2 read_checkpoint 문서](https://docs.amd.com/r/2022.2-English/ug835-vivado-tcl-commands/read_checkpoint)다. `board_route_v5`는 이 모드 선택을 바로잡기 위해 배선 전에 중단한 실행이다.
