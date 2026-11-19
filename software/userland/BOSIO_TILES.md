# 보시오 타일 미들웨어

`build.sh OUTPUT all`은 커널에 들어가는 보시오 UAPI 헤더와 Buildroot 교차 컴파일러로 보시오 타일 미들웨어를 빌드한다. 이미지에서 `bosio-tiles start`로 실행하고 `bosio-tiles status`로 갱신 수를 확인한다. `bosio-tiles stop`은 장치 사용을 종료한다. 실제 백엔드는 `/dev/bosio0`을 단독으로 연다. QEMU나 호스트에서는 `bosio-tiles start --mock`을 쓸 수 있다.

보시오를 시작하면 빈 장면과 창 API를 제공한다. 런처와 내장 앱을 사용하려면 [보아요 UI](BOAYO_UI.md)를 시작한다.

최신 앱 경로는 [공유 메모리와 변경 영역 알림](WM_DAEMON.md)을 사용한다. WM은 두 버퍼 중 제출된 슬롯의 변경 사각형만 복사하고, 앱은 응답 후 해당 슬롯을 재사용한다. 아래 Base64 `update_surface` API도 기존 앱용으로 지원한다.

| 계층 | 단위 | 저장 위치 | 변경 처리 |
|---|---|---|---|
| 2D 이미지 | 32×32 정사각형. 오른쪽·아래쪽 끝은 이미지 경계에서 잘림 | CPU의 선형 RGB24 이미지와 보시오의 4MiB 공유 텍스처 영역 | 요청 사각형에서 픽셀을 비교하고 바뀐 타일 안의 변경 범위를 기록. 연속 범위는 합쳐서 전송 |
| 보시오 장면 | BS25 구면 타일 4,220개. 타일당 RGB332 셀 1,024개 | 두 장면 뱅크와 경계 합성용 스크래치 | 투영 디스크립터의 샘플 범위와 2D 변경 영역을 대조. 영향받는 타일을 투영코어에 보내고 기존 타일은 버전으로 재사용 |

2D 타일은 변경 추적의 단위다. DDR 텍스처는 투영코어가 읽을 수 있도록 행 우선으로 이어진다. 미들웨어가 창별로 공간을 할당하고, 텍스처가 퇴출됐다가 필요해지면 전체 이미지를 다시 올린다. 텍스처는 픽셀당 4바이트이므로 한 이미지가 사용할 수 있는 최대 크기는 1,048,576픽셀이다. 여러 창과 에셋은 같은 4MiB를 공유한다. 첫 사용이나 크기 변경에는 전체 전송이 필요할 수 있다.

CPU가 변경된 2D 영역을 기록하고 보시오 타일별 디스크립터를 준비한다. 두 투영코어는 한 번에 각각 최대 64개 작업을 처리한다. 변경 타일이 겹치거나 경계에 걸리면 스크래치와 기존 마스크 합성을 사용한다. 화면에 보일 다른 타일의 최신 버전이 뒤쪽 뱅크에 없으면 DMA로 복사한다. 현재 표시 중인 뱅크는 그대로 읽히고, 복사와 투영이 끝난 뒤 `BO_PUBLISH`로 새 장면을 게시한다. 창 이동, 크기, 가림 순서 변경도 영향 범위에 포함된다.

초기 표시와 창 이동에서는 영향 범위의 배경과 아래쪽 창을 먼저 복원한 뒤 보이는 창을 투영한다. 같은 색으로 채울 인접 타일은 하나의 DMA fill 구간으로 합친다. 빈 검정 배경은 디렉터리의 빈 항목으로 표시한다. 최상위 창이 완전히 덮는 타일에서는 아래쪽 레이어의 투영을 생략하고, 덮개가 이동하거나 사라지면 누적된 변경을 반영한다. 출력 뱅크별 타일 버전으로 이전 화면의 내용도 재사용한다.

## 앱에서 이미지 갱신

로컬 Unix 소켓 `/tmp/bosio-wm.sock`에 줄바꿈으로 구분한 JSON을 보낸다. 아래 요청은 같은 연결을 유지하면서 사용한다. 두 번째 요청의 `window_id`에는 첫 응답에서 받은 값을 넣는다.

