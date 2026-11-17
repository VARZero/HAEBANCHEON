# BoAYO 원본 텍스처 직접 DMA — rawdma-v1

## 문제와 변경

assets-v1 실보드에서 창 설정 완료 후 이미지 준비까지 약 262.7초, 그 뒤 창 적용까지 약 53.4초가 걸렸다. 해당 측정은 UART 상태 조회와 병행한 값이며 파일 읽기·CRC·확장 각각을 분리한 측정은 아니다. 이 후보의 실보드 성능은 아직 측정하지 않았다.

고정 UI 132개 상태를 PC에서 투영코어 입력 그대로 생성한다. 파일 픽셀은 **리틀엔디언 32비트 0x00RRGGBB**다. 보드에서 RLE 압축 해제, RGB24 중간 버퍼 생성, 픽셀 형식 변환, RGB 버퍼 복사를 하지 않는다.

1. 작은 헤더의 형식·크기·전체 키·메타데이터 CRC를 확인한다.
2. 원본 픽셀 영역을 읽기 전용으로 파일 매핑한다. 픽셀 데이터는 사용자 공간으로 읽어 복사하지 않는다.
3. 창은 공유 매핑을 참조한다. 초기 창의 임시 RGB 버퍼와 배경 초기화도 생략한다.
4. 새 `BO_UPLOAD_DIRECT`에서 매핑의 원본 페이지를 읽기 전용으로 고정하고 DMA 주소로 매핑한다.
5. DMA가 원본 페이지에서 기존 BOSIO 텍스처 DDR 영역으로 전송한다. 기존 투영코어에 같은 디스크립터를 전달한다.
6. 같은 텍스처가 아직 DDR에 남아 있으면 재전송을 생략한다.

여기서 없앤 복사는 CPU의 픽셀 중간 복사다. **DMA에 의한 DDR 전송 자체와 SD 파일 읽기는 필요하다.** 파일 페이지가 아직 메모리에 없으면 Linux가 읽어와야 한다. 초기 initramfs 구성·파일시스템·페이지 고정·DMA 명령 설정에도 CPU가 관여한다. 전체 부팅이나 합성이 무복사라는 뜻은 아니다.

## 그대로 유지한 부분

CPU·투영코어·출력코어·DMA RTL·MIG·HDMI IP, 기존 비트스트림은 변경하지 않았다. DMAengine 및 SD 드라이버도 기존 그대로다. BOSIO Linux 드라이버에 원본 페이지 업로드 ioctl과 기능 비트를 추가했다. 기존 ioctl 구조·번호와 ABI3는 보존한다. 새 고정 UI 경로는 새 드라이버 기능을 요구하며, 임시 복사 업로드로 몰래 대체하지 않는다.

동적 그래프·그림 수정이나 소프트웨어 렌더러가 실제 RGB 픽셀을 요구할 때만 RGB 버퍼를 만든다. 부분 수정은 원본 픽셀을 유지하며 변환하고, 전체 색 채우기는 원본 변환 없이 덮어쓴다. 이후 오래된 불변 텍스처를 재사용하지 않는다. 마스크·창 가림·최종 타일 합성은 기존 CPU 처리다. 모든 창 텍스처를 동시에 하드웨어 DDR에 보관하는 변경은 아니다.

## 파일 형식과 무결성

`BUITEX01` 파일은 4096바이트 헤더 다음에 연속된 원본 픽셀을 둔다. 헤더에는 폭·높이·키 길이·픽셀 바이트 수·픽셀 CRC·메타데이터 CRC와 전체 키가 들어 있다. 파일 크기와 픽셀 수를 제한하고 잘림·불일치·잘못된 헤더는 기존 CPU 그리기로 복귀한다.

일반 실행은 **픽셀 전체 CRC를 CPU로 순회하지 않는다.** 픽셀 CRC는 `BOAYO_ASSET_VERIFY=1` 진단 모드에서만 검사한다. 빌드·배포 검증에서는 132개 파일 전체 SHA-256과 ext4에 들어간 바이트를 검사한다. 정상 실행 중 픽셀 데이터만 손상된 경우 즉시 검출한다고 주장하지 않는다. 실행 중 자산 파일을 수정·잘라내지 말아야 한다. 자산이 잘못된 경우 헤더가 유효해도 잘못된 색으로 보일 수 있다.

`BOAYO_ASSETS=0`: 비교용 기존 CPU 경로. `BOAYO_ASSET_DIR`: 별도 자산 디렉터리. 새 프로세스를 시작할 때 환경을 지정한다.

## DMA 페이지 수명

원본은 `FOLL_WRITE` 없이 고정하고 `DMA_TO_DEVICE`로 매핑한다. 사용자가 DMA 중 원본을 수정하거나 매핑 해제해서는 안 된다. 부분 핀 실패·DMA 매핑 실패·정상 완료에서는 획득한 자원만 반환한다. 전송 일부 완료 후 실패하면 장치를 오염 상태로 표시한다. 시간 초과 시 원본 페이지와 DMA 매핑을 유지해 늦은 DMA 읽기가 재사용된 메모리를 읽지 않게 한다. 사용자 프로세스는 즉시 종료하며 보드 리셋이 필요하다. 기존의 비취소 DMA 정책을 유지한다.

