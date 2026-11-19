# 보아요 WM 데몬과 프로세스별 앱

보아요 WM 한 프로세스가 창·입력·가림 순서와 보시오의 화면 게시를 관리한다. 런처와 Dashboard·Telemetry·SDK Pulse는 각각 별도 Linux 프로세스에서 그림을 만든다. 같은 실행 파일을 프로세스별로 사용하며 WM의 렌더 스레드에서 앱 코드를 주기적으로 실행하지 않는다.

## 시작과 종료

```sh
boayo start
boayo status
boayo launcher
boayo app dashboard
boayo app pulse
boayo bench-small 21
boayo stop
```

`boayo start`는 WM을 시작한다. 런처는 `boayo launcher`로 연다. `bosio-tiles start`도 같은 WM과 소켓을 사용하며, 이미 실행 중인 WM에 `boayo start`를 실행하면 연결을 확인한다. `boayo stop`과 `bosio-tiles stop`은 WM과 그 WM이 실행한 앱 프로세스를 종료한다. 예전 UI만 닫는 동작이 필요하면 프로토콜의 `ui_stop`으로 WM이 실행한 앱들을 닫는다.

호스트·QEMU에서는 `boayo start --mock`을 사용한다. 상태의 `wm_pid`, `app_processes[].pid`로 프로세스 분리를 확인한다. 앱이 비정상 종료하면 해당 연결의 창과 공유 버퍼를 정리한다. 다른 앱의 연결은 유지한다.

## 공유 이미지와 변경 영역 알림

Unix 소켓은 `/tmp/bosio-wm.sock`이며 `BOAYO_SOCKET`으로 바꿀 수 있다. 기존 줄바꿈 JSON 요청과 응답 형식을 사용한다. 같은 연결에서 다음 순서로 요청한다.

1. `create_window`로 창과 원본 이미지 크기를 정한다.
2. `create_surface`에 `window_id`를 보내 두 RGB24 버퍼를 할당한다.
3. 응답의 `path`를 읽기·쓰기로 열고 `slot_bytes × slots`를 `MAP_SHARED`로 매핑한다.
4. 앱이 선택한 슬롯의 바뀐 픽셀만 기록하고 `commit_surface`로 슬롯·순번·변경 사각형을 알린다.
5. commit 응답을 받은 뒤 그 슬롯을 다시 쓸 수 있다. 실제 화면 반영을 기다릴 때는 응답의 `generation`으로 `wait_present`를 요청한다.

```jsonl
{"id":1,"app":"sample","op":"create_window","args":{"title":"Shared image","surface_width":64,"surface_height":64,"width_deg":16,"height_deg":12}}
{"id":2,"app":"sample","op":"create_surface","args":{"window_id":1}}
{"id":3,"app":"sample","op":"commit_surface","args":{"window_id":1,"slot":0,"sequence":1,"damage":[[0,0,64,64]]}}
{"id":4,"app":"sample","op":"commit_surface","args":{"window_id":1,"slot":1,"sequence":2,"damage":[[12,9,8,8]]}}
```

창 ID는 실제 응답 값을 사용한다. `create_surface` 응답에는 `path`, `format=RGB24`, `stride=width×3`, `slots=2`, `slot_bytes`가 들어간다. 경로는 WM이 보유한 익명 memfd의 `/proc/PID/fd/FD`다. 창과 연결이 살아 있는 동안 열 수 있다. 매핑 크기는 seal로 고정한다.

첫 commit은 전체 이미지를 포함한다. 다음 commit부터 슬롯의 나머지 픽셀을 다시 복사할 필요가 없다. WM은 알림에 적힌 사각형만 자신의 CPU 이미지로 복사하며 이후 앱의 버퍼 재사용과 독립적으로 투영한다. commit 응답의 `changed=false`는 같은 픽셀이라 장면 버전이 유지됐음을 나타낸다.

순번은 양의 정수로 계속 증가해야 한다. 변경 사각형은 `[x,y,width,height]` 형식이며 요청당 1~128개다. 모든 범위를 검사한 뒤 반영한다. 다른 연결의 창은 접근할 수 없다. 동일한 `app` 이름을 보내도 연결별 소유권이 분리된다. 앱은 commit 응답 전까지 제출한 슬롯에 쓰지 않아야 한다.

