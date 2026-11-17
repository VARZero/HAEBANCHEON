# 보아요 UI

보아요는 런처, 앱 창, 입력과 창 배치를 담당한다. 보시오 타일 미들웨어가 UI 이미지의 변경을 추적하고 투영코어와 출력코어를 구동한다. 두 명령은 같은 서비스를 사용하며 보시오가 `/dev/bosio0`을 단독으로 연다.

```sh
bosio-tiles start
boayo start
boayo status
boayo keys
```

`boayo start`가 받은 `target_generation`은 런처를 만든 장면 버전이다. `boayo status`의 `performance.rendered_generation`이 그 값에 도달하면 해당 버전이 출력 경로에 반영된 것이다. 키 입력 화면에서 W/A/S/D로 시점을 움직이고 Space 또는 Enter로 가운데를 누른다. Q를 누르면 키 입력 화면을 닫고 콘솔로 돌아온다.

```sh
boayo app dashboard
boayo app telemetry
boayo app pulse
boayo launcher
boayo stop
bosio-tiles stop
```

`boayo stop`은 런처와 보아요가 연 앱 창을 닫는다. 보시오에 연결된 별도 앱의 창은 계속 표시할 수 있다. 보아요는 `boayo start`로 다시 열 수 있다. `bosio-tiles stop`은 미들웨어와 출력까지 종료한다.

QEMU에서는 첫 명령을 `bosio-tiles start --mock`으로 바꿔 같은 UI 흐름을 시험한다. 일반 부팅은 직렬 콘솔로 시작한다. 내장 UI와 앱은 실행 시 그림을 생성하며 v14 배포 이미지에 있던 사전 생성 에셋과 투영 계획은 별도다. 첫 프레임에 걸리는 실보드 시간은 측정하지 않았다.
