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

### 기존 커널에서 메타데이터 비교 비용 줄이기

BS25의 출력 타일을 한 개만 옮겨도 `BO_PUBLISH`는 4,476워드(17,904바이트)의 메타데이터를 받는다. 기존 드라이버는 이전 장면과 같은지 앞에서부터 비교해 전체 메타데이터 적재와 기존 메타데이터 재사용 중 하나를 선택한다. 색상표 뒤의 높은 번호 타일만 바뀌면 긴 동일 구간을 먼저 비교한다. 실보드에서 이 선택 구간은 약 144ms였다. 타일 투영 시간이 아니다.

유저 게시 경로는 BS25 색상표 첫 워드의 미사용 상위 바이트에 장면 뱅크별로 다른 값(1/2)을 넣는다. 기존 출력 RTL은 색상표에 `rdata[23:0]`만 저장하므로 모든 RGB 색상은 유지된다. 비교는 처음 네 바이트에서 끝나고, 검증·DMA·게시 완료 대기는 기존 드라이버를 그대로 사용한다. 이 변경은 **커널·DTB·RTL·비트스트림을 수정하지 않는다.**

태그를 뱅크별 고정값으로 유지하므로 예열 뒤 추가 색상표 쓰기가 발생하지 않는다. `LinuxDevice::publish`는 17,904바이트의 전송용 스냅샷에만 태그를 넣고 호출자의 논리 색상표는 수정하지 않는다. 알려진 BS25 시그니처에서만 활성화하며, 이 변경으로 다시 빌드한 실행 파일은 `BOSIO_METADATA_BANK_TAG=0`으로 기존 게시 경로를 선택할 수 있다.

2026-11-19 같은 실보드·같은 실행 파일에서 각 조건 예열 1회 후 21회 측정했다. 흰색 1KiB 타일 데이터를 두 뱅크에 한 번 설치하고 타일을 켜거나 8개 디렉터리 위치로 옮겼다. 아래 시간은 디렉터리 갱신·전송용 스냅샷 복사·`BO_PUBLISH`부터 드라이버 완료까지다. 원본 이미지·투영·좌표 계산·WM·IPC는 포함하지 않는다.

| 동작 | 기존 중앙값 | 개선 중앙값 | 기존 P95 | 개선 P95 |
|---|---:|---:|---:|---:|
| 타일 1개 활성화 | 269.164ms | 129.268ms | 285.006ms | 139.173ms |
| 타일 1개 위치 이동 | 139.128ms | 129.247ms | 269.153ms | 139.162ms |

활성화의 메타데이터 선택/비교 중앙값은 144.174ms에서 0.136ms로 줄었다. 이동의 긴 지연도 제거됐다. 실제 추가 업로드는 양쪽 모두 활성화 0워드·이동 2워드였고, 장치 오류는 없었다. 이 시험은 투영하지 않아 두 투영 IRQ 카운터는 변하지 않았다. [실보드 원시 비교·프로파일](validation/bosio-bank-tag-board-20261119.json)

이 방법은 동일 메타데이터의 patch 명령 대신 전체 메타데이터 적재 명령을 선택하게 한다. 따라서 내용만 바뀌는 다른 작업에서는 메타데이터 DDR 읽기 비용을 포함해 별도로 비교해야 한다. 현재 커널의 전체 범위 검증 약 35ms, 뱅크별 차이 탐색·스냅샷 처리 약 49ms와 출력 전환 대기는 남는다. WM 전체 지연과 실제 모니터 표시 시간은 이번 시험에서 측정하지 않았다.

직접 측정 프로그램은 [bosio_tile_publish_board.c](tests/bosio_tile_publish_board.c)다. 기존 교차 컴파일러로 다음처럼 빌드하고 기존 앱 배포 방식으로 보드에 옮긴다.

```sh
NATIVE=updates/boayo-tilebatch-v14/software/boayo/native
"$OUT/buildroot/host/bin/riscv32-buildroot-linux-musl-gcc" \
  -std=c11 -O2 -static -march=rv32ima_zicsr_zifencei -mabi=ilp32 \
  -I "$NATIVE/src" -I updates/boayo-tilebatch-v14/software/bosio/include \
  software/userland/tests/bosio_tile_publish_board.c -o /var/tmp/bosio-tile-publish
# 보드에서 WM을 종료한 뒤, 전송한 프로그램을 실행
boayo stop
/tmp/bosio-tile-publish
/tmp/bosio-tile-publish --bank-tag
```

현재 출력 RTL을 그대로 시뮬레이션해 256개 색상의 상위 바이트 변경 전후 512픽셀이 동일함을 검사한다. 실제 `LinuxDevice` 게시 코드는 ioctl 기록기로 태그·뱅크·호출자 데이터 불변성·기존 경로 선택·다른 시그니처·오류 처리를 검사한다.

```sh
bash software/userland/tests/check_bosio_palette_tag.sh /var/tmp/bosio-tag-rtl
bash "$NATIVE/build.sh" /var/tmp/bosio-tag-host host
python3 software/userland/tests/check_bosio_publish_transport.py \
  /var/tmp/bosio-tag-host/boayo-native-host.build.json
```

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
