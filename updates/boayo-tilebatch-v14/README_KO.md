# HAEBANCHEON BoAYO 타일 부분 갱신·에셋 재사용 v14

2026-11-15 제작. 하드웨어를 유지하고 2D 이미지의 변경 영역과 반복 에셋 전송을 바꾼 SD 이미지다. 로컬 기능 비교와 QEMU Linux·BoAYO 검증을 통과했다. **2026-11-16 실보드 후속에서는 Linux·HDMI 출력은 확인했지만, Telemetry는 31.6%, SDK Pulse는 26.7% 느려졌다. 일반 앱 성능 개선으로 판정하지 않는다.** [실보드 비교·원인 범위](BOARD_RESULT_KO.md)를 참고한다.

## 이미지와 사용 조건

- SD: `D:\temp\haebancheon-tilebatch-v14\HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img`
- 크기: 311,427,072바이트, 297MiB
- SHA256: `cccee8bb4704fa2ec14a4b1f71d882c4d3a541809c0b91c2506467b3850cff30`
- 기존 비트스트림: `D:\temp\haebancheon-l1-16k-dma3\HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit`
- 비트스트림 SHA256: `9bb855886be0ac55d983f9a72ab0e6c23317b7f4fe27e13430cb2de17d7d4e07`
- 대상: PZ-A7200T-KFB, L1I/D 각 16KiB·DMA 3개 구성.

보드를 사용할 때 새 이미지를 SD에 기록·검증하고 기존 비트스트림으로 부팅한다. `cat /etc/oryukdo-image`에 `HAEBANCHEON-BoAYO-tilebatch-v14`가 표시되어야 한다. 11월 15일 제작 중에는 보드에 연결하지 않았고, 11월 16일 사용자 요청으로 실보드 후속 비교를 수행했다.

## 변경 내용

1. 2D 이미지 변경을 32×32 구획으로 관리한다. 구획 전체가 아니라 **실제 변경된 사각형만** 전송한다. 같은 구획에 있는 서로 떨어진 요소 사이의 배경을 변경 영역에 포함하지 않는다. 앱 에셋 조합과 외부 `fill`·`update_surface` 요청에 모두 적용했다.
2. 작은 불변 에셋을 기존 DDR 텍스처 영역에 보관한다. 반복 글자·아이콘은 CPU 사용자 메모리에서 다시 올리지 않고 DDR 내부 DMA 복사로 창 이미지에 넣는다. 에셋당 최대 64KiB, 전체 최대 256KiB이며 기존 4MiB 영역 안에서 동적으로 사용한다.
3. 에셋 복사를 기존 드라이버의 요청 묶음으로 보낸다. 연속 복사는 합치고 최대 128개 단위로 전달한다. 복사 순서와 완료 확인은 유지한다.
4. Dashboard·Telemetry·SDK Pulse의 **앱 본문 외곽을 사각형**으로 변경했다. 공통 이동·크기 조절·닫기 버튼 18개 에셋은 이전 파일과 동일하다. 런처 모양과 내부 위젯 자체의 장식은 유지한다.
5. 새 본문용 에셋 689개와 투영 자료 37개를 만들었다. 본문 에셋 키에 버전을 넣어 예전 둥근 모서리 파일이 잘못 선택되지 않게 했다.

RTL, 투영·출력코어, DMA 하드웨어, MIG, HDMI IP, Linux BOSIO 드라이버/UAPI는 변경하지 않았다. 구면의 변경 타일 재투영·재사용 경로와 메타데이터 전환은 유지한다. 빈 장면을 위해 전체 장면 버퍼를 0으로 채우는 경로를 추가하지 않았다.

## 얼마나 개선됐는가

아래는 호스트의 픽셀 기준 모델로 검증한 전송·요청량이다. **실보드 소요 시간이나 FPS가 아니다. 실보드에서는 두 자동 갱신 앱 모두 성능 회귀가 확인됐다.** 비교본에도 동일한 사각형 본문 변경을 적용해 UI 변경과 최적화의 효과를 분리했다.

| 시험 | 이전 | v14 | 해석 |
|---|---:|---:|---|
| 준비 후 반복 글자 40회 갱신: 사용자 메모리 업로드 | 491,520B | 0B | 캐시에 남은 에셋 재사용 |
| 같은 시험: 업로드 호출 | 320 | 0 | 사용자 페이지 재전송 생략 |
| 같은 시험: 복사 묶음 호출 | 320 | 80 | 묶음 호출 75% 감소 |
| 같은 시험: 위 두 종류 호출 합계 | 640 | 80 | 호출 87.5% 감소, DDR 내부 복사는 남음 |
| 대각선으로 맞닿은 32개 요소: 사용자 업로드 | 1,081,344B | 1,024B | 넓은 배경까지 갱신하던 합치기 제거 및 에셋 재사용 |
| 서로 다른 앱 그림 12회 갱신: 사용자 업로드 | 460,800B | 460,800B | 이 작업에서는 감소 없음 |