```jsonl
{"id":1,"app":"sample","op":"create_window","args":{"title":"Tile sample","surface_width":320,"surface_height":200}}
{"id":2,"app":"sample","op":"update_surface","args":{"window_id":1,"x":35,"y":20,"width":1,"height":1,"rgb24":"/wAA"}}
```

`rgb24`는 위에서 아래로 이어진 RGB 픽셀의 Base64다. 갱신 위치와 길이를 검사한 뒤 적용한다. 같은 픽셀을 다시 보내면 이미지 버전을 올리지 않는다. 렌더링 전에 여러 갱신을 받으면 변경 영역을 누적한다. 한 타일 내부의 여러 변경은 그 타일 안의 사각형 범위로 합칠 수 있다. 연결이 끊어지면 해당 앱의 창이 정리된다.

출력·투영코어의 주소와 장면 뱅크 배치는 [보시오 메모리맵](BOSIO_OUTPUT.md)을 따른다. 호스트의 부분 갱신 회귀 시험은 [v14 소스의 테스트](../../updates/boayo-tilebatch-v14/tests/test_partial_projection.py)에서 실행할 수 있다. 동일 이미지를 다시 보냈을 때의 무전송·무투영과, 부분 갱신 결과가 처음부터 다시 만든 장면과 같은지를 검사한다.

## 보시오 타일 전송·투영 측정

실보드에서 `/dev/bosio0`을 단독으로 사용한다. 보시오 미들웨어가 켜져 있으면 먼저 `bosio-tiles stop`으로 장치를 닫는다.

```sh
bosioctl bench 1 21 > /data/bosio-single.csv
bosioctl bench 16 21 > /data/bosio-16.csv
bosioctl bench > /data/bosio-all.csv
```

기본값은 타일 1·4·16·64·128개, 각 조건 21회다. 타일 수와 반복 횟수는 각각 1–128, 1–1000 사이에서 지정할 수 있다. 전송 비교는 **BS25 결과 타일 1개당 1,024바이트**를 사용한다.

| 시나리오 | 측정 내용 |
|---|---|
| `upload-span` | 인접 타일 전체를 `BO_UPLOAD` 한 번으로 전송 |
| `upload-per-tile`, `upload-gapped` | 인접 타일 또는 한 타일씩 띄운 주소에 타일마다 `BO_UPLOAD` 호출 |
| `upload-direct-span` | 사용자 페이지에서 직접 DMA로 인접 타일 전체 전송 |
| `dma-copy-span` | DDR 텍스처 영역에서 스크래치로 연속 영역을 DMA로 복사 |
| `dma-batch-adjacent`, `dma-batch-gapped` | 타일별 DMA 복사 명령을 한 번에 제출. 목적지는 인접하거나 한 타일 간격 |
| `projection-core0`, `projection-dual` | 합성 32×32 RGB 텍스처 업로드, 작업 기술자 업로드, 투영 완료 대기, 세 단계 합계. 타일 1개는 코어 0만 실행하며, 64개를 넘으면 두 코어에 나눈다. |

CSV의 `ioctls`는 커널에 요청한 횟수다. DMA 배치 한 번에도 타일별 복사 작업이 들어간다. `bytes`는 전송 단계에서는 전송량, 투영 실행과 합계 단계에서는 결과 크기다. 각 조건에서 예열 1회 뒤 최소·중앙값·95백분위 시간을 ns로 기록한다. DDR 내부 복사에 필요한 원본 적재와 매회 스크래치 검증은 측정 구간 밖에서 수행한다. `BO_UPLOAD` 경로가 DMA로 바뀌는 크기는 도구가 표준 오류에 출력하며, 지원하지 않는 선택적 DMA 경로는 건너뛴다.

측정은 표시용 뱅크를 게시하지 않고 보시오 스크래치에 기록한다. 수치에는 해당 ioctl의 검증·완료 대기 시간이 들어간다. 앱의 변경 영역 탐색, 보아요, HDMI 프레임 전환은 이 시험 범위 밖이다.

