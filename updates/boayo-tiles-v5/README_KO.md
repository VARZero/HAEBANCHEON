# 해반천 — BoAYO tiles-v5 고정 장면 실행 자료

Linux가 이미 실행 중인 상태에서 BoAYO 데몬을 시작해 첫 장면을 전환하는 경로를 줄인 SD 이미지다. 기존 v4와 이미지·소스·증거를 분리한다. 기존 SD4 비트스트림을 사용한다.

## 변경 내용

- PC에서 고정 첫 장면의 두 투영코어 명령 목록, 두 출력 뱅크용 DMA Fill/경계 합성 요청, 출력 타일 디렉터리를 생성한다. `.bgp.ready`에는 픽셀 사본이 없다.
- 보드에서는 이 읽기 전용 파일을 매핑하고 출력 뱅크를 선택해 전달한다. 실제 Linux 경로는 이미 준비된 요청 배열을 중간 벡터로 복사하지 않는다.
- 원본 2D 텍스처는 기존 직접 DMA 업로드를 유지한다. 투영코어가 52개 완전 타일과 44개 경계 타일을 처리하고, 출력코어가 완료 후 전환한다.
- 빈 영역은 디렉터리의 `0xffffffff`로 표현한다. 장면 전체의 픽셀 영역을 지우지 않는다. 경계 합성을 위해 실제 표시하는 44개 타일만 기존 방식으로 준비한다.
- 매 전환마다 4,220개 타일을 다시 비교하는 검사를 기본 경로에서 제거했다. 순차 장치 완료 응답과 버퍼 소유권 관리는 유지한다. 오류가 발생하면 해당 출력을 실패 상태로 고정하고 재사용하지 않는다.
- 공통 `require` 함수에 문자열 리터럴용 경로를 추가했다. 정상 처리에서 오류 메시지 문자열을 만들거나 힙을 할당하지 않는다. 실패 시 예외 메시지는 유지한다.
- 창 내용 변경, 겹침, 이동, 가림, WASD, 포인터, 앱 실행은 기존 변경 타일 경로로 이어진다. 고정 경로는 첫 실행의 단일 사전 준비 창에만 적용된다.

CPU에는 주소·요청·완료 관리와 이후 변경을 위한 소규모 버전 상태 등록이 남는다. 모든 CPU 작업을 DMA로 옮긴 구성은 아니다. RTL, MIG, HDMI IP, DMA 하드웨어, Linux 장치 드라이버는 v4와 동일하다.

## 실행 순서

1. 장면 계획과 장치용 요청 목록을 매핑한다. 내장 읽기 전용 자료는 이미지 제작 때 전수 검증한다.
2. 비활성 출력 뱅크를 선택하고 첫 장면 디렉터리 및 필요한 타일의 상태를 등록한다.
3. 필요한 경계 타일 준비 요청을 한 묶음으로 전달한다.
4. 2D 텍스처를 직접 DMA로 업로드한다.
5. 미리 만든 명령 목록으로 두 투영코어를 실행하고 경계 합성 요청을 전달한다.
6. 모든 요청이 완료되면 메타데이터를 게시하고 장면을 전환한다.

파일 매핑은 Linux가 수행하며, 텍스처 전송은 기존 BOSIO 직접 DMA 경로다. 임의 파일을 SD에서 투영 DDR로 읽는 새로운 단일 DMA 명령을 추가한 것은 아니다.

## 이미지와 사용법

- SD: `ORYUKDO-KFB-BoAYO-tiles-v5.img`
- 이미지 식별: `ORYUKDO-BoAYO-tiles-v5`
- Linux: `6.12.0-oryukdo-tiles5`
- 비트스트림: 기존 `ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`

보드 전원을 끄고 balenaEtcher로 새 이미지를 기록·검증한다. SD 기록은 기존 `/data` 내용도 교체하므로 필요한 파일을 먼저 백업한다. SD를 꽂고 전원을 켠 다음 기존 비트스트림을 올린다. Linux 셸에서 `cat /etc/oryukdo-image`로 v5를 확인하고 `boayo-start`, `boayo-status`, `boayo-keys`를 사용한다. HDMI는 HDMI2다.

UART의 `output.present.done`은 출력 전환 완료 응답이다. 실제 HDMI 첫 화면이 보인다는 증거와는 구분한다. `device_ready_frames: 1`이면 사전 생성 요청 경로가 실행된 것이다. `ready.prepare.begin/end`, `plan.load.begin/end`, `texture.upload.begin/end`, `projectors.begin/end`, `output.versions.begin/end`, `output.publish.begin/end`으로 시간을 구분할 수 있다.

비교 진단이 필요할 때만 데몬을 중지한 뒤 `BOAYO_READY_PLANS=0 boayo-start`로 기존 요청 생성 경로를 선택한다. `BOAYO_VALIDATE_SCENE=1 boayo-start`는 전체 버전 검사를 추가하는 진단 모드다. 기본값은 검사 비활성이다. 외부 경로의 자료는 내용까지 검증하고 손상·불일치 시 일반 경로로 돌아간다.

## 시간 해석

v4의 주요 첫 장면 처리 구간 합은 약 171.14초였다. 이 중 전체 타일 검사 구간은 96.04초, 장면 계획 로드·타일 준비 합은 37.95초였다. 이것은 계측 구간의 합이며 정확한 데몬 시작부터 첫 화면까지의 총시간이나 Linux 부팅 시간은 아니다.

v5는 이 두 CPU 부담을 겨냥했다. DMA·투영·출력 대기와 남아 있는 관리 비용이 있으므로 단순 차감 값을 측정 결과로 제시하지 않는다. 실제 보드 소요 시간, HDMI 영상, 실보드 WASD는 새 SD로 별도 확인해야 한다. QEMU/호스트 실행 시간을 FPGA 성능으로 환산하지 않는다.

## 재현과 검증

WSL Ubuntu에서 이 디렉터리의 `build.sh`, `validate.sh`, `package.py`를 순서대로 실행한다. 기본 빌드 경로는 `/var/tmp/oryukdo-boayo-tiles-v5`다. 기존 RGB-DMA 빌드의 Linux 원본·BusyBox·OpenSBI와 설치된 RV32 툴체인을 사용한다. `BASE_BUILD`로 기준 빌드 경로를 지정할 수 있다. `VALIDATE_PHASE=host`와 `VALIDATE_PHASE=image`로 검증 단계를 나눌 수 있다.

최종 PASS 범위는 동봉한 `result.json`, `evidence/`를 기준으로 확인한다. 호스트/메모리 검사에서는 기존 화면과 비교하고, QEMU에서는 정확한 SD 이미지에 들어 있는 커널·프로그램을 실행하되 BOSIO 백엔드는 Mock이다. 기존 BS25 출력 RTL과 SD4/DMA RTL 테스트도 실행한다. 전체 ORYUKDO CPU RTL의 Linux→BoAYO→HDMI 통합 실행 및 실보드 검증을 완료했다고 주장하지 않는다.
