> **추가 확인:** [2026-11-15 v12 SD 실보드 콜드 부팅](COLD_BOOT_KO.md)에서 Linux 쉘·BoAYO·실제 HDMI 화면까지 확인했다. 아래 RAM 적용 검증 당시의 미확인 상태를 갱신한다.

# HAEBANCHEON — 창 이동 경계 마스크 최적화 v12

`boayo-incremental-v11-fix1`을 보존하고 별도 작업본에서 수정했다. RTL·비트스트림·Linux BOSIO 드라이버·DMA 인터페이스는 변경하지 않았다. 실보드 측정 결과는 [BOARD_RESULT_KO.md](BOARD_RESULT_KO.md)에 기록했다.

## 처리 변경

기존 `Compositor::prepare_masks()`는 타일의 1,024개 샘플마다 제곱근으로 행을 구하고, 투영 좌표를 나누고, JSON 도형을 읽어 둥근 사각형을 판정했다. 창 이동 후 새 기하에서는 이 경로가 수 분 동안 실행됐다.

- JSON 도형을 레이어당 한 번 숫자 구조체로 변환한다.
- 삼각형의 샘플을 행별 1·3·5·…·63개 순서로 순회한다. 좌표는 정수 덧셈으로 갱신하며 샘플마다 제곱근과 곱셈을 반복하지 않는다.
- 검증된 정수 사각형은 나눗셈 전의 동차 좌표로 바깥 영역을 제외하고 중앙 띠를 판정한다. 둥근 모서리 또는 비정수 도형은 기존 double 식과 연산 순서를 유지한다.
- 타일 전체가 하나의 중앙 띠에 포함되는 경우 8개 보수적 꼭짓점 검사로 전체 마스크를 확정한다. 서로 다른 띠에 들어간 꼭짓점을 합쳐 타일 내부라고 추정하지 않는다.
- 반열린 사각형 경계, 원호의 등호, 여러 도형의 합집합, 마스크 비트 순서, 출력 전환 순서를 유지한다. 전체 장면 초기화·CPU 장면 생성·출력 결과 읽어오기를 추가하지 않는다.

빠른 정수 경로는 4,096 이하의 정수 도형과 제한된 좌표 범위에만 적용한다. 범위 밖이나 비정수 도형은 정밀 경로로 처리한다. `BOAYO_MASK_REFERENCE=1`은 비교 검증용 기존 계산이다.

## 이동 시 기하 계산 제외

실보드의 첫 수정본에서 마스크 이전 `Geometry::descriptor()`의 소프트웨어 double 계산이 오래 남는 것을 PC 표본으로 확인했다. 원본 descriptor 식을 변경하는 대신, 고정된 4,220개 타일을 최대 32개씩 132개 묶음으로 나누고 각 묶음의 꼭짓점 범위를 미리 저장했다.

창의 투영 영역 밖임을 확실히 증명할 수 있는 묶음만 건너뛴다. 기존 affine upper bound, descriptor 정수 반올림 오차, 소수 연산 여유를 포함한다. 극단적으로 좁은 FOV 등 제한 밖은 원래 경로를 사용한다. 남은 타일의 descriptor와 가시성 판정은 기존 함수를 그대로 사용한다. `BOAYO_GEOMETRY_REFERENCE` 환경 변수를 설정하면 이 제외 처리를 끌 수 있다.

`tests/test_culling.cpp`는 고정 범위가 현재 모든 꼭짓점을 포함하는지 확인하고, 1,200개 임의/극단 투영의 5,064,000개 타일에서 기존 함수가 유효하다고 판정한 670,914개를 하나도 제외하지 않았음을 검사했다. 전체 2,948,212개가 제외됐다. 이는 시험 분포의 개수이며 실보드 속도 개선율이 아니다. `generate_culling_bounds.py`로 범위를 재생성할 수 있다. 상태의 기존 `geometry_tiles_evaluated_total`은 후보 전체를 세는 기존 계수로, 이 제외 처리 이후 실제 descriptor 호출 수를 나타내지는 않는다.

