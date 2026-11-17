# BoAYO 런처 초기화 진단 SD — launchtrace-v1

현재 실보드에서 `desktop_initialization`이 오래 지속되는 원인을 확인하기 위한 이미지다. 원인 수정본이나 성능 개선본이 아니다. 기존 BOSIO DMA 드라이버·RTL·비트스트림·그림 계산식을 유지하며, 첫 데스크톱 생성에 단계별 기록을 추가했다.

## 파일과 사용법

- SD 이미지: `D:\temp\gen3bosio\boayo-launchtrace-v1\ORYUKDO-KFB-BoAYO-launchtrace-v1.img`
- 크기: 276,824,064바이트(264MiB), ext4 `/data` 256MiB
- SHA-256: `4aa56a1358e94a2d5bd8663e7b811a4fa602af30764294a434af9fefb3bd8f20`
- 커널 식별자: `6.12.0-oryukdo-launchtrace1`
- `/etc/oryukdo-image`: `ORYUKDO-BoAYO-launchtrace-v1`
- 비트스트림: 기존 `D:\temp\gen3bosio\sd4-dma\candidate\ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`
- HDMI 연결: 보드 **HDMI2 → 캡처카드 HDMI 입력**.

보드 전원을 끈 상태에서 balenaEtcher로 위 고유 이름의 이미지를 기록하고 검증한다. 전체 이미지 기록은 기존 SD의 파일을 덮어쓰므로 필요 파일은 먼저 백업한다. SD를 보드에 넣고 전원을 켠 뒤 기존 비트스트림을 다시 올린다.

셸이 나오면 다음을 실행한다. BoAYO는 자동 시작하지 않는다.

```sh
uname -r
cat /etc/oryukdo-image
boayo-start
```

`boayo-start`가 진단을 기본 활성화한다. 첫 데스크톱 생성의 진행 기록이 **UART에 직접 출력**되고 `/tmp/boayo-wm.log`에도 저장된다. 상태 조회를 반복해서 실행하지 않아도 된다. `/tmp`의 로그는 전원을 끄면 사라지므로 PC에서 UART를 기록한다.

## 무엇을 구분할 수 있나

실행 순서는 빈 화면 메타데이터 설정 → 데스크톱/런처 이미지 생성 → 투영·합성 → 업로드·표시다. 이번 세부 진단 대상은 데스크톱/런처 이미지 생성이다. Dashboard 앱은 자동 실행하지 않는다.

| UART의 `event=` 값 | 뜻 |
|---|---|
| `desktop.begin` | 데스크톱 생성 진입 |
| `window.create.begin/end` | 640×360 런처 창·표면 생성 |
| `window.configure.begin/end` | 창 모양·속성 설정 |
| `launcher.allocate.begin/end` | 런처 작업 이미지 할당·초기화 |
| `surface.clear.begin/end` | 이미지 배경 채우기; `value`는 바이트 수 |
| `rounded.begin`, `rounded.row`, `rounded.end` | 둥근 모서리 계산; `row` 값이 진행 |
| `circle.begin`, `circle.row`, `circle.end` | 원 계산; `row` 값이 진행 |
| `launcher.text.begin/end` | 런처 제목·안내 문자 그리기 |
| `launcher.app.begin` | 앱 목록 항목 그리기; `value=0,1,2` |
| `launcher.apps.end`, `launcher.image.end` | 목록·런처 이미지 생성 완료 |
| `surface.publish.begin`, `surface.scan.row/end` | 변경 영역 검색; 행 0/180/359로 진행 확인 |
| `surface.copy.begin/end` | 691,200바이트 이미지 복사 |
| `surface.publish.end`, `desktop.end` | 창 메모리에 결과 반영, 데스크톱 생성 완료 |
| `desktop.scope_exit` | 진단 범위를 벗어남; 예외 경로에서도 나올 수 있으므로 `desktop.end`도 확인 |

각 줄은 다음 형식이다. 아래는 형식 예시이며 실보드 측정값이 아니다.

```text
BOAYO_INITTRACE n=12 event=rounded.row wall_us=123456 cpu_us=98765 value=9
```

- `n`: 해당 생성 작업 안의 순번. 첫 런처에서는 호스트 검사 기준 67줄이며 최대 128줄로 제한했다.
- `wall_us`: 데스크톱 생성 진입 이후 경과 시간(마이크로초).
- `cpu_us`: 해당 작업 스레드가 사용한 CPU 시간. 커널이 지원하지 않으면 `-1`.
- `value`: 행 번호, 항목 번호, 버퍼 크기 또는 창 ID.