`wait_present`는 `{"generation": commit응답값}`을 인자로 받는다. 해당 장면 또는 이를 포함하는 후속 장면의 출력 게시가 완료되면 응답한다. 장치 오류와 WM 종료는 대기자에게 오류로 전달한다. commit 응답은 공유 슬롯 재사용 시점이며, 출력 완료는 `wait_present`로 확인한다.

## 작은 갱신의 300ms 목표

`bench-small`은 원본을 한 번 올린 다음 1×1과 8×8 변경을 반복한다. 각각 예열 1회 후 요청부터 출력 완료 응답까지 중앙값·95백분위 시간을 기록한다. CPU의 변경 처리, 소켓 왕복, 필요한 DDR 업로드·투영·출력 게시를 포함한다. 기본 본 측정은 조건당 21회다. 결과의 `target_met`는 해당 실행의 95백분위가 300ms 이내인지 나타낸다.

WM 기본 갱신 제한은 60fps다. 보시오는 기존 장면의 활성 타일 목록에서 뒷뱅크에 필요한 타일을 찾고, 변경 영역에 해당하는 원본과 출력 타일만 갱신한다. 앱의 이미지 생성·RGB 비교는 앱 프로세스에서 수행한다. WM의 공유 이미지 경로는 작은 완료 응답을 사용하고 상세 상태·프로파일 JSON은 요청할 때 생성한다.

`get_state`와 `profile_progress`는 스냅샷을 반환하면서 갱신을 요청한다. `snapshot_age_seconds`와 `rendered_generation`을 함께 확인한다. 매 프레임의 완료 대기에는 `wait_present`를 사용한다.

300ms는 작은 내용 변경의 실보드 목표다. 2026-11-19 후속 최적화에서 통신 스레드의 반복 확인을 완료 알림으로 바꾸고, 타일 준비 배열의 전체 초기화·임시 JSON 생성·단일 영역의 중복 교차 검사를 줄였다. 동일 실보드 조건에서 1×1 중앙값은 1,115.505→773.458ms, 8×8은 1,230.996→838.070ms로 줄었다. 개선판 95백분위는 각각 796.995ms, 865.825ms로 목표에 미달한다. 크기별 예열 후 21회이며 요청부터 출력 게시 완료 응답까지 포함한다. 호스트·QEMU 모의 결과와 실제 FPGA 시간을 구분한다. 창 이동·크기 변경은 좌표와 마스크를 새로 준비할 수 있다. [실보드 비교·검증 범위](VALIDATION.md)

## 메모리와 빌드

이미지당 최대 1,048,576픽셀이고 CPU 공유 버퍼 두 개는 최대 6MiB다. 살아 있는 창들의 공유 버퍼 합계 한도는 64MiB다. WM 소유 RGB24 복사본과 앱의 이미지도 RAM을 사용한다. 기존 GPU 텍스처 영역 4MiB는 모든 창이 공유하며 픽셀당 4바이트를 사용한다. 앱 프로세스별 코드·힙·스택 사용량도 추가된다.

Linux는 `CONFIG_SHMEM=y`, `CONFIG_MEMFD_CREATE=y`, Unix 소켓과 procfs가 필요하다. 개발 이미지 빌드는 두 메모리 옵션을 활성화한다.

```sh
bash software/userland/build.sh /var/tmp/haebancheon-dev all
bash updates/boayo-tilebatch-v14/software/boayo/native/build.sh /var/tmp/boayo-wm-host host
python3 updates/boayo-tilebatch-v14/tests/test_wm_daemon.py /var/tmp/boayo-wm-host/boayo-native-host
python3 updates/boayo-tilebatch-v14/tests/test_partial_projection.py /var/tmp/boayo-wm-host/boayo-native-host
```

[공유 이미지와 앱 프로세스 구현](../../updates/boayo-tilebatch-v14/software/boayo/native/src/app_process.cpp), [공유 버퍼 소유권](../../updates/boayo-tilebatch-v14/software/boayo/native/src/shared_surface.hpp), [작은 갱신과 실보드 이전 결과](VALIDATION.md)를 참고한다.
