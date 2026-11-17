# BOSIO 버퍼 복사·초기화 DMA 연결 — v1

2026-11-10 소프트웨어 후속 후보이다. SD4 하드웨어에 이미 있는 DDR 복사/채우기 DMA를 Linux BOSIO 드라이버와 BoAYO에 연결했다. **보드는 꺼진 상태이며 이 후보의 실보드 DMA·HDMI 시험은 아직 하지 않았다.** 기존 RTL, MIG, HDMI IP, 스케줄러 및 비트스트림은 변경하지 않았다.

## 적용 내용

| 작업 | 이번 경로 |
|---|---|
| 4KiB 이상 `BO_UPLOAD` | 사용자 데이터를 임시 버퍼에 먼저 복사한 다음 SG DMA로 BOSIO DDR 영역에 전달 |
| 4KiB 미만 업로드 | 기존 CPU 복사 유지 |
| 화면 버퍼 동기화 | 비활성 버퍼에 한 번 업로드 → 프레임 경계 전환 완료 → DDR DMA로 반대 버퍼에 변경 범위 복사 |
| 투영 작업용 SCRATCH 초기화 | 새 클라이언트가 열 때 DMA로 256KiB 0 채우기 |
| 일반 버퍼 복사/초기화 | `BO_MEM_COPY`, `BO_MEM_FILL` ioctl 제공 |
| 상태 조회 | `BO_GET_DMA_INFO`, `boayo-status`의 `output.memory_dma` |
| SD 파일시스템 | SD4 Linux v2의 4KiB 요청 분할·ext4 수정 포함 |

빈 첫 장면은 기존처럼 **4,480워드 메타데이터만** 업로드한다. 전체 화면을 투영코어로 지우지 않는다. 고정 기하 사전 계산, BPT2 변경 타일, 두 투영코어, 런처·내장 앱·UART WASD/클릭 기능을 유지한다.

DMA가 CPU 작업을 전부 대체하는 것은 아니다. 사용자→커널 임시 버퍼 복사, 작은 업로드, `BO_DOWNLOAD`는 CPU가 처리한다. 커널의 표준 `dma_alloc_coherent()`가 수행하는 최초 16MiB 초기화도 유지했다. 따라서 이 변경만으로 모든 초기 지연이 없어졌다고 볼 수 없다.

## 메모리 소유권과 완료 처리

```text
BoAYO 사용자 버퍼
  └─ BO_UPLOAD: 사용자 메모리 전체 검증·복사
       └─ dma_map_sg → 기존 oryukdo-dma → BOSIO 예약 DDR
            └─ IRQ 완료 → 매핑 해제 → ioctl 반환

화면 변경 범위 → 비활성 bank 업로드
  → BO_PRESENT 프레임 경계 완료
  → 활성 bank 읽기 → DMA → 반대 비활성 bank 쓰기
```

- 기존 ABI 3 레이아웃·ioctl은 보존하고 0x47/0x48/0x49를 추가했다. 구형 커널이 DMA 조회에 `ENOTTY`를 반환하면 BoAYO는 기존 이중 업로드로 동작한다.
- 목적지는 BOSIO arena 내부, 4바이트 정렬 범위로 제한한다. 표시 중인 bank 쓰기와 겹치는 복사는 거부한다. 활성 bank 읽기는 허용한다. Fill 값은 0..255의 반복 바이트다.
- 하나의 장치 mutex가 업로드·복사·채우기·투영·표시를 직렬화한다. 제출/승인을 완료로 취급하지 않고 DMAengine 완료 콜백과 결과를 기다린다.
- 30초 시간 초과 시 장치를 격리한다. 이미 승인된 AXI 접근은 강제 취소할 수 없으므로 임시 버퍼·SG 매핑·콜백 문맥을 보존한다. 늦은 완료가 와도 자동 재사용하지 않으며 새 작업은 차단한다. 재사용하려면 보드/시스템 재시작이 필요하다.
- 일부 SG 전송이 완료된 뒤 실패해도 부분 상태를 정상 화면으로 사용하지 않는다. 완료된 오류는 메모리 해제 가능 여부를 구분하고, 진행 중인 전송은 보존한다.
- DTS의 `dmas = <&storage_dma 0>`로 기존 단일 DMAengine 채널을 BOSIO가 장치 수명 동안 점유한다. 다른 DMAengine 클라이언트와 동시 대여하지 않는다. SD 드라이버는 이 Linux 채널을 대여하지 않으며 기존 SD 전송 경로를 사용한다.

## 산출물

- SD: `D:\temp\gen3bosio\bosio-dma-v1\ORYUKDO-KFB-BOSIO-DMA-v1.img`
- 크기: 276,824,064바이트(264MiB), `/data` ext4 256MiB
- SD SHA-256: `2039a806c008e15ee5282e132b969bfaebf113f4c7cbee4b0b89a9288f41e92c`
- 커널: `6.12.0-oryukdo-bosio-dma1`
- 비트스트림: 기존 `D:\temp\gen3bosio\sd4-dma\candidate\ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`

현재 SD의 `/data`에는 사용자가 만든 파일이 있을 수 있다. 전체 SD 이미지 기록은 해당 내용을 덮어쓰므로 필요 파일은 먼저 백업한다. 꺼진 보드를 지금 켤 필요는 없다.

## 검증과 한계

