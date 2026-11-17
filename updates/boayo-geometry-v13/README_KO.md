# HAEBANCHEON BoAYO 투영계수 최적화 v13

창을 새 위치로 이동하거나 크기를 바꿀 때, 미리 만든 투영 계획이 없는 레이어의 CPU 계산을 줄였다. v12는 보존하고 `updates/boayo-geometry-v13`에 별도로 구현했다. 실보드 결과는 [BOARD_RESULT_KO.md](BOARD_RESULT_KO.md)에 기록한다.

## 변경한 계산

1. 고정 삼각형 4,220개의 경계 자료를 제작 단계에서 생성한다. 실행 중 32개 단위로 먼저 제외하고, 남은 그룹을 4개 단위로 다시 검사한다. 화면에 필요할 가능성이 있는 타일만 기존 투영식을 실행한다. 첫 이동의 창 본문·버튼 4개 레이어에서 후보 합계는 3,136개에서 484개로 줄었다. 실제 유효 타일 170개는 같다.
2. 계수를 정수로 바꾸기 위한 스케일은 일반적인 경우 부동소수점 지수로 구한다. 2의 거듭제곱 경계 근처와 예외 값은 기존 `log2/exp2` 경로로 돌려 반올림 차이를 피한다. 좌표 변환의 나눗셈을 역수 곱셈으로 바꾸거나 계수 정밀도를 낮추지 않았다.
3. 계산 준비·후보 선택·계수 생성·레이어 삽입을 따로 누적한다. 정상 경로는 레이어 캐시 미스마다 시계 5회만 읽으며 타일별 로그를 출력하지 않는다. 더 세밀한 좌표 변환·계수식·스케일·정수 인코딩 시간은 별도 벤치마크에서 측정한다.

경계 검사에는 정수화 오차와 부동소수점 오차 여유를 포함한다. 극단적으로 작은 시야각이나 검사에 부적합한 값은 전체 후보 경로로 돌아간다. 무작위 비교 통과는 시험 범위의 근거이며 모든 실수 입력에 대한 형식 증명으로 해석하지 않는다.

CPU는 새 창 배치의 계수·경계 마스크를 준비하고, 기존 투영코어가 타일 픽셀을 생성하며 DMA가 복사·필요 영역 채우기를 수행한다. 빈 장면 메타데이터 때문에 장면버퍼 전체를 CPU로 0 초기화하는 경로를 추가하지 않았다. RTL, MIG, HDMI IP, Linux BOSIO 드라이버는 변경하지 않았다.

## 검증 범위

- 원래 함수를 비교용으로 보존. 5,064,000개 타일의 계수·표시 판정 일치, 표시 타일 잘못 제외 없음.
- 스케일 361,190개 경계·무작위 값 일치. ASan/UBSan으로 geometry 구현을 계측한 동일 시험 통과.
- 기존 v12와 120개 장면, 250개 상태 비교 및 텍스처 풀·갱신 주기·시점 서비스 회귀 시험 통과.
- 기존 37개 투영 계획의 payload 바이트 일치. 호환성 허용은 현재 소스 지문에 묶여 있다.
- 호스트/RV32 빌드 소스 일치, SD CRC·파티션·ext4·앱·커널·드라이버·689개 에셋 확인.
- 새 SD의 QEMU 부팅·BusyBox 셸·상위 메모리 검사·BoAYO 자체 시험 통과. QEMU는 Mock이며 FPGA/RTL 검증으로 세지 않는다.
- 실보드는 v12 SD/커널에서 동일 새 RV32 앱을 RAM에 올려 시험한다. 새 v13 SD 자체의 실보드 콜드 부팅은 별도다.

## 재현

WSL Ubuntu에서 기본 작업 경로는 `/var/tmp/haebancheon-geometry-v13`이다. 빌드는 기존 `software/boayo/native/build.sh`의 host/RV32 방식을 사용한다. `evidence/host-build.json`, `rv32-build.json`에 실제 소스 스냅샷과 해시가 있다.

```sh
python3 tests/generate_rejection_bounds.py --check
python3 tests/run_geometry_proof.py /var/tmp/haebancheon-geometry-v13
python3 tests/build_geometry_benchmark.py /var/tmp/haebancheon-geometry-v13
python3 tests/sanitize_geometry.py
```

`run_geometry_proof.py`는 `host/boayo-native-host.build.json`의 빌드 스냅샷을 사용한다. 벤치마크도 해당 host/RV32 스냅샷의 geometry/wm 객체와 링크한다. 기존 `run_proof.py`는 v12 마스크·그룹 검사 보존용이다. 전체 회귀 시험은 `../boayo-incremental-v11/tests/run_regressions.sh`에 작업 경로를 전달하고 `BASELINE_BINARY`를 v12 host 바이너리로 지정했다.

실보드 비교는 같은 v13 앱에서 `BOAYO_GEOMETRY_V12=1`로 기존 계산 경로, `=0`으로 최적화 경로를 선택한다. `BOAYO_PROFILE=1`은 비교 계측 때만 켠다. `BOAYO_MOTION_PROFILE=1`은 CLI에서 단계 완료 후 누적 계측을 기록한다. `motion 1 0 11`은 첫 이동까지, 기본 `motion 1 0 25`는 이동·크기 변경·겹침·닫기·복귀까지 실행한다. 이 명령은 실행 중 창 상태를 변경하므로 일반 사용자 세션에 무단으로 실행하지 않는다.

## 새 SD

- 파일: `D:\temp\haebancheon-geometry-v13\HAEBANCHEON-KFB-BoAYO-geometry-v13.img`
- 크기: 311,427,072바이트
- SHA-256: `dccb9042d390db89edc8dd877157aa575cc7f0f2342c77e051b4258435684eab`
- 앱 SHA-256: `2bc3f60f8ce05531d21e14594362b374d60a5a2c7b8ac58a203e77c48f7e5a06`
- 기존 `HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit` 사용. 새 합성·배선은 수행하지 않았다.
- BusyBox와 `/data` 파일시스템, 기존 에셋 및 계획을 유지한다. 새 SD 기록은 사용자가 보드 종료 후 수행한다.

`build_image.sh`는 기존 로컬 Linux/toolchain/에셋 빌드 디렉터리를 재사용하므로 독립 인터넷 다운로드 빌더가 아니다. `BASE_BUILD`, `PREVIOUS_BUILD`와 스크립트의 toolchain 경로를 설치 환경에 맞춰야 한다.