단발 에셋도 캐시에 넣으므로 처음 사용하는 에셋에는 캐시 관리와 DDR 복사 비용이 추가될 수 있다. 반복 사용 빈도와 캐시 공간에 따라 실제 이득이 달라진다. 첫 화면은 기존에도 사전 계산 자료를 쓰므로 이번 변경으로 첫 화면까지 같은 비율로 빨라진다고 말할 수 없다.

모서리만 사각형으로 바꾼 이동 장면 비교에서는 곡선의 정밀 판정이 284회에서 0회로 줄었지만, 마스크를 계산한 타일은 양쪽 모두 82개였다. 사각형도 구면 타일을 가로지르고 창끼리 겹칠 수 있어 경계 합성은 남는다. **창 이동의 투영계수·경계 계산 병목을 모두 해결한 버전은 아니다.**

## 검증

자세한 범위와 원본 증거는 [검증 결과](RESULTS_KO.md), 동작 순서는 [구조 설명](ARCHITECTURE_KO.md)에 있다.

- host/RV32/ASan·UBSan 빌드의 소스 47개 해시 일치.
- 120개 장면·상태 결과 비교 통과: 창 이동·크기 변경·겹침·닫기, 피치·롤·요, 에셋 없는 CPU 경로, 앱 부분 갱신 포함.
- 무작위 변경 영역 3,000건, 51,063,722픽셀 커버리지 비교 통과.
- 캐시 퇴출·겹치는 복사·12개 실패 위치에서 오류 후 슬롯 무효화 통과.
- 689개 에셋의 픽셀 비교, 공통 버튼 18개 파일 동일성 통과.
- SD 파티션·CRC·ext4 검사, 커널 내 initramfs와 최종 RV32 실행 파일·에셋·투영 자료 일치 통과.
- QEMU에서 실제 SD의 OpenSBI·커널·DT payload로 Linux 쉘, 상위 메모리 검사, BoAYO 런처·3개 앱·키 입력 통과. BOSIO는 Mock Backend다. FPGA RTL/SD 컨트롤러/실제 HDMI 검증을 의미하지 않는다.

## 다시 빌드하는 순서

WSL Ubuntu에서 저장소 루트를 현재 디렉터리로 두고 실행한다. 기존 툴체인·Linux·v13 rootfs 작업 디렉터리가 필요하며 `build_image.sh`의 `BASE_BUILD`, `PREVIOUS_BUILD`, `IMAGE_BUILD`로 경로를 지정할 수 있다.

```bash
v="$PWD/updates/boayo-tilebatch-v14"
w=/var/tmp/haebancheon-tilebatch-v14-rebuild
bash "$v/software/boayo/native/build.sh" "$w/host" host
bash "$v/software/boayo/native/build.sh" "$w/rv32" rv32
python3 "$v/tests/make_assets.py" "$w/host/boayo-native-host" "$w/assets"
python3 updates/boayo-motion-v7/tests/make_plans.py "$w/host/boayo-native-host" "$w/plans"
IMAGE_BUILD="$w/release-final" bash "$v/build_image.sh" "$w"
python3 "$v/tests/verify_build.py" "$w/release-final" /var/tmp/haebancheon-l1-16k-dma3/release "$w/audit.json"
python3 "$v/tests/test_image.py" "$w/release-final/image/HAEBANCHEON-KFB-BoAYO-tilebatch-v14.img" "$w/qemu"
```

최적화 비교용 기준 소스는 v13 native 소스 복사본에 `tests/reference-ui.patch`를 `patch -p1`로 적용해 재현한다. 기존 v13은 직접 수정하지 않는다. 로컬 회귀 시험은 기존 `boayo-incremental-v11/tests/run_regressions.sh`에 `BASELINE_BINARY`, `CANDIDATE_BINARY`, `ASSETS`, `PLANS`를 지정하며, 추가 시험은 `tests/run_local_extra.py`, `tests/run_sanitized.py`, `tests/test_rect_assets.py`를 사용한다. 각 실행의 정확한 소스·바이너리 해시는 `evidence/*-build.json`에 보관했다.

이번 변경은 로컬 파일만 작성했으며 Git 커밋·푸시는 하지 않았다.
