# 실보드 각도 이동 및 추가 창 시도

2026-11-14. 기존 부팅과 BoAYO 프로세스를 유지하고 조작했다. RTL/SD/실행 프로그램은 변경하지 않았다.

## 확인된 동작

- 실제 HDMI 캡처 실시간 창을 열고 유지했다.
- `/usr/bin/boayo-native view 6 0`: yaw=6, pitch=0 응답, HDMI에서 런처가 왼쪽으로 이동한 모습 확인. 명령 왕복 6.04초.
- `view -12 4`: yaw=-6, pitch=4 응답. 명령 왕복 15.82초. 캡처 `interaction_yawminus6_pitch4.png` 보관.
- `view 6 -4`: yaw=0, pitch=0 복귀 응답.
- 각도 명령은 WASD가 사용하는 동일한 `view_move` 경로지만, 이번에는 셸 명령으로 호출했다. 실제 WASD 키 입력 시험이라고 주장하지 않는다.

## 추가 창의 미완료 동작

- 중앙 복귀와 Dashboard 열기 명령 묶음은 76.20초 뒤 `BOAYO_APP_READY window=2`를 반환했다. 이 수치는 중앙 복귀까지 포함하며 Dashboard만의 시간은 아니다.
- 상태에서 Dashboard와 resize/close 버튼 창 생성이 확인됐다. 하지만 09:27 무렵까지 HDMI는 이전 런처를 유지했고, 새 창의 실제 출력 완료는 확인하지 못했다.
- Telemetry 열기는 130.56초 뒤 `WM reply failed or timed out`, RC=1. 재요청하지 않았다. 서버에 이미 제출된 요청이 나중에 처리될 가능성은 남는다.
- 상태 서비스는 응답하며 phase=composing. 최근 완료된 프레임의 상태가 반환되는 snapshot이므로 render_error=null과 DMA 오류 0만으로 현재 작업 완료/정상을 판정할 수 없다.
- `/proc/33/task/34/stat` 두 관측에서 합성 스레드는 R 상태, 사용자 CPU tick이 37039→37732로 증가했다. CPU가 동작하고 있다는 근거이며 무한 루프 여부까지 배제하는 증거는 아니다.

## 확인된 성능 공백

실보드 `/usr/share/boayo/plans`에는 `p_d6f47219.bgp`와 `.ready`만 있다. `/data/boayo-plans`에도 같은 파일과 manifest만 있다. `install_plans.py`는 bootstrap 계획만 복사한다.

`Compositor::make_layer`는 계획이 없으면 CPU로 전체 타일의 기하를 계산하고, `prepare_masks`는 타일당 1024개 셀의 경계를 계산한다. 따라서 첫 런처의 9.36초 결과를 임의 앱/창 생성 속도로 일반화할 수 없다. 이 계산 경로가 추가 창 지연의 유력한 병목이나, 현재 PC 위치를 샘플링해 특정 함수에서 소비한 시간을 분리한 것은 아니다.

## 근거 파일

- `BODMA_1ee50c58.json`: 오른쪽 각도
- `BODMA_cdd0d7bf.json`: 반대쪽/위쪽 각도
- `BODMA_05aa59b1.json`: 중앙 복귀 및 Dashboard 생성
- `BODMA_163d4cbf.json`: Telemetry 요청 시간 초과
- `BODMA_af0c0b21.json`: 상태와 프로세스. tail 명령이 PATH에 없어 전체 RC=127이나 앞선 상태/ps 출력은 수집됨.
- `BODMA_7aadd1a1.json`, `BODMA_5d2daa33.json`: 계획 파일 목록 및 스레드 CPU 시간. BusyBox tail applet도 없어서 최종적으로 cat으로 로그 확인.

실시간 UART/HDMI 창과 보드 실행은 유지했다. 부팅·첫 화면 성공 결과와 이번 추가 창 미완료 결과는 구분한다.
