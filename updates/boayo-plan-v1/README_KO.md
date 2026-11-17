# BoAYO 첫 런처 투영 자료 사전 계산 — plan-v1

첫 런처의 투영 후보 탐색·계수 계산·모양 마스크 계산을 PC 빌드 단계로 옮겼다. `boayo-rawdma-v1`을 보존한 별도 후보다. **새 후보의 실보드 표시 시간과 HDMI 출력은 아직 측정하지 않았다.**

이전 rawdma-v1 실보드에서 Desktop 준비는 약 29.02초였지만 이후 첫 화면 합성 완료는 확인하지 못했다. 이번 변경은 해당 구간의 계산량을 줄이며, 정지 원인이 모두 해결됐다는 뜻은 아니다.

## 처리 순서와 계산량

1. 미리 만든 32비트 `0x00RRGGBB` 런처 텍스처를 읽기 전용 매핑한다. 기존 132개 UI 이미지를 유지한다.
2. 창의 크기·방향·시야각·모양과 실행 소스 버전이 일치하는 투영 자료를 읽는다.
3. 저장된 타일 목록·18워드 디스크립터·경계 마스크를 사용한다.
4. 기존 직접 DMA로 텍스처를 투영코어 DDR 영역에 전송한다. 동일 텍스처가 남아 있으면 재전송을 생략한다.
5. 두 투영코어가 필요한 타일만 처리한다. 모양 마스크가 전부 0인 타일은 생략한다.
6. CPU가 타일을 합성한다. 전부 유효한 타일은 일괄 복사하고 경계 타일만 셀별 마스크를 검사한다.
7. 기존 출력 업로드·commit 경로로 전달한다.

| 기본 런처 항목 | 기존 경로 | 사전 계산 경로 |
|---|---:|---:|
| 보드에서 검사하는 투영 후보 | 4,220 | 0 |
| 보드에서 계산하는 마스크 셀 | 163,840 | 0 |
| 투영코어 작업 | 160 | 96 |
| 전체 유효 타일 | 셀별 검사 | 52개 일괄 복사 |
| 완전히 제외되는 타일 | 투영 수행 | 64개 생략 |
| 경계 타일 | 44개 계산 | 저장된 마스크 사용 |
| 추가 자료 | 없음 | 62,592바이트 |

**투영 작업 40% 감소가 전체 실행 시간 40% 감소를 뜻하지는 않는다.** 파일 읽기·페이지 고정·DMA 대기·장면 버퍼 초기화·투영 결과 복사·CPU 합성·출력 업로드는 남는다. 표의 0은 계수/마스크 산술 계산 횟수다. 자료 검사나 목록 복사까지 없어진 것은 아니다.

## 유지한 기능과 적용 범위

미리 생성한 투영 자료는 **기본 첫 런처 1개 배치**다. 132개 텍스처의 모든 위치와 모양을 미리 생성한 것은 아니다. 같은 배치에서 픽셀만 바뀌면 계수·마스크를 재사용한다. 이동·회전·시야각·모양이 일치하지 않으면 기존 CPU 계산으로 처리하고, 기본 배치로 돌아오면 자료를 재사용한다.

완전히 제외됐던 타일이 드러나면 재투영한다. 창 겹침, 부분 갱신, 숨김·삭제, WASD·클릭, 소프트웨어 렌더러 복귀를 유지한다. 마스크는 기존의 0/1 덮어쓰기이며 새 반투명 알파 합성은 추가하지 않았다.

CPU·DMA·투영코어·출력코어·MIG·HDMI RTL, 비트스트림, SD/DMAengine/BOSIO Linux 드라이버는 변경하지 않았다. 하드웨어 계수 생성기나 마스크 합성기를 추가한 버전이 아니다. RTL 변경이 없어 새 합성·배선은 수행하지 않았다.

## 자료 형식과 무결성

`BOGPLN01`: 4KiB 헤더 + 타일당 84바이트 레코드 + 경계 타일당 1,024바이트 마스크. 빈/전체 유효 타일은 마스크 본문을 저장하지 않는다.

전체 키·소스 지문·길이·메타데이터 및 본문 CRC·타일 순서와 범위·디스크립터 고정 필드·마스크 오프셋과 0/1 값을 검사한다. 손상·누락·버전 불일치 시 기존 계산 경로를 사용한다. CRC는 악의적인 변조를 막는 인증 수단이 아니다.

기하·창·합성·자료 로더 관련 소스 지문을 빌드마다 생성한다. 읽기 전용 공유 매핑으로 창이 참조하는 동안 자료 수명을 보장한다. 실행 중 자료 파일을 수정하거나 잘라내지 않는다. 자료 캐시는 16MiB로 제한한다.

