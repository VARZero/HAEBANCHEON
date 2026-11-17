# v13 작업 중단 지점 — 2026-11-15

사용자 요청으로 추가 최적화·25단계 시험·레이턴시 분석을 중단했다. 다음 재개 때 확인할 것은 두 가지다.

1. CPU가 투영코어 요청을 시작한 시점부터 완료를 받기까지의 실제 시간. 요청 준비/전송/드라이버 검증·레지스터 설정/실행·폴링/복귀를 구분한다.
2. 두 투영코어를 제어할 때 생기는 레이턴시: 동기 배치 완료 대기, 공용 잠금, 작은 배치, 작업량 불균형, 공유 DDR 중재를 분리한다. 지금은 추가 구현하지 않는다.

## 완료된 사실

- 첫 창 이동 동일 조건 비교: 기존 계산 228.957초 → 개선 계산 120.938초, 47.18% 단축.
- 같은 구간 계수 생성 133.263초 → 16.739초. 후보 선택 4.402초 → 10.617초. 마스크 31.362초 → 32.616초. 후보 3136개 → 484개.
- 로컬 계수 비교 5064000개·스케일 361190개, 37개 계획, 기존 장면/상태 회귀 시험 및 새 SD QEMU/내용 검증 통과. evidence 참조.
- 실보드는 v12 SD/커널 위에서 새 v13 앱을 RAM 실행. 기존 비트스트림 유지. 새 v13 SD 자체의 실보드 부팅은 하지 않았다.
- 기준 모드 11단계 완료·복귀 확인. 개선 모드 18단계까지 완료 로그 확인 후 사용자 중단. 25단계 전체 PASS나 이후 SDK 이동·닫기 완료로 보고하면 안 된다.
- 개선판 새 크기 확대 104.551초, 축소 31.619초, 겹침 111.816초. 캡처 프로그램 저장 프레임이 멈춰 PC 뷰어만 재시작했고 확대·축소 Dashboard 실제 화면을 확인했다. 오래된/검은/부분 캡처를 기능 완료 근거로 사용하지 않는다.

## 중단 처리

호스트 run_trials.py(34976), watch_trials.py(26524), monitor_hdmi.py(28000)는 중지했다. COM4 소유자인 기존 UART 수집기는 유지했다. 현재 실행 중이던 motion CLI에는 기존 전송 큐로 Ctrl+C를 보냈다. CLI는 SIGINT에서 motion_stop을 요청하고 원상 복귀 출력 완료를 기다린 뒤 `BOAYO_MOTION_STOPPED restored`를 출력하도록 구현돼 있다. 실제 복귀 확인 결과는 별도 pause-state.json을 참고한다. FPGA 재프로그래밍·커널 교체·RTL 변경은 하지 않았다.

## 다음에 읽을 자료

- `D:/temp/haebancheon-geometry-v13/board-trial/`의 partial 로그·기준 결과·first-move-profile.
- 기존 UART: `D:/temp/haebancheon-mask-v12/coldboot-20261115/uart.log`, 전송 큐도 같은 폴더. COM4를 직접 열거나 수집기를 중복 실행하지 않는다.
- 실제 DDR 상주 경로는 `software/boayo/native/src/resident.cpp`의 execute이다. backend.cpp의 CPU 다운로드 경로를 현재 주 경로로 오인하지 않는다.
- 기존 `projection_wait`는 dev.run/ioctl 구간이다. 첫 이동에서 20호출 합계 1.813852초(평균 약90.69ms), 스레드 CPU 합계0.499620초. descriptor 준비·업로드는 별도 1.578139초. 이 수치는 코어 단독 실행 시간이나 레지스터 start→done 실측으로 표현하면 안 된다. 두 구간 합계3.391991초는 20호출 묶음 전체이며 텍스처 전송·후단 합성/화면 전환은 제외한다.
- common/bosio_engine.c bo_run은 모든 descriptor/겹침 검증 후 두 코어에 설정·start를 순서대로 쓰고 두 코어 done을 각각 latch하여 모두 끝날 때 반환한다. 한 코어가 끝난 다음 다른 코어를 시작하는 구조는 아니다.
- driver/bosio_kfb.c ioctl은 공유 mutex를 잡은 채 실행, polling sleep은 usleep_range(10,60). 이 sleep은 실제 완료 인지 지연을 보장하는 상한이 아니다. 코어별 실제 완료 시각 및 DDR 중재 대기 시간은 아직 별도 측정하지 않았다.

새 SD: `D:/temp/haebancheon-geometry-v13/HAEBANCHEON-KFB-BoAYO-geometry-v13.img`, SHA-256 `dccb9042d390db89edc8dd877157aa575cc7f0f2342c77e051b4258435684eab`. 사용자 지시 전에는 SD 기록·새 실보드 시험을 시작하지 않는다. 현재 턴에서 Git commit/push는 하지 않았다.

## 추가 종료 요청 처리

사용자 요청으로 BoAYO를 종료하고 sync 및 /data 정상 unmount를 확인했다. 이어 Linux 종료 명령 후 UART `reboot: Power down`을 확인했다. 물리 전원 스위치는 사용자가 직접 꺼야 한다. 다음 실보드 작업에는 전원 상태와 필요 시 비트스트림 재업로드부터 확인한다.