`*.begin` 이후 대응하는 `*.end`가 없다면 그 사이가 조사 대상이다. 행 번호가 계속 바뀌면 해당 계산이 진행 중이라는 근거가 된다. 경과 시간만 크고 CPU 시간이 적다면 스케줄링·대기·출력 비용도 살펴봐야 한다. 이것만으로 정확한 원인을 단정하지 않는다.

기록 자체의 UART 출력·시간 조회 비용이 포함되므로 이 이미지의 시간을 기존 이미지와 직접 비교해 성능 개선/악화를 판단하지 않는다. 픽셀마다 출력하지 않고 바깥 반복문의 일부 행만 기록한다. 추가 진단 시간 계산에 부동소수점 연산을 사용하지 않는다.

## 표시 완료와의 구분

기존 `BOAYO_NATIVE_READY initial_scene_committed`는 런처 표시 완료로 오해할 수 있어 다음 문구로 바로잡았다.

```text
BOAYO_NATIVE_READY blank_committed_launcher_pending
BOAYO_FIRST_FRAME_COMPOSE_BEGIN
BOAYO_FIRST_FRAME_UPLOAD_BEGIN
BOAYO_FIRST_FRAME_PRESENT_DONE
```

이 후속 문구는 `/tmp/boayo-wm.log`에 기록된다. `desktop.end`만으로 HDMI 정상이라고 판단하지 않는다. `PRESENT_DONE`도 출력코어의 전환 완료이며, 실제 HDMI2·케이블·캡처 경로는 화면 관측 또는 별도 고정 패턴 검사가 필요하다. 이 진단 이미지에는 독립 HDMI 테스트 패턴을 추가하지 않았다.

진단을 끄고 실행하려면 **서비스가 실행 중이지 않은 상태에서**:

```sh
BOAYO_INIT_TRACE=0 BOAYO_INIT_TRACE_UART=0 boayo-start
```

UART 직접 출력만 끄고 파일 기록을 유지하려면 `BOAYO_INIT_TRACE_UART=0 boayo-start`를 사용한다. 이미 실행 중인 서비스에는 시작 환경변수 변경이 적용되지 않는다.

## 검증 범위

- 기존 기능 회귀 9개 PASS.
- 변경 전 바이너리 / 변경 후 기록 OFF / 변경 후 기록 ON: 창 상태, 모든 창 표면 픽셀 해시, 전체 합성 장면 바이트 일치 PASS.
- 기록 67개의 순서·필수 단계·행 진행·시간 단조성 검사 PASS.
- RV32 앱·커널 빌드 및 내장 실행 파일/시작 스크립트/설정/DTB/SD 페이로드 일치 검사 PASS.
- SD RTL 적재/CRC 및 런타임 전송 검사 PASS(DDR 모의 모델; 전체 CPU Linux 부팅 검증 아님).
- QEMU 결과는 `evidence/qemu/result.json` 참조. BOSIO는 모의 장치이며 실보드 HDMI·CPU 성능 검증은 아니다.
- **이 진단 이미지의 실보드 실행은 아직 하지 않았다.**

선행 BOSIO-DMA-v1 이미지는 실보드에서 4KiB 업로드·복사·채우기, 겹침 거부, DMA IRQ 0→3, SD 256KiB 기록/정상 재마운트/전체 바이트 재검사를 통과했다. BoAYO는 빈 메타데이터 완료까지 확인했지만 런처/HDMI/WASD는 미완료다. 전원 차단 전 `sync`와 `/data` 정상 해제를 확인했다. 이는 이번 진단 이미지의 실보드 PASS 근거로 대신 사용하지 않는다.

## 빌드

WSL Ubuntu, 저장소 루트에서:

```sh
bash updates/boayo-launchtrace-v1/build.sh /var/tmp/oryukdo-boayo-launchtrace-v1/release
```

기본 입력은 `/var/tmp/oryukdo-bosio-dma-v1/release`의 전체 Linux 소스·설정·root·OpenSBI이며 `BASE_BUILD`로 바꿀 수 있다. 입력을 별도 트리로 복사하고 원본은 수정하지 않는다. 앱은 기존 RV32 musl 툴체인, 커널은 `riscv64-linux-gnu-`, 이미지 생성에는 dtc/e2fsprogs를 사용한다. 로컬 도구 의존성은 앞선 [DMA 후보 안내](../bosio-dma-v1/README_KO.md)와 같다.