## 사전 계산 계획의 호환성

소스 변경으로 계획의 버전 해시가 달라진다. 기존 계획을 무조건 신뢰하거나 검사를 끄지 않는다. 새 알고리즘으로 기존 **37개 계획의 descriptor·분류·byte/packed mask 전체 payload를 재생성해 모두 일치**함을 확인했다.

`plan_compatibility.json`의 검증된 소스 해시와 현재 해시가 정확히 같을 때만 `plan_revision.py`가 기존 payload 버전을 허용한다. 미래 소스 변경 시 허용이 자동으로 사라지는 검사도 통과했다. 외부 계획의 CRC·구조·ready 명령 검사는 그대로다. 689개 그림 에셋과 기존 계획 파일은 변경하지 않았다.

## 로컬 검증

- 7,893개 타일, **8,082,432개 픽셀**의 byte mask·packed mask·전체/부분/빈 분류가 기존 계산과 일치.
- **600,000개 경계 좌표** 비교. 정수/비정수, 둥근/각진 사각형, 여러 영역, 폭 1, 큰 분모, descriptor 부호 변화 포함.
- 새 마스크 코드에 AddressSanitizer/UndefinedBehaviorSanitizer를 적용한 동일 검사 통과. 링크된 기존 객체 전체를 계측한 검사라는 뜻은 아니다.
- 기존 **120개 장면** 회귀, 상태 250개 스냅샷, 텍스처 풀·프레임 주기·시점 서비스 검사 통과.
- 저장 계획 37개 payload의 바이트 일치, 소스 변경 시 호환 허용 해제 검사 통과.
- host/RV32 소스·실행 파일·initramfs·커널·SD payload CRC·ext4·에셋 감사 결과는 `evidence/`에 기록한다.

## 계측

상태의 `projection`에 `mask_builds`, `mask_build_us_total`, `mask_fast_tiles_total`, `mask_integer_samples_total`, `mask_precise_samples_total`을 추가했다. 시간은 새 마스크 계산 구간의 누적 마이크로초이며, 기하 descriptor 생성·계획 조회·출력 장치 처리 시간 전체를 뜻하지 않는다. 매 픽셀 시간을 읽지 않고 마스크 빌드 전후에만 측정한다.

## 재현 및 이미지

`software/boayo/native/build.sh`로 host/RV32를 빌드한다. `tests/test_masks.cpp`, `tests/run_proof.py`, `tests/prove_plans.py`, `tests/sanitize_masks.py`, `tests/test_compatibility_guard.py`가 신규 검사다. 전체 회귀는 `../boayo-incremental-v11/tests/run_regressions.sh`에 v12 작업 디렉터리와 fix1 기준 실행 파일을 지정한다.

`build_image.sh`로 SD를 생성하고 `tests/verify_build.py`, `tests/test_image.py --boot-only`로 검사한다. QEMU에서는 BOSIO Mock을 사용하므로 실제 보드 검증과 구분한다.

- 파일: `D:\temp\haebancheon-mask-v12\HAEBANCHEON-KFB-BoAYO-mask-v12.img`
- 크기: 311,427,072바이트.
- SD SHA-256: `a14cb702636b07bce056261778d432abe92703604291f7b543be9515cfb63d90`.
- 앱 SHA-256: `c6e9114ca5398f84e0c7bc2522bda71be7f064a6781e950f4bd20e6ecca0dc51`.
- 비트스트림: 기존 `HAEBANCHEON-KFB-L1-16K-DMA3-v1.bit`.

실보드 RAM 적용은 동일 v12 앱을 v11 커널에서 실행하는 방식이다. 새 SD 전체의 실보드 콜드 부팅을 대신하는 증거로 쓰지 않는다.

