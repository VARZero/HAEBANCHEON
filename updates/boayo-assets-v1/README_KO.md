# BoAYO 완성된 2D 이미지 재사용 — assets-v1

## 변경 내용

첫 런처의 둥근 테두리·원·아이콘·고정 문구를 FPGA CPU가 매번 계산하던 경로를 미리 생성한 이미지 로딩으로 바꿨다. 원본 그림과 투영 결과를 기존 rgbdma-v1과 동일하게 유지한다.

- PC에서 런처 선택·호버·스크롤 상태, 창 테두리, 캡션 버튼, 보관된 창 표시를 포함한 132개 UI 변형을 생성한다. 고정 문구와 글자도 최종 픽셀에 포함된다.
- 파일은 무손실 `BUIRLE01` 형식이다. 각 연속 구간에 픽셀 수와 투영기 입력 색상 `0x00RRGGBB`를 저장한다. 길이·크기·전체 키·CRC를 검사한다.
- 보드에서는 구간 데이터를 RGB24 UI 버퍼와 32비트 투영 입력으로 한 번 확장한다. 큰 RGB 구간은 기존 RGB DMA 조건에 맞으면 DMA를 사용한다. 32비트 구간 확장과 일부 메모리 복사는 CPU 작업이다. 이미지 파일을 그대로 DMA에 주는 방식은 아니다.
- 확장한 데이터는 렌더 스레드의 24MiB 캐시에 보관한다. 다른 객체에서 사용 중인 불변 투영 데이터는 참조가 끝날 때까지 보존한다.
- 원본이 미리 생성된 이미지이면 기존 RGB24→32비트 픽셀별 변환을 생략한다. 내용 변경 시 준비된 데이터 연결을 해제해 오래된 결과를 쓰지 않는다.
- 첫 런처 및 고정 UI 상태 교체에는 변경 영역 검색과 전체 픽셀 복사를 하지 않고 버퍼 소유권을 옮긴다. 같은 불변 이미지의 재게시도 생략한다. 다른 고정 이미지로 교체하면 CPU 비교를 생략하는 대신 해당 창 전체를 다시 투영한다.
- 투영기의 현재 텍스처 영역에 같은 불변 원본이 남아 있으면 재업로드를 생략한다. 다른 원본을 업로드하거나 오류가 나면 이 상태를 해제한다. 모든 창을 동시에 DDR 텍스처 영역에 상주시키는 구현은 아니다.
- 투영 결과의 마스크·창 순서 합성은 기존 CPU 경로를 유지한다. 임의 도형·동적 그래프·투명도 합성을 하드웨어로 추가한 변경은 아니다.

## 하드웨어와 드라이버

기존 투영코어·출력코어·DMA RTL·MIG·HDMI IP와 비트스트림을 그대로 사용한다. Linux BOSIO 및 DMAengine 드라이버도 rgbdma-v1과 동일하다. 기존 DMA 업로드 경로는 커널 임시 복사 후 DMA로 전송하므로 완전한 무복사 방식은 아니다.

비트스트림: `D:\temp\gen3bosio\sd4-dma\candidate\ORYUKDO-KFB-SD4-DMA-v1.bit`

SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`

새 합성·배선을 실행하지 않았으며 FPGA 리소스나 클럭 개선을 주장하지 않는다.

## SD 이미지

- 이미지: `D:\temp\gen3bosio\boayo-assets-v1\ORYUKDO-KFB-BoAYO-assets-v1.img`
- 커널: `6.12.0-oryukdo-assets1`
- 식별자: `/etc/oryukdo-image`의 `ORYUKDO-BoAYO-assets-v1`
- BusyBox 및 SD ext4 `/data` 유지.
- 132개 파일: `/data/boayo-assets/`. 생성 파일 합계 3,400,330바이트(목록 파일 제외).
- 기본 첫 런처는 `/usr/share/boayo/assets/`에도 넣어 /data 없이 시작하는 환경에서도 사용한다.
- SHA-256 및 실제 이미지 크기는 배포 폴더 `SHA256SUMS.txt`와 `result.json` 참조.

보드 전원을 끈 상태에서 SD 전체 이미지를 기록·검증하고, 전원을 켠 뒤 기존 비트스트림을 올린다. 기록은 기존 SD 파일을 덮어쓴다. 이 작업에서는 실보드를 재시작하거나 새 이미지를 적용하지 않았다.

```sh
uname -r
cat /etc/oryukdo-image
boayo-start
```

첫 데스크톱 초기화 중 `asset.loaded` 또는 `asset.cache` → `surface.publish.move` → `desktop.end`가 표시되어야 한다. 기존처럼 `rounded.begin`, `circle.begin`으로 첫 런처를 그리는 경로에 들어가면 파일 누락·키 불일치·손상에 따른 대체 경로인지 확인한다. 실제 첫 프레임 표시와 HDMI·WASD는 별도로 확인해야 한다.

파일이 없거나 검사에 실패하면 기존 UI 그리기로 돌아간다. `BOAYO_ASSET_REJECT`는 손상 파일 진단이다. DMA 실패는 파일 손상으로 취급하지 않고 기존 오류 정책을 따른다. 비교 실행에는 `BOAYO_ASSETS=0`, 별도 파일 디렉터리에는 `BOAYO_ASSET_DIR=/경로`를 사용할 수 있다. 이미 실행 중인 서비스의 환경을 변경하는 기능은 아니다.

## 검증

- 132개 이미지: 이전 실행 파일의 RGB24 픽셀과 정확히 일치.
- 준비된 투영 입력: 창 이동·부분 수정·전체 색 변경·런처 복원 후 최종 투영 장면이 이전 버전과 정확히 일치.
- 동일 불변 원본의 위치 변경: 투영 작업은 수행하되 텍스처 업로드는 생략되는지 검사.
- 원본 변경·대체 후 오래된 준비 데이터가 남지 않는지 검사.
- 파일 누락·잘림·CRC 오류·헤더 오류·구간 오버플로를 거부하고 같은 화면의 CPU 대체 경로로 복귀하는지 검사.
- 기존 기능 시험 9개를 자산 경로 ON/OFF 각각 실행.
- 기존 RGB DMA 드라이버 경계 모델 480조합 및 오류·시간 초과 수명 검사 유지.
- 실제 RV32 앱·Linux 커널·내장 initramfs·SD 페이로드 CRC·ext4 파일 내용·원본 소스 일치 검사.
- QEMU는 실제 새 SD 페이로드로 셸·첫 자산 로딩·BoAYO 앱·WASD를 검증한다. BOSIO는 모의 장치이고 /data의 전체 자산은 호스트 및 이미지 파일 감사에서 확인한다.
- SD 로더 RTL 시험은 핀 수준 SD 모델과 모의 DDR이며 CPU Linux 부팅 시험은 아니다.

최종 결과는 `evidence/`와 배포 폴더 `result.json`에 기록한다. 실보드 성능·HDMI는 미검증이다. 기존 RGB DMA에서 측정한 311.6초→11.5초는 다른 버전의 배경 채우기 결과이며, 본 후보 전체 시작 시간의 측정값으로 사용하지 않는다.

## 재현

WSL Ubuntu, 저장소 루트에서:

```sh
bash updates/boayo-assets-v1/build.sh /var/tmp/oryukdo-boayo-assets-v1/release
python3 updates/boayo-assets-v1/tests/test_assets.py /var/tmp/oryukdo-boayo-rgbdma-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-assets-v1/host/boayo-native-host /var/tmp/oryukdo-boayo-assets-v1/release/data-root/boayo-assets /var/tmp/oryukdo-boayo-assets-v1/reports/assets
```

기본 빌드 입력은 `/var/tmp/oryukdo-boayo-rgbdma-v1/release`의 Linux 소스·설정·root·OpenSBI다. `BASE_BUILD`로 바꿀 수 있고 별도 트리에 복사하여 원본을 보존한다. 호스트 실행 파일에서 자산 기능을 끄고 UI를 생성하므로 생성 과정이 이전 자산에 의존하지 않는다. 커널과 앱 툴체인은 기존과 같다. `tests/verify_build.py`, `tests/test_image.py`, `package.py`가 이미지 감사·QEMU·배포 묶음을 담당한다.
