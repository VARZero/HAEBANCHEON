# 해반천 삼각 타일 WM

현재 보드에 적용한 버전은 **`fixed-wm-solid-resize-v13`**이다. 한국어 문서를 기본으로 사용한다. [영문 원문](README_EN.md)은 참고용으로 보존한다.

| 문서 | 내용 |
|---|---|
| [S/B 색과 이동·크기 조절](SOLID_RESIZE.md) | 현재 화면 동작, 타일 데이터 공유, 실보드 측정과 남은 문제 |
| [앱 연동](BOAYO.md) | 소켓 연결, 공유 화면 버퍼, 변경 영역 전송, 색 설정, 완료 확인 |
| [포인터와 실시간 화면](INPUT.md) | 버튼 배치, 포인터 닷 설정, PC 입력 연결, 상태 확인 |

## 현재 동작

창의 스플래시 색 S와 실제 배경색 B를 따로 지정한다. 두 색 모두 RGB332의 0–255번 색을 사용할 수 있다.

- **처음 실행할 때:** 창 영역을 S색으로 먼저 표시하고, 콘텐츠 준비가 끝나면 B색 배경과 콘텐츠를 표시한다.
- **이동할 때:** 원래 창과 콘텐츠는 그대로 두고, S색 삼각형 타일 하나만 움직인다.
- **크기를 조절할 때:** 원래 창과 콘텐츠·버튼을 숨기고, S색으로 채운 빈 영역 전체를 늘리거나 줄인다. 윤곽선은 그리지 않는다.
- **마우스를 놓을 때:** 최종 위치·크기의 B색 창과 최신 콘텐츠를 한 번에 표시한다. 그동안 밀린 중간 상태는 차례로 그리지 않는다.
- **취소할 때:** 원래 창을 복원한다. 위쪽 창에 의한 가림과 아래쪽 창의 복원도 유지한다.

미리보기의 모든 타일은 각 장면 뱅크에 준비한 **같은 1 KiB짜리 S색 타일 데이터**를 참조한다. 드래그 중에는 픽셀을 다시 칠하지 않고 타일별 참조 정보를 바꾼다. 포인터 닷은 별도로 켜거나 끌 수 있으며, 꺼도 좌표 추적과 창 조작은 계속된다.

창에는 둥근 모서리나 고정 테두리가 없다. 콘텐츠보다 조금 넓은 영역을 배경 타일로 확보하고, 실제 삼각형 타일의 경계를 창 모양으로 사용한다. 타일 배치와 출력 방향에 따라 외곽 모양은 달라진다.

## 현재 구현

보아요는 앱의 소켓·공유 화면 버퍼 호출 방식을 유지하면서 창 관리와 입력 처리를 맡는다. 보시오는 투영과 출력 작업을 수행한다. 이전 WM의 합성기·장식·앱 실행 관리자를 그대로 띄우는 구조는 아니다.

- CPU 기하 계산은 Q24 고정소수점으로 처리한다. 창 각도는 0.25도 단위로 맞춘다.
- 앱과 WM은 기본적으로 BWM1 바이너리 메시지를 주고받는다. JSON은 진단·비교 경로로 남아 있다.
- 빠른 포인터 입력은 PTR1을 사용한다. 밀린 좌표는 최신 값으로 바꾸되 누름·놓기 순서는 보존한다.
- 앱의 변경 영역은 창마다 누적하고, 다음 갱신에서 최신 픽셀을 읽어 필요한 타일만 다시 투영한다.
- 두 투영 코어는 서로 다른 목적지 타일을 처리한다. 바뀌지 않은 타일은 재사용하거나 DMA로 복사한다.
- 기본 화면 게시 경로는 개선한 `BO_PUBLISH_COPY`다. 메타데이터 전체를 복사하고 출력 코어의 완료 응답을 기다린다.
- 장면은 두 뱅크를 번갈아 표시한다. 현재 표시 중인 뱅크의 픽셀을 덮어쓰지 않는다.

타일 목록 계산, 메타데이터 전송, 출력 완료 대기는 여전히 필요하다. 독립된 하드웨어 커서나 비동기 명령 처리기를 추가한 것은 아니다.

## 실보드에서 확인한 성능

2026-11-25에 실제 보드에서 동작을 검사하고 부팅 파일을 저장했다. 아래 시간은 **PC의 마지막 드래그 입력부터 해당 목표가 실제 USB HDMI 캡처에 나타날 때까지**다.

| 동작 | 중앙값 | 관측 범위 | 횟수 |
|---|---:|---:|---:|
| v13 이동: S색 타일 하나 | 239.6 ms | 215.7–277.8 ms | 3 |
| v12 크기 조절: 외곽 타일 | 497.3 ms | 465.9–513.0 ms | 3 |
| v13 크기 조절: S색 전체 영역 | 529.5 ms | 458.3–674.3 ms | 3 |

10 Hz로 좌표 20개를 보내고, 마지막에는 앞선 경로와 구분되는 목표를 주었다. 크기 조절 두 버전은 같은 20×14도 창, 최종 기하 정보, 캡처 판정 영역으로 비교했다. 시간에는 HTTP·UART·USB 캡처 지연과 프레임 샘플링이 포함되며, 모니터의 광학 지연은 포함되지 않는다.

**이번 변경에서 크기 조절의 속도 개선은 확인되지 않았다.** 3회 측정만으로 작은 차이나 최악 지연을 단정할 수도 없다. 놓은 뒤 본창 완성까지는 별도의 보드 내부 소켓 검사에서 1.52초, 더 큰 확대 동작 검사에서 1.82초가 걸렸다. 각각 한 번 측정한 값이며, 1초 목표는 아직 달성하지 못했다.

