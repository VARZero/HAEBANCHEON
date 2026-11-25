# 보아요 앱 연동

[문서 목록](README.md) · [English](BOAYO_EN.md)

현재 버전의 `triangle-boayo-fixed`는 삼각 타일 렌더러를 직접 관리한다. 앱은 기존처럼 소켓에 연결하고, 공유 화면 버퍼에 픽셀을 쓴 뒤 변경 영역을 알린다. 이전 WM의 합성기·장식·앱 실행 관리자를 함께 띄우지는 않는다.

기본 메시지 형식은 **BWM1 바이너리**다. 클라이언트의 호출 순서와 연산 이름은 유지하지만, 갱신된 `native/src/ipc.hpp`와 코덱을 함께 사용해야 한다. 진단·비교용 JSON 경로는 `TRIANGLE_WIRE=json`으로 선택하며, WM과 클라이언트의 설정을 맞춘다.

공유 화면 버퍼는 RGB332, RGB24, XRGB8888을 지원한다. 전송 형식과 수치 표현의 이전 구현 기록은 [FIXED_BINARY.md](FIXED_BINARY.md), 현재 S/B 동작은 [SOLID_RESIZE.md](SOLID_RESIZE.md)에 있다.

## 빌드와 실행

현재 고정소수점 버전의 빌드·검증·SD 제작 명령은 [README.md](README.md#build-current)를 따른다. 예전 `build.sh`로 만든 `triangle-boayo`는 부동소수점 비교용이므로 현재 보드 실행 파일과 구분한다. 독립 실행형 실험 도구인 `triangle-wm`과도 구분해야 한다. 둘은 `/dev/bosio0`를 독점하므로 동시에 실행할 수 없다.

현재 SD 이미지에는 다음 파일이 설치돼 있다.

| 항목 | 경로 |
|---|---|
| WM | `/usr/bin/triangle-boayo-fixed` |
| 데모 앱 | `/usr/bin/triangle-app` |
| 입력 클라이언트 | `/usr/bin/triangle-input` |
| RAM에서 실행하는 복사본 | `/run/triangle-wm-runtime/` |
| 실행 상태 | `/run/triangle-wm.pid`, `/run/triangle-app.pid` |

부팅 시 `triangle-fixed-start`가 실행 파일을 RAM으로 옮긴 뒤 WM과 RGB332 데모를 시작한다. 수동으로 시작하거나 멈출 때도 같은 스크립트를 쓴다.

```sh
triangle-fixed-start start
triangle-fixed-start stop
```

데모의 글자와 사각형은 앱이 자기 메모리에 그린다. 닫기·이동·크기 조절 버튼과 포인터는 WM이 그린다. 데모는 기본적으로 RGB332를 사용하고, `--rgb24`와 `--rgb32`는 비교용 형식을 선택한다. 앱이 `create_surface`에서 형식을 생략하면 기존 호환 동작에 따라 RGB24를 받는다.

## 연결과 창 소유권

앱 소켓은 `$BOAYO_SOCKET`을 사용하며, 기본값은 `/tmp/bosio-wm.sock`이다. 내부 관리 소켓은 여기에 `.control`을 붙인 `/tmp/bosio-wm.sock.control`이다.

소켓 파일 권한은 0600이고, 연결한 프로세스의 UID가 WM의 UID와 같아야 한다. 창의 소유자는 요청의 `app` 문자열이 아니라 **살아 있는 소켓 연결**로 정한다. 앱 연결이 끊기면 그 연결이 만든 창과 공유 화면 버퍼를 정리한다. 창을 유지하려면 연결도 유지해야 한다.

연산의 논리적인 요청·응답 구조는 다음과 같다. 기본 경로에서는 이 구조를 JSON 문자열이 아닌 BWM1 바이너리로 보낸다.

```json
{"id":1,"app":"example","op":"create_window","args":{}}
```

성공 시 `id`, `ok: true`, `result`를 돌려주며, 실패 시 `id`, `ok: false`, `error`를 돌려준다.

## 앱에서 화면을 갱신하는 순서

1. **`create_window`로 창을 만든다.**
   `title`, `surface_width`, `surface_height`, `azimuth`, `elevation`, `width_deg`, `height_deg`, `mapped`를 지정할 수 있다. `padding_deg` 기본값은 2, 실제 배경색 `background_index`의 기본값은 84, 스플래시 색 `splash_index`의 기본값은 228이다. S/B 색은 각각 0–255번 중 선택한다. 처음에는 S색 배경을 먼저 표시한다.
2. **`create_surface`로 공유 화면 버퍼를 받는다.**
   `window_id`와 선택 항목인 `format`을 보낸다. 응답의 `path`, `format`, `width`, `height`, `stride`, `slots: 2`, `slot_bytes`, `sequence`를 확인한다. 반환된 크기에 맞춰 두 슬롯 전체를 읽기·쓰기 가능하게 매핑한다. 이미 만든 버퍼의 형식은 바꿀 수 없다. 형식을 생략해 다시 호출하면 기존 버퍼 정보를 반환한다.
3. **한 슬롯에 바뀐 픽셀을 쓴다.**
   두 슬롯 중 하나를 골라 수정한 뒤 `commit_surface`에 `window_id`, `slot`, `sequence`, `damage`를 보낸다. `sequence`는 양수이고 이전 값보다 커야 한다. `damage`는 `[[x,y,w,h], ...]` 형식이며, 버퍼 안에 들어오는 사각형 1–128개를 받는다.
4. **`commit_surface` 응답을 받으면 그 슬롯을 재사용할 수 있다.**
   WM은 지정된 변경 영역을 자신이 소유한 최신 픽셀 이미지로 복사한 뒤 응답한다. 응답에는 `generation`, `changed`, `copied_bytes`가 들어간다. 이 응답 이후 앱이 슬롯을 다시 써도 이미 접수한 이미지에는 영향을 주지 않는다.
5. **화면 반영이 필요하면 `wait_present`로 확인한다.**
   응답에서 받은 `generation`을 지정한다. 해당 세대 이상이 실제로 게시될 때까지 기다리며, 더 새 갱신이 중간 이미지를 대체할 수 있다. 서버가 아직 발급하지 않은 `generation`은 거부한다. 픽셀이 바뀌지 않은 제출도 `sequence`는 소비하지만 화면 세대는 올리지 않는다.

슬롯을 다시 써도 된다는 응답과, 화면이 반영됐다는 응답은 다르다. 이동·크기 조절 미리보기를 표시한 것만으로 앱의 미반영 콘텐츠가 완료됐다고 알리지 않는다.

## 창 설정과 최신 콘텐츠 처리

앱은 자신이 소유한 창에 `configure_window`, `destroy_window`, `raise_window`, `focus_window`를 호출할 수 있다. `configure_window`에서는 위치·크기·여백·표시 여부·제목과 S/B 색을 바꾼다.

S/B는 각각 `splash_index`와 `background_index`로 지정한다. WM의 B색은 창 배경과 투영 영역 바깥 샘플에 적용한다. 앱이 이미 채운 불투명 픽셀까지 다른 색으로 바꾸지는 않는다.

원본 화면 해상도와 공유 메모리 매핑은 창의 각도 크기를 바꿔도 유지된다. 다른 픽셀 해상도가 필요하면 새 창을 만들어야 한다. 위치·크기 확정 시에는 필요한 투영 디스크립터를 다시 준비하므로 단색 미리보기보다 비용이 크다. 외부 제어로 닫힌 창의 ID는 무효가 된다. 앱은 오류를 처리하고 매핑을 해제해야 하며, 제공한 데모도 자기 창이 사라지면 종료한다.

밀린 변경 영역은 창마다 합친다. 겹치는 사각형은 병합하고, 서로 떨어진 영역은 최대 128개까지 따로 보관한다. 이를 넘으면 모두 포함하는 사각형으로 묶는다. 다음 처리에서는 각 영역의 **최신 픽셀**을 읽고, 영향을 받는 타일을 한 번씩 갱신한다. 중간 제출을 전부 순서대로 그리지 않는다.

원본 텍스처 여러 개를 DDR에 유지하고 재사용한다. 필요할 때는 전체 폭의 행을 올리거나, 범위가 작은 변경을 임시 공간에 모아 DMA로 옮긴다. 관련 과거 측정은 [CONTENT_COALESCING.md](CONTENT_COALESCING.md), [TEXTURE_CACHE.md](TEXTURE_CACHE.md)에 보존돼 있다.

## 내부 관리 명령

`--ctl`은 제어 소켓에 연결하며, 화면 세대가 반환되는 작업은 해당 화면 반영까지 기다린다. 아래의 창 ID `1`은 `get_state`로 확인한 실제 ID로 바꾼다.

```sh
triangle-boayo-fixed --ctl '{"op":"get_state"}'
triangle-boayo-fixed --ctl '{"op":"configure_window","args":{"window_id":1,"azimuth":8,"elevation":2}}'
triangle-boayo-fixed --ctl '{"op":"configure_window","args":{"window_id":1,"width_deg":24,"height_deg":16}}'
triangle-boayo-fixed --ctl '{"op":"configure_window","args":{"window_id":1,"splash_index":228,"background_index":84}}'
triangle-boayo-fixed --ctl '{"op":"raise_window","args":{"window_id":1}}'
triangle-boayo-fixed --ctl '{"op":"destroy_window","args":{"window_id":1}}'
triangle-boayo-fixed --ctl '{"op":"view_set","args":{"yaw":5,"pitch":2}}'
triangle-boayo-fixed --ctl '{"op":"pose_sweep","args":{"samples":61,"amplitude":8}}'
triangle-boayo-fixed --ctl '{"op":"shutdown"}'
```

`get_state`, `sync_scene`, `app_events`, `poll_events`로 상태를 확인할 수 있다. 다만 앱 위젯에 포인터 이벤트를 전달하는 기능은 아직 구현하지 않았으므로 이벤트 배열은 비어 있다. WM의 포인터 조작은 제어 소켓에서 처리한다.

과거의 둥근 `shape`·창 장식 힌트, 이미지 전체의 base64 전송, 앱 실행 관리, 작업 공간 관리, 앱 위젯 입력 전달, 창 회전 `roll`은 현재 호환 범위에 포함하지 않는다.

## 자원과 동작 한계

| 항목 | 범위 |
|---|---|
| 창·연결 | 창 최대 64개, 연결 최대 64개 |
| 앱 원본 해상도 | 가로·세로 각각 1–1024픽셀 |
| 창 각도 | 방위각 ±180°, 고도각 ±80°, 가로·세로 1–100°, 여백 0–10° |
| 출력 시점 | 방위각 ±180°, 고도각 ±85°, `roll` 0 |
| S/B 색 | 각각 RGB332 번호 0–255 |
| 앱 버퍼 예약 총량 | 최대 64 MiB |

창 생성 시에는 픽셀당 13바이트로 예약한다. 공유 화면 버퍼를 만들면 실제 형식에 맞춰 RGB332는 7바이트, RGB24는 13바이트, XRGB8888은 16바이트로 조정한다. 두 공유 슬롯, WM의 최신 원본 이미지, 내부 RGB32 이미지를 포함한 값이다. 256색의 단색 타일은 첫 화면 게시 전에 두 뱅크에 준비한다.

렌더링은 서비스의 단일 이벤트 루프가 맡는다. 콘텐츠 제출 응답은 렌더링 전에 내보낸다. 화면 반영 전에는 대기 중인 입출력 요청을 최대 4차례 확인하고, 요청이 없으면 기다리지 않는다. 한 처리 묶음은 총 32요청, 각 연결은 한 차례에 4요청으로 제한한다. 준비된 제어 요청을 먼저 처리하고 앱 연결은 순환한다. 추가 입력을 모으기 위한 고정 대기 시간은 두지 않는다.

이미 진행 중인 투영·게시 ioctl을 선점하는 구조는 아니므로 새 요청이 기다릴 수 있다. `TRIANGLE_INPUT=0`이면 출력 시점 변경에 장면 재합성을 생략한다. 버튼·포인터를 켠 경로는 화면 좌표에 맞춰 덧그림을 다시 게시한다. 어느 쪽도 60 FPS 입력 반응을 보장하지 않는다.

## 측정값 읽는 법

`accept_ms`는 콘텐츠 제출이나 설정 요청의 응답까지, `present_ms`는 뒤이은 `wait_present` 왕복까지 포함한다. 앱의 원본 이미지 생성은 콘텐츠 제출 측정 밖에 있고, 원본에서 공유 슬롯으로 복사하는 시간은 안에 있다. 보드 내부 요청 시간에는 UART·USB 캡처·모니터 광학 지연이 들어가지 않는다.

PC 입력부터 실제 화면까지의 결과와 미리보기·본창 확정의 구분은 [SOLID_RESIZE.md](SOLID_RESIZE.md)를 참고한다. 초기 기준본, 픽셀 형식, Q44 시절 CPU 최적화, 응답 경로와 외곽 다듬기의 과거 결과는 [영문 원문](BOAYO_EN.md#limits-and-timing)의 링크 목록에 남겨 두었다. 그 수치를 현재 v13의 측정값으로 사용하지 않는다.
