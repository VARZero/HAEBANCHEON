# HAEBANCHEON — 변경 항목 상태 갱신·드라이버 계측 v11

현재 상태: **실보드 v11 부팅·드라이버 비교 완료. 정상 Telemetry 갱신은 2.537초/회. 추가로 발견한 본문 호버 에셋 누락은 [fix1](../boayo-incremental-v11-fix1/README_KO.md)에서 수정했다. 창 이동 시 CPU 마스크 생성 병목은 남아 있으며 전체 조작 시험 통과가 아니다.** [실보드 측정 결과](../boayo-incremental-v11-fix1/BOARD_RESULT_KO.md).

## 변경 사항

`boayo-frame-v10`은 비교용으로 그대로 보존했다. `software/`는 이번 수정본 전체이며 RTL·FPGA 비트스트림은 변경하지 않았다.

- 창 데이터·포커스·순서를 비교해 변경된 창만 공개 상태에 갱신한다. 내용 픽셀만 바뀌면 창 목록과 포인터 상태를 다시 생성·복사하지 않는다.
- 포인터 좌표·기하·입력·버튼·캡처·스크롤 변화는 즉시 반영한다. 상태 조회의 기존 전체 JSON 형식은 유지하고, 내부 게시만 변경 항목 단위로 처리한다.
- 작업 공간은 이미 보관한 상태를 읽는다. 상태 게시 시 새 전체 창 목록을 만들어 전달하던 복사를 제거했다.
- 커널 드라이버에 선택적 누적 계측을 추가했다. 요청 복사, 전체 요청 검사, 마스크 해석, CPU 워드 처리, DMA, 메타데이터 차이 탐색·전송·비교, MMIO 전환 명령, 완료 폴링·잠자기·마무리를 구분한다.
- 경계 마스크에서 64바이트 이하의 온전히 선택된 연속 구간은 정렬된 CPU 워드 복사/채우기로 처리한다. 큰 연속 구간은 DMA를 유지한다. 부분 워드는 기존처럼 선택된 바이트만 바꾼다. 이는 실제 보드에서 기존 경로와 비교할 최적화 후보다.
- CPU 부분 처리의 메모리 순서 보장은 작업 시작·DMA 제출 전·완료 시 유지한다. DMA와 CPU 처리의 완료를 확인한 뒤에만 출력 전환한다.
- 출력 메타데이터의 동일성은 정렬된 32비트 워드로 정확히 비교한다. 값이 동일하면 같은 CPU 스냅샷을 다시 복사하지 않는다. 해시 추정이나 완료 응답 생략은 하지 않는다.
- 기존 active bank 쓰기 방지, 전체 batch 사전 검사, 오류 후 poison, timeout 후 DMA 메모리 유지 규칙을 보존했다.

## 계측·비교 실행

새 드라이버는 기존 ABI3 요청 구조를 바꾸지 않고 `BO_PROFILE_CONTROL`/`BO_GET_PROFILE`을 추가한다. 계측은 기본적으로 꺼져 있다. `BOAYO_PROFILE=1`로 실행하면 `boayo-native profile`의 `driver_profile`에 누적 마이크로초·호출 수가 나온다. 부모 구간과 자식 구간은 중첩되므로 전부 더하지 않는다.

`BOAYO_DRIVER_LEGACY=1`은 같은 v11 앱에서 드라이버의 기존 경계 처리·메타데이터 비교 경로를 선택한다. 기본 실행은 새 처리 경로다. 두 모드 모두 새 드라이버의 동일한 계측을 사용해 비교할 수 있다. 새로운 ioctl이 없는 이전 커널에서도 앱은 실행되지만 `driver_profile.available=false`이며 새 드라이버 최적화는 적용되지 않는다.

`/usr/bin/boayo-native-v10`도 SD에 보존했다. 이 파일을 실행해도 커널은 v11이므로, 이전 SD 전체와 동일한 구성이라고 간주하면 안 된다.

## 검증 결과

- v10 대비 120개 장면의 픽셀 일치: 일반 장면, 혼합 시점·창 이동/크기/닫기, 에셋 없는 CPU 경로, 앱 갱신, 공통 버튼 최소화/복원.
- 상태 250개 원본 비교 및 변경 항목 적용 후 전체 상태 일치. 변화 없는 창·포인터를 다시 게시하지 않는 검사.
- 실제 IPC 서버의 생성, 메타데이터, 픽셀, 숨김/표시, 포커스, 캡처, 삭제, 연결 해제 후 상태 반영.
- 텍스처 풀 100개 임의 조합·60개 CPU 편집, 건너뛴 갱신·퇴출·오류 처리, ASan/UBSan.
- 드라이버 경계 마스크 2,048개: 기존/새 경로 모두 바이트 기준 결과·경계 canary 일치. 전체 batch 검사, active bank 보호, 부분 실패·timeout poison 검사.
- 기존 DMA 준비/제출/완료 실패, 산재 페이지, RGB 480개·직접 업로드 36개, 늦은 DMA의 메모리 수명 검사.
- 실제 worker의 처리 시간을 포함한 2fps 주기와 시점 재생/중단/복구 검사.
- 이미지의 소스·UAPI·실행 파일·initramfs·커널·SD payload CRC·파티션·ext4·에셋 동일성 감사.
- QEMU RV32IMA: 해당 이미지의 Linux/BusyBox 셸, 이미지 식별자, 상위 DDR 검사, BoAYO 자체 검사. **QEMU의 BOSIO는 Mock이며 FPGA 구동 증거가 아니다.**

## SD 및 실행 범위

- 파일: `D:\temp\haebancheon-incremental-v11\HAEBANCHEON-KFB-BoAYO-incremental-v11.img`
- 크기: 311,427,072바이트.
- SHA-256: `5df06750b6a5a419aac07bf127133fda6b2fd793b24e69b00b146f9c6b5099af`.
- 앱 SHA-256: `cb542ae1df9ae4f2ea7af736e4ab8fa0bcf12eaf5eed0e3f7410f08b1e89b929`.
- 드라이버는 커널 내장형이다. 현재 커널은 모듈 교체·kexec가 꺼져 있어 새 드라이버 검증에는 새 SD 부팅이 필요하다.
- 기존 `HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit`를 그대로 사용한다. 새 합성·배선이나 비트스트림 변경은 없다.
- 이 v11 SD로 실보드 부팅과 드라이버 비교를 수행했다. 현재 보드는 fix1 앱을 RAM에서 실행하며, 다음 부팅용 이미지는 fix1 README를 참조한다. v11 원본과 실패 로그는 보존한다.

## 재현

`software/boayo/native/build.sh`로 `host`, `rv32`, `sanitize`를 각각 빌드한다. `tests/run_regressions.sh`, `tests/run_sanitize.sh`, `tests/run_driver.py`, `tests/test_incremental_service.py`가 기능 검증이다. 빌드 명세에 기록된 스냅샷의 소스/오브젝트를 사용한다.

`build_image.sh`는 기존 Linux를 별도 디렉터리에 복사하고 새 BOSIO 드라이버만 반영한다. BusyBox·부트 펌웨어·689개 그림 에셋·37개 투영 계획·기존 DT를 유지한다. `tests/verify_build.py`와 `tests/test_image.py --boot-only`로 산출물과 부팅을 확인한다. 실제 board backend 측정은 별도 UART 증거로 남긴다.

커널 빌드에는 기존 `kernel/time/clocksource.c`의 `pr_fmt` 재정의 경고가 남아 있다. 이번 BOSIO 수정의 컴파일 오류는 없으며, 이 소프트웨어 빌드를 Vivado DRC 결과와 혼동하지 않는다.