| 검증 기록 | 파일 |
|---|---|
| 실보드 측정과 최종 상태 | [solid-resize-board.json](results/solid-resize-board.json) |
| 소프트웨어 검증 항목과 실행 파일 해시 | [solid-resize-validation.json](results/solid-resize-validation.json) |
| SD 이미지 구성과 해시 | [solid-resize-image.json](results/solid-resize-image.json) |
| 실제 RV32 실행 파일의 이미지 부팅 검사 | [solid-resize-boot.json](results/solid-resize-boot.json) |

에뮬레이터에서 얻은 시간은 실보드 성능으로 사용하지 않는다. 측정 구간과 입력 경로별 차이는 [S/B 미리보기 문서](SOLID_RESIZE.md)에 정리했다.

<a id="build-current"></a>
<a id="build-validate-and-package-the-current-release"></a>

## 빌드·검증·SD 이미지 제작

저장소 루트에서 WSL/Linux로 실행한다.

```sh
bash updates/triangle-wm/tools/build_fixed.sh /var/tmp/triangle-solid-resize-release
bash updates/triangle-wm/tools/validate_solid_resize.sh /var/tmp/triangle-solid-resize-release
python3 updates/triangle-wm/tools/package_fixed.py \
  --build /var/tmp/triangle-solid-resize-release \
  --output /var/tmp/triangle-solid-resize-image \
  --name HAEBANCHEON-solid-resize-preview.img
python3 updates/triangle-wm/tools/test_publish_image.py --marker \
  --image /var/tmp/triangle-solid-resize-image/HAEBANCHEON-solid-resize-preview.img \
  --output /var/tmp/triangle-solid-resize-image/focused-boot
```

빌드에는 호스트 C++ 컴파일러·Python과 다음 경로의 검증된 RV32 Buildroot 도구 모음이 필요하다.

`/var/tmp/haebancheon-userland-validation/buildroot/host/bin/`

이미지 제작에는 `/var/tmp/triangle-publish-copy/` 아래에 준비한 기준 rootfs·커널·부팅 이미지, `debugfs`, `e2fsck`도 필요하다. 위 명령만으로 새 PC에서 모든 기반 파일을 처음부터 만드는 것은 아니다. 기반 환경은 [재현 가능한 사용자 환경 빌드](../../software/userland/README.md)를 참고한다. 게시 드라이버의 준비 과정은 과거 기록인 [PUBLISH_COPY.md](PUBLISH_COPY.md)에 남아 있다.

이미지 부팅 검사에는 RV32 QEMU와 `dtc`가 필요하다. 검증 스크립트는 두 뱅크의 픽셀 비교, 확대·축소·취소, 창 가림과 복원, S/B 색 설정, 최신 콘텐츠 반영, 입력 순서, 실패 시 복구를 검사한다. 호스트 검사와 이미지 부팅 검사가 실제 HDMI 검증을 대신하지는 않는다.

## 보드 실행과 실시간 화면

SD 이미지의 `/etc/init.d/S99triangle-fixed`는 `/dev/bosio0`가 있으면 WM과 RGB332 데모를 시작한다. 실행 파일은 먼저 `/run/triangle-wm-runtime/`으로 복사해 RAM에서 실행한다.

| 항목 | 경로 |
|---|---|
| 설치된 WM | `/usr/bin/triangle-boayo-fixed` |
| 실행 중인 WM·앱·입력 클라이언트 | `/run/triangle-wm-runtime/` |
| WM PID | `/run/triangle-wm.pid` |
| 앱 PID | `/run/triangle-app.pid` |
| WM·앱 로그 | `/tmp/triangle-wm.log`, `/tmp/triangle-app.log` |

보드의 시작·종료 명령은 다음과 같다. 이미 실행 중일 때 `start`를 다시 호출해도 WM을 중복 실행하지 않는다.

```sh
triangle-fixed-start start
triangle-fixed-start stop
```

PC에서 실제 HDMI 캡처와 포인터 입력을 연결하는 방법은 [INPUT.md](INPUT.md)를 따른다. UART는 COM4, 115200 baud이며 DTR/RTS는 끈다. UART를 사용하는 PC 프로세스는 하나만 실행한다. 창 조작 화면은 `http://127.0.0.1:8767/?v=13`이다.

검사한 SD 이미지의 로컬 경로는 `D:\temp\haebancheon-solid-resize-preview\HAEBANCHEON-solid-resize-preview.img`다. 이미지·영상·빌드 결과·장치별 원본 로그는 Git에 넣지 않는다. 재현 절차, 이미지 구성·해시와 측정 요약은 저장소에 보관한다.

## 이전 버전 기록

현재 구현은 `feat/reproducible-linux-userland`의 `5f6ff539fae3e529ef53c1d90952dbe72ba609ea`를 바탕으로 `feat/triangle-tile-wm` 브랜치에서 개발했다.

초기의 6단계 실험, 외곽 타일·얇은 셀 윤곽 비교, 고정소수점·통신 방식의 분리 비교 등은 과거 기록으로 남겨 두었다. 당시 동작·경로·측정값을 현재 v13의 결과로 읽지 않도록 주의한다. 이 문서의 [영문 원문](README_EN.md#earlier-experiments-and-foundations)에서 과거 문서 목록과 독립 실행형 `triangle-wm` 실험 절차를 확인할 수 있다.