- `BOAYO_PLANS=0`: 기존 계산 비교용.
- `BOAYO_PLAN_DIR`: 자료 경로. 기본 `/usr/share/boayo/plans`, `/data/boayo-plans` 순서.
- `BOAYO_ASSETS=0`: 기존 CPU 이미지 생성 비교용.

## 이미지와 실행

- 이미지: `D:\temp\gen3bosio\boayo-plan-v1\ORYUKDO-KFB-BoAYO-plan-v1.img`
- 커널: `6.12.0-oryukdo-plan1`
- 식별자: `ORYUKDO-BoAYO-plan-v1`
- BusyBox·영구 ext4 `/data`·기존 SD4/DMA 기능 유지.
- 기본 런처 텍스처와 투영 자료는 initramfs와 `/data` 모두에 포함한다.
- 기존 비트스트림: `D:\temp\gen3bosio\sd4-dma\candidate\ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`
- 최종 이미지 크기·SHA-256·검증 판정은 동봉 `result.json`, `SHA256SUMS.txt`를 따른다.

이번 작업에서는 보드를 조작하지 않았다. 다음 실보드 시험 때 전원을 끄고 SD를 기록·검증한 뒤 기존 비트스트림으로 실행한다. SD 전체 기록은 카드의 기존 데이터를 덮어쓴다.

```sh
uname -r
cat /etc/oryukdo-image
boayo-start
boayo-status
```

첫 기본 런처에서 `geometry_tiles_evaluated_total`, `mask_samples_evaluated_total`은 0, `precomputed_layers_total`은 1 이상이어야 한다. 이후 다른 창이나 배치를 사용하면 누적 계산 수가 증가할 수 있다.

`BOAYO_FRAME_TRACE`로 장면 할당 → 자료 로드 → 텍스처 업로드 → 투영 → 합성 → 출력 업로드를 구분한다. `plan.load.end value=1`은 자료 사용이다. `frame.scope_exit`만으로 성공을 판단하지 말고 `output.upload.end`, 출력 commit, 실제 HDMI를 확인한다. 첫 렌더만 추적하며 최대 64개 이벤트다.

## 검증 범위

패키징은 다음 검사 통과를 요구한다. 원본 로그와 판정은 `evidence/`에 포함한다.

- 기존 기능 9개 테스트를 자산·사전 계산 비활성/활성 두 구성에서 실행.
- 이전 rawdma와 10개 장면의 전체 픽셀 비교: 초기, 픽셀 수정, 이동·복원, 가려진 타일 노출·모양 복원, 회전·시야각, 창 겹침·삭제.
- 기본 장면에서 계수/마스크 계산 0, 투영 작업 96개 확인.
- 누락·잘림·CRC·버전·타일 중복·디스크립터·마스크 오류의 복귀 결과 비교.
- 기존 132개 UI 및 장면 비교, ASan/UBSan 메모리 검사.
- 실제 이미지의 RV32 Linux/BusyBox 부팅, 상위 DDR 모의 검사, 두 투영코어 모형, BoAYO 앱 3종, WASD·클릭·셸 복귀.
- RV32의 기존 산술 계산 결과와 PC 생성 디스크립터/마스크 본문을 바이트 단위로 비교.
- SD 부트·CRC·소유권 전환·DMA 읽기/쓰기 RTL 검사. DDR은 모형이다.
- 실행 파일 빌드 소스, 커널의 initramfs, ext4 자료, SD 페이로드 일치 검사.

QEMU의 BOSIO는 명시적인 모형이다. **전체 ORYUKDO CPU RTL Linux→BoAYO→HDMI 실행이나 실보드 속도·물리 HDMI 검증을 대체하지 않는다.** 기존 커널의 `clocksource.c` `pr_fmt` 재정의 경고는 유지하며 빌드 로그에 남긴다.

## 재현

WSL Ubuntu, 기존 RV32 musl 툴체인, Linux 빌드 도구, Verilator, QEMU, Python/numpy가 필요하다. 기반 Linux·설정·OpenSBI·BusyBox는 `/var/tmp/oryukdo-boayo-rgbdma-v1/release`에서 복사한다. 업데이트 ZIP은 기반 전체를 중복 포함하지 않는다.

```sh
cd work/ORYUKDO-3d5i_BOSIO_SoC/updates/boayo-plan-v1
bash build.sh /var/tmp/oryukdo-boayo-plan-v1/release
bash validate.sh /var/tmp/oryukdo-boayo-plan-v1
python3 package.py /var/tmp/oryukdo-boayo-plan-v1 /mnt/d/temp/gen3bosio/boayo-plan-v1
```

기존 Python 버전·rawdma-v1·실보드 로그는 보존했다. 새 후보 실보드 결과는 별도 기록해야 한다.