## 결과물

- 크기: 277,872,640바이트(265MiB). `/data` 시작 LBA 18432, ext4 256MiB.
- SD: `D:\temp\gen3bosio\boayo-rawdma-v1\ORYUKDO-KFB-BoAYO-rawdma-v1.img`
- 커널: `6.12.0-oryukdo-rawdma1`
- 식별자: `ORYUKDO-BoAYO-rawdma-v1`
- 원본 UI 파일: `/data/boayo-assets/`, 132개, 파일 합계 81,224,192바이트.
- 기본 런처는 `/usr/share/boayo/assets/`에도 포함해 /data가 없는 모의 부팅에서도 확인한다.
- BusyBox·ext4 /data 유지. 기존 Python 버전과 이전 후보는 별도 디렉터리에 보존한다.
- 기존 비트스트림: `D:\temp\gen3bosio\sd4-dma\candidate\ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`

SD 전체 기록은 기존 카드 내용을 덮어쓴다. 현재 보드 실행은 이 작업에서 중단하거나 교체하지 않는다. 이후 SD를 교체할 때 전원을 끄고 새 이미지를 기록·검증한 뒤 기존 비트스트림을 다시 올린다.

```sh
uname -r
cat /etc/oryukdo-image
boayo-start
boayo-status
```

첫 초기화에서 `asset.map.begin` → `asset.loaded` → `surface.publish.move` → `desktop.end`가 나와야 한다. `rgb.dma.begin`, `rounded.begin`, `surface.copy.begin`은 정상적인 첫 고정 런처 준비에서 나오지 않아야 한다. `desktop.end`는 준비 완료이며 HDMI 표시 완료와 다르다. 출력 commit과 실제 캡처를 별도로 확인한다.

## 검증 범위

- 기존 기능 9개를 원본 자산 ON/OFF로 각각 실행.
- 132개 UI 픽셀 및 이동·부분 수정·전체 채우기·복원·호버 후 투영 장면을 rgbdma-v1과 비교.
- 첫 런처의 RGB 버퍼 0바이트, RGB 변환 0회, RGB 초기화 DMA 0회, 직접 업로드 선택을 검사.
- 매핑 참조 유지와 동일 텍스처 재업로드 생략, 수정 후 재업로드를 검사. 캐시 축출 이후에도 사용 중인 창의 매핑이 유효한지 ASan/UBSan 빌드로도 비교한다.
- 잘못된 메타데이터·파일 길이와 선택적 픽셀 CRC 검사 실패의 대체 동작을 검사.
- 실제 드라이버 함수의 호스트 모델에서 직접 DMA 36개 크기·정렬·SG 조합과 부분 핀·매핑·준비·제출·완료 실패·시간 초과·늦은 읽기 수명을 검사. ASan/UBSan 사용.
- 기존 RGB DMA 480개 조합 및 기존 DMA 정책·오류 시험 유지.
- 실제 RV32 앱/커널 빌드, initramfs 및 SD 페이로드·ext4 내용·소스 일치 감사.
- QEMU: 실제 SD 페이로드의 Linux·쉘·BoAYO·WASD. BOSIO는 모의 장치다. 실제 Linux 페이지 고정과 물리 DMA 전송의 실보드 검증을 대체하지 않는다.
- SD 로더 RTL: SD 모델과 모의 DDR. 전체 CPU Linux RTL 부팅이 아니다.

최종 실행 결과는 `evidence/result.json`과 개별 로그를 따른다. 실보드 첫 화면 시간·HDMI·직접 DMA 성능은 미검증이다. 합성·배선은 새로 수행하지 않는다.

## 재현

WSL Ubuntu에서 저장소 루트 기준:

```sh
bash updates/boayo-rawdma-v1/build.sh /var/tmp/oryukdo-boayo-rawdma-v1/release
python3 updates/boayo-rawdma-v1/tests/test_assets.py /var/tmp/oryukdo-boayo-rgbdma-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-rawdma-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-rawdma-v1/release/data-root/boayo-assets /var/tmp/oryukdo-boayo-rawdma-v1/reports/assets
```

기본 Linux 소스·설정·root·OpenSBI 입력은 `/var/tmp/oryukdo-boayo-rgbdma-v1/release`이며 `BASE_BUILD`로 지정할 수 있다. 원본을 보존하고 별도 출력 트리에 복사한다. `tests/verify_build.py`, `tests/test_image.py`, `package.py`가 이미지 감사·QEMU·배포를 담당한다.

커널 빌드에는 기존 `kernel/time/clocksource.c`의 `pr_fmt` 재정의 경고가 남아 있다. 해당 파일은 입력 기준과 바이트가 동일하며 이 변경에서 수정하지 않았다. 새 BOSIO 코드의 컴파일 오류는 없다.
