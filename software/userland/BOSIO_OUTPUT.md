# 보시오 출력 메모리맵

대상은 HAEBANCHEON의 `l1-16k-dma3-lru-ram` 비트스트림과 Linux 개발 이미지다. 주소는 CPU 기준 물리 바이트 주소이며, 표의 마지막 주소는 범위에 포함된다.

| 영역 | 물리 주소 | 크기 | 소유자와 용도 |
|---|---|---:|---|
| 출력코어 MMIO | `0x20020000–0x2002007F` | 128B | Linux BOSIO 드라이버가 BS25/DTF1 레지스터를 제어 |
| BOSIO 작업 영역 | `0xBD000000–0xBDFFFFFF` | 16MiB | Linux `no-map` 예약. 장면·텍스처·투영 작업·스크래치 |
| 장면 0 | `0xBD010000–0xBD43363F` | 최대 4,339,264B | 출력코어가 읽는 BS25 장면 |
| 장면 1 | `0xBD450000–0xBD87363F` | 최대 4,339,264B | 출력코어가 읽는 두 번째 BS25 장면 |
| HDMI 영상 0 슬롯 | `0xBF800000–0xBFBFFFFF` | 4MiB | `kfb_video` 전용 |
| HDMI 영상 1 슬롯 | `0xBFC00000–0xBFFFFFFF` | 4MiB | `kfb_video` 전용 |

HDMI 슬롯 두 개는 DT의 단일 8MiB `no-map` 영상 예약에 들어 있다. 한 프레임은 1280×720 픽셀이며 RGB888을 리틀엔디언 32비트 `0x00RRGGBB`로 저장한다. 행 간격은 5,120B이고 프레임은 3,686,400B다. 실제 프레임은 각 슬롯에서 `0xBFB83FFF`와 `0xBFF83FFF`까지 사용한다. RTL의 MIG 주소 `0x0FE00000`과 `0x0FF00000`은 DDR 내부 워드 주소다.

## 출력코어에 장면 전달

1. CPU가 비활성 장면에 팔레트, 디렉터리, 타일 데이터를 준비한다. 보아요는 `/dev/bosio0`의 `BO_UPLOAD`를 사용하고, 작업 영역 내부 복사나 채우기는 기존 BOSIO 메모리 DMA 경로를 사용할 수 있다.
2. `BO_PRESENT`에 장면 번호, 워드 수, 카메라 계수를 전달한다. 드라이버가 출력코어의 장면 주소와 길이(`+0x08`, `+0x0C`)를 설정하고 `+0x6C`로 전환을 요청한다. 활성 주소(`+0x44`)와 상태(`+0x04`)로 완료를 확인한 뒤 이전 장면을 재사용한다.
3. 출력코어가 장면을 읽어 RGB888 스트림을 보낸다. `kfb_video`가 HDMI 뒤쪽 슬롯에 프레임을 저장하고 완성된 프레임을 영상 경계에서 전환한다. 보아요와 복사 DMA는 장면 영역에서 작업하고, HDMI 슬롯은 `kfb_video`가 관리한다. `BO_PRESENT` 완료는 장면 전환 완료를 뜻하며 모니터 표시 완료는 별도다.

CPU에서 생성한 타일만으로 출력 경로를 구성할 수 있다. 이후 투영코어의 결과도 같은 비활성 장면에 넣고, 투영 작업과 DMA 완료를 확인한 뒤 장면을 게시한다. 필요할 때 [현재 출력 레지스터 표](../../docs/HAEBANCHEON_SOC_REVIEW_PROMPT_KO.txt)와 [BOSIO UAPI](../../variants/l1-16k-dma3-lru-ram/software/bosio/include/bosio_uapi.h)를 참고한다.

## 투영코어 제어

새 개발 이미지에는 `bosioctl`과 앱용 `/usr/include/bosio_uapi.h`가 들어간다. 보드에서 보시오 장치를 단독으로 열 수 있을 때 다음 명령을 실행한다.

```sh
bosioctl info
bosioctl project 0
bosioctl project 1
bosioctl project both
bosioctl demo
```

`project`는 고정 테스트 텍스처와 코어당 한 개의 18워드 디스크립터를 올린다. 지정한 코어를 실행한 뒤 스크래치 타일을 읽어 실행한 코어의 1,024개 셀과 실행하지 않은 코어의 타일이 그대로인지 확인한다. `PROJECT_OK`가 성공 표시다. `demo`는 두 코어가 만든 타일을 장면 0에 반영하고 출력코어로 게시한다. Enter를 누르면 출력을 정지한다.

| 리소스 | 시작 주소 | 크기 |
|---|---|---:|
| 투영코어 0 / 1 제어 | `0x20000000` / `0x20010000` | 각각 128B |
| 투영코어 0 / 1 디스크립터 | `0xBD000000` / `0xBD002000` | 각각 최대 64개 × 72B |
| 공유 텍스처 | `0xBD900000` | 4MiB |
| 스크래치 결과 | `0xBDD00000` | 256KiB |

앱은 `BO_UPLOAD`로 텍스처와 각 코어의 디스크립터를 올리고, `BO_RUN`의 `jobs[0]`과 `jobs[1]`에 작업 수를 지정한다. 드라이버가 코어별 범위와 결과 쓰기 중첩을 검사하고, 선택한 코어가 모두 완료될 때까지 기다린다. `bank=2`는 `BO_DOWNLOAD`로 읽을 수 있는 스크래치이고, `bank=0/1`은 비활성 표시 장면이다. CPU는 작업 목록을 준비하고, 투영코어가 AXI로 텍스처를 읽어 타일을 쓴다. 별도 복사 DMA는 큰 업로드와 작업 영역 내 복사·채우기에 사용할 수 있다.

## 이미지 검증

`software/userland/build.sh OUTPUT all`은 이미지에 들어가는 DTB에서 두 DDR 예약의 기준 주소·크기·`no-map`, BOSIO 장치의 MMIO, 작업 영역 연결을 검사한다. 성공하면 `OUTPUT/images/manifest.json`의 `bosio_output_map`에 출력 구성과 HDMI 슬롯 정보를 기록한다. 장면 오프셋은 [현재 드라이버의 레이아웃 표](../../variants/l1-16k-dma3-lru-ram/software/bosio/common/bosio_engine.c)를 따른다.

저장소 루트에서 Verilator로 각 코어와 동시 투영의 스크래치 결과를 검증할 수 있다.

```sh
bash variants/l1-16k-dma3-lru-ram/software/bosio/tests/run_rtl.sh /var/tmp/bosio-scratch-rtl --scratch
```

QEMU에서는 루트 파일시스템에 설치된 명령과 헤더를 확인한다. 두 투영코어의 실행과 DDR 결과는 Verilator 시험에서 확인한다.