| 검사 | 결과 및 범위 |
|---|---|
| 변경 전 기준 BoAYO | 호스트 9개 회귀 검사 PASS |
| 변경 후 BoAYO | 일반 빌드 9개, ASan/UBSan 빌드 9개 PASS |
| CPU 복사 대비 DMA 결과 | 두 bank 전체 바이트 일치: 메타데이터 빈 화면·전체 장면·32회 BPT2 변경 PASS |
| 실제 드라이버 함수의 호스트 검사 | kmalloc/vmalloc SG, 경계·정렬·겹침·활성 bank 보호, prep/submit/completion 오류, 부분 SG 실패, 시간 초과 후 늦은 완료 PASS |
| 기존 제어 엔진 | 28개 검사 PASS |
| RV32 커널/앱 | 빌드 PASS, 내장 initramfs·앱 스냅샷·드라이버·DTB·SD payload 해시 일치 |
| SD 파일 | CRC·MBR 비중첩·ext4 fsck PASS |
| SD RTL | 실제 새 boot payload의 3개 segment 적재/CRC 및 런타임 SD/DMA 왕복 PASS; mock DDR 사용 |
| QEMU | 정확히 이 이미지의 Linux 셸·상위 DDR 검사·BoAYO 자체 검사·런처·3개 앱·WASD/클릭 PASS. BOSIO 모의 장치이며 실제 SoC·DMA·HDMI 검증은 아님 |
| 실보드 | 이번 후보는 미실행. 기존 SD4 Linux v2의 `/data` PASS와 구분 |

CPU 복사/DMA 비교 시 업로드는 **34,872 → 17,436워드**, 대신 DDR DMA 복사 17,436워드였다. 이는 해당 시나리오의 업로드량 비교이며 실행 시간 50% 단축 측정이 아니다. 실보드 시간·프레임률은 미측정이다.

처음 회귀 검사에서 `dual-selftest`의 과거 이중 업로드 개수 기대값이 실패했다. 화면 데이터는 일치했고, 검사를 새 계약인 업로드 1회+DMA 복사 1회로 수정한 뒤 재통과했다. QEMU 첫 실행은 새 검사 도구의 문구 기대값(`STORAGE_CHECK`/실제 `IDENTITY`) 오류로 실패했으며, 이 검사만 수정했다. 실패 로그도 보존한다.

새 합성·배선은 수행하지 않았다. RTL/비트스트림이 같으므로 기존 SD4 구현 결과(LUT 114,075, FF 74,189, BRAM 101.5, DSP 62, CPU 50MHz, WNS +0.034ns, WHS +0.027ns)를 그대로 사용한다. 새로운 측정 결과로 제시하지 않는다.

## 보드를 다시 켠 후

새 이미지를 기록·검증하고 기존 SD4 비트스트림으로 부팅한 뒤 다음 순서로 확인한다. `/dev/bosio0`는 독점이므로 BoAYO를 실행하기 전에 진단한다.

```sh
uname -r
cat /etc/oryukdo-image
bosio-dma-check
cat /proc/interrupts
storage-check
boayo-start
boayo-status
boayo-keys
```

`bosio-dma-check`는 SCRATCH에서 4KiB 업로드·복사·0x5a 채우기를 읽어 비교하며, 겹침 거부와 DMA 완료 통계를 검사한다. PLIC 2번 DMA IRQ 증가도 UART로 확인한다. 이어 실제 HDMI 화면·WASD·중앙 클릭과 `/data` 읽기·쓰기를 확인한다. `storage-check`는 최소 BusyBox에 없는 `dd`/`sha256sum` 없이 동작하는 네이티브 도구이며 검사 도중 `/data`를 정상 해제/재마운트할 수 있으므로 다른 사용자는 파일을 닫아야 한다.

## 소스와 재실행

`software/bosio/`는 수정된 드라이버·공통 엔진·UAPI·DTS, `software/boayo/native/`는 기존 Python 비교 기준을 포함한 C++ 실행 환경이다. `tests/`에는 실제 함수 경계 모델, 출력 비교, 실보드 진단, 이미지 부팅 및 산출물 감사가 있다. 상속한 BoAYO v2 문서는 과거 기준 설명이며 이번 변경은 이 문서를 우선한다.

WSL Ubuntu에서 저장소 루트를 기준으로:

```sh
bash updates/bosio-dma-v1/build.sh /var/tmp/oryukdo-bosio-dma-v1/release
python3 updates/bosio-dma-v1/tests/run_driver.py
bash updates/bosio-dma-v1/tests/run_output.sh
python3 updates/bosio-dma-v1/tests/verify_build.py \
 /var/tmp/oryukdo-bosio-dma-v1/release /var/tmp/oryukdo-sd4-linux-v2 /tmp/bosio-audit.json
python3 updates/bosio-dma-v1/tests/test_image.py \
 /var/tmp/oryukdo-bosio-dma-v1/release/image/ORYUKDO-KFB-BOSIO-DMA-v1.img /tmp/bosio-qemu
```

빌드 입력은 `/var/tmp/oryukdo-sd4-linux-v2`의 Linux 전체 트리·설정·root·OpenSBI다. 해당 경로는 `BASE_BUILD`로 바꿀 수 있다. Linux용 `riscv64-linux-gnu-`와 앱용 `/var/tmp/oryukdo-boayo/runtime/host/bin/riscv32-buildroot-linux-musl-*`, GCC/G++, dtc, e2fsprogs, QEMU가 필요하다. 완전히 빈 PC에서 실행하는 자동 설치 스크립트는 아니다. 복사한 원본 SD4 소스·원격 릴리스는 이번 작업으로 덮어쓰지 않았다.