원본 2D 이미지가 DDR에 있는 상태만 측정하려면 `bosioctl bench-resident`를 사용한다.
타일 수와 반복 횟수는 `bench`와 동일하게 지정한다. 전체 크기의 원본을 시작 시 DDR에 한 번만 올리고,
타일 수 조건을 바꿀 때도 같은 원본의 앞부분을 사용한다. `total`은 작업 목록 전송을 시작하고 코어가
완료될 때까지의 시간이다. 타일 1개는 코어 0, 여러 타일은 두 코어가 나눠 처리한다. 같은 원본의 좌우
샘플링을 번갈아 적용해 매회 실제로 새 결과가 기록됐는지 확인하며, 이 읽기 검증은 시간에서 제외한다.

완료 IRQ를 비교할 때는 보아요와 보시오 타일 데몬을 종료하고, IRQ 5·6번을 연결한 비트스트림과 해당 DTB로 부팅한다.
측정 전후의 `/proc/interrupts`에서 `bosio-projection0`, `bosio-projection1` 증가량을 함께 기록한다.
기본 21회 측정에서 각 타일 개수별 워밍업을 포함하면 코어 0은 110회, 코어 1은 88회 증가한다.

2026-11-18 IRQ·캐시 가능한 작업 목록 검사 버전의 실보드 중앙값은 다음과 같다. 원본을 DDR에 한 번 올린 뒤 작업 목록 전송부터 완료까지 측정한 값이며 각 조건은 예열 1회와 본 측정 21회다.

| 출력 타일 수 | 사용하는 코어 | 작업 목록 전송부터 완료까지 |
|---|---|---:|
| 1 | 0 | 8.872ms |
| 4 | 0·1 | 15.878ms |
| 16 | 0·1 | 35.388ms |
| 64 | 0·1 | 120.086ms |
| 128 | 0·1 | 248.015ms |

합성 32×32 원본 타일과 정해진 디스크립터를 사용하는 보시오 단독 시험이다. 임의 창 이동의 CPU 좌표 계산·마스크 생성 시간은 포함하지 않는다. [원본 CSV](validation/bosio-resident-irq-sorted-20261118.csv)와 [보아요를 포함한 실보드 결과](VALIDATION.md)를 함께 참고한다.

## 빌드와 회귀 시험

Linux/WSL의 저장소 루트에서 실행한다.

```sh
bash software/userland/build.sh /var/tmp/haebancheon-dev all
cd variants/l1-16k-dma3-lru-ram
bash software/bosio/tests/run_engine.sh /var/tmp/haebancheon-engine
bash software/bosio/tests/run_rtl.sh /var/tmp/haebancheon-projection --scratch
cd ../..
bash updates/boayo-tilebatch-v14/software/boayo/native/build.sh /var/tmp/haebancheon-tiles-host host
python3 updates/boayo-tilebatch-v14/tests/test_partial_projection.py /var/tmp/haebancheon-tiles-host/boayo-native-host
```

개발 이미지의 의존성 설치·앱 배포 절차는 [빌드 안내](README.md)를 따른다. 단독 타일 미들웨어도 같은 native 소스에서 빌드되며 실행 이름 `bosio-tiles`로 런처 없이 시작한다.

실보드 부분 갱신 확인용 [C 클라이언트](tests/boayo_partial_board.c)는 빈 장면의 미들웨어에서 사용한다. `boayo stop`으로 UI를 닫고 별도 앱 연결도 종료한 뒤 `bosio-tiles status`에서 `service.phase=ready`, `windows=[]`를 확인한다. SDK로 빌드한 실행 파일을 [자체 앱 배포 절차](README.md#자체-앱-빌드포함실행)에 따라 옮기거나, 보드에서 `gcc -Os boayo_partial_board.c -o /data/apps/boayo-partial-check`로 빌드해 실행한다. 로그의 `BOAYO_HARDWARE_PARTIAL_OK`는 초기 표시·이동·부분 변경·동일 픽셀 재전송·창 제거의 완료 확인을 나타낸다. 시험 전후의 `/proc/interrupts`도 함께 남긴다.
