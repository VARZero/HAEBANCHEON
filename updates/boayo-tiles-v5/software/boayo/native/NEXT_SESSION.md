# 다음 단계: BoAYO C++ v2 실보드 확인

2026-11-09. 사용자가 보드는 나중에 켠다고 했으므로 이번 작업에서 보드·UART·캡처카드에 접속하지 않았다.

## 완료한 오프라인 작업

- 고정 기하 사전 계산, 창별 공통 계산 재사용, 불필요한 장면 복사 감소.
- 초기 장면의 유효 데이터만 전송하고, 부분 갱신의 해당 범위만 shadow에 복사.
- 통신과 화면 작업 분리. 초기화 중/계산 중 상태 조회 및 완료 스냅샷의 경과 시간 제공.
- 정적 RV32 실행 파일과 미사용 공유 라이브러리 제외. BusyBox와 기본 도구 유지.
- Python 기준 기능·픽셀 비교, 두 투영코어/출력 Mock, IPC/WASD, ASan/UBSan 통과.
- 최종 SD payload QEMU 부팅, 쉘, 상위 RAM 16페이지, BoAYO Mock 런처·세 앱·입력·종료 통과.

## 새 이미지와 증거

- 기록용: `D:/temp/gen3bosio/boayo-native-v2/ORYUKDO-KFB-BoAYO-native-v2-raw.img`
- SHA256: `5f3be90dae30e9126ae55ffc1def4c39254d92ee6d31b3616945a7da0e3a4467`
- 전체 산출물: `work/bosio-linux-sw/artifacts/boayo-native-v2/`
- WSL 빌드: `/var/tmp/oryukdo-boayo-native-v2-final`
- 기존 비트스트림: `D:/temp/gen3bosio/clocksource_tlb/board_v1/route/ORYUKDO-Gen3-BOSIO-KFB.bit`
- 비트스트림 SHA256 재확인: `9e762305efd3c376cce979bb6857f428e02ccfe275388b85d412e39d44f9296b`

## 사용자가 보드를 준비한 뒤

1. v2 raw 이미지를 SD에 기록·검증하고 교체한다. 보드 연결 상태를 다시 확인한 뒤 기존 비트스트림을 사용한다.
2. UART에서 이미지 ID와 BusyBox 쉘을 확인하고 `boayo-start`, `boayo-status`를 실행한다.
3. 초기화 단계·완료 프레임·에러와 실제 HDMI를 함께 확인한다. 서비스 소켓/READY 문구만으로 화면 성공을 판단하지 않는다.
4. `boayo-keys`: WASD/방향키 시야, Space/Enter 중앙 클릭, R 초기화, Q 쉘 복귀.
5. 런처는 시작 시 자동으로 열리고, 다시 열려면 쉘에서 `boayo-launcher`. 전용 런처 키는 아직 없다.
6. 실제 시작 시간·상태 응답·화면/입력·장시간 안정성을 기록한다.

## 보존한 v1 관측

v1 실보드 쉘은 약 4분 14초에 도달했지만 런처 HDMI와 WASD는 확인하지 못했다. 상태 명령도 관측 제한 300초 안에 반환하지 않았다. 당시 시작 보조 PID 23만 종료하고 작업 PID 25를 유지했으나, 이 번호와 UART 포트는 과거 관측값이므로 재사용하지 말고 확인한다. 증거는 `artifacts/boayo-native-board-20261109/`에 있다.

PC/QEMU 수치를 실제 FPGA 성능으로 환산하지 않는다. QEMU 검증은 SD SPI·ORYUKDO RTL·MIG·실제 투영/출력/HDMI 검증이 아니다. 기존 전체 CPU RTL 검증 중단 요청을 유지한다. RTL·MIG·HDMI IP·비트스트림 및 GitHub는 이번 작업에서 변경하지 않았다.
