# BoAYO 메타데이터 통합 전환·경계 비트마스크 — tiles-v4

## 범위와 현재 근거

보존된 `updates/boayo-tiles-v3`의 후속이다. `variants/sd4-dma/`의 RTL·비트스트림, CPU 스케줄러, 두 투영코어, BS25, MIG, HDMI는 변경하지 않는다. Linux/BusyBox와 SD4/ext4 `/data`, 기존 앱과 WASD 입력, Python 비교 자료를 유지한다.

v3 실보드에서 출력 메타데이터·전환 구간은 87.729877초였다. 당시 로그에는 내부 항목별 구분이 없어 **그 87.7초 중 어느 항목이 몇 초였는지는 확정할 수 없다.** 이번 버전은 중복 경로를 제거하고 세부 계측을 추가한다. 로컬 기능 검증과 새 SD의 실보드 속도 검증을 구분하며, 새 이미지로 보드를 변경하거나 재부팅하지 않는다.

## 개선한 처리 순서

1. 기존처럼 바뀐 타일만 준비·투영·합성한다. 전체 장면 초기화, CPU 픽셀 사본, 투영 결과 회수, 전환 뒤 즉시 반대 버퍼 복제는 없다.
2. 타일 생성 완료와 목적지 버전을 확인한다.
3. CPU의 메타데이터 4,476워드를 `BO_PUBLISH` 한 요청으로 전달한다. 기존의 여러 upload 요청과 별도 present 요청을 합쳤다.
4. 커널은 사용자 자료를 한 번 복사하여 고정된 스냅샷을 만든다. 입력을 다시 읽어 실행 중 변경되는 문제를 피한다.
5. 스냅샷의 디렉터리 범위·장면 길이·카메라·비활성 버퍼 소유권을 검사한다. 실패하면 업로드를 시작하지 않는다.
6. 목적지 버퍼의 커널 메타데이터 사본과 비교하여 바뀐 연속 구간만 기록한다. 목적지 사본이 유효하지 않으면 메타데이터 전체를 기록한다. **Cell Data는 기록하지 않는다.**
7. 마지막 전환 성공 자료와 같은 메타데이터/길이면 BS25 주소 전환, 다르면 전체 메타데이터 로드를 요청한다. CPU 스냅샷을 사용하므로 이 경로는 DDR 출력 매핑의 메타데이터를 다시 읽지 않는다.
8. 메모리 쓰기 완료·배리어 이후에 요청하며, 활성 주소와 완료 상태를 확인한 뒤에만 소유권을 바꾼다.

커널 메타데이터 사본은 두 버퍼분 약 35KiB가 추가된다. 픽셀 버퍼나 FPGA BRAM이 아니다. 기존 upload/direct upload/copy/fill/batch 경로에서 메타데이터를 덮을 수 있는 요청은 공통 검사 단계에서 사본을 보수적으로 무효화한다. 실패한 요청 때문에 불필요한 재업로드가 생길 수는 있지만 오래된 사본을 믿고 쓰기를 생략하지 않는다. 기존 ioctl은 유지하며 새 ioctl은 ABI3의 선택 기능 비트 128이다.

DMA 실패 시 이전 작업 일부가 완료됐을 수 있다. 장치는 실패 상태로 잠기며 새 장면을 표시하거나 버퍼를 재사용하지 않는다. DMA가 늦게 완료될 수 있는 경우에는 기존 소스 매핑/페이지 유지 정책을 그대로 따른다.

## 경계 마스크

`BOGPLN02` 계획 파일은 기존 바이트 마스크와 함께 드라이버가 사용하는 32개의 32비트 비트마스크를 담는다. 기존 바이트 마스크는 비교 렌더러와 검증용으로 유지한다. 기본 런처의 44개 경계 타일에서는 실행 때 45,056개 셀을 검사하여 비트로 바꾸던 작업을 없애고, 이미 준비된 5,632바이트를 요청에 복사한다.

동적 모양도 모양이 바뀔 때 비트마스크를 함께 만들고 재사용한다. 제작 과정에서 모든 비트가 바이트 마스크와 일치하는지 검사한다. 외부 파일은 CRC·범위·비트 일치성을 검증하며, 형식/버전이 맞지 않으면 기존 계산 경로로 동일한 결과를 만든다. 일반 알파 혼합 하드웨어를 추가한 것은 아니다.

## 세부 계측

첫 프레임 UART의 `BOAYO_FRAME_TRACE`에 다음 항목을 추가했다.

| 항목 | 의미 |
|---|---|
| `output.versions.begin/end` | 완료된 타일 버전 기록과 표시 가능한 타일 확인 |
| `output.publish.begin/end` | 통합 요청 전체 호출 구간 |
| `publish.user_copy_us` | 요청·메타데이터 사용자 메모리 복사 |
| `publish.validate_us` | 메타데이터·소유권·카메라 검사 |
| `publish.upload_us` | CPU 사본 비교, 변경 구간 업로드·DMA 대기, 대상 사본 갱신 |
| `publish.configure_us` | 필요할 때 초기 출력 설정 |
| `publish.select_us` | 전체 로드/주소 전환 선택과 메타데이터 비교 |
| `publish.wait_us` | 요청 레지스터 기록부터 실제 전환 완료 확인까지 |
| `publish.finish_us` | 성공 후 활성 상태와 메타데이터 사본 갱신 |

`publish.*_us`의 **value 필드가 해당 구간의 마이크로초**다. 출력한 로그 시각 자체를 구간 시작으로 해석하지 않는다. `boayo-status`의 `output.publish_profile`에도 최근 값·기록 워드 수·구간 수·전환 명령을 제공한다. QEMU의 BOSIO 모형은 `backend=mock`으로 표시하며 커널/실보드 시간으로 사용하지 않는다. UART 진단 출력 자체의 비용 때문에 바깥 호출 시간은 세부 값 합계보다 클 수 있다.

세부 로그는 다음처럼 정리한다. 실보드 로그에 7개 커널 값이 빠져 있으면 성공으로 처리하지 않는다.

```sh
python3 tests/summarize_publish.py uart.log publish-profile.json hardware
```

## 이미지와 사용

- 이미지: `D:\temp\gen3bosio\boayo-tiles-v4\ORYUKDO-KFB-BoAYO-tiles-v4.img`
- 커널: `6.12.0-oryukdo-tiles4`
- 식별자: `ORYUKDO-BoAYO-tiles-v4`
- 비트스트림: 기존 `ORYUKDO-KFB-SD4-DMA-v1.bit`
- 비트스트림 SHA-256: `fe1ce8e52f30f1b9bd7bad944a0bf6f981eef9bba01438c0d26b40da6126fdf0`

새 앱과 새 드라이버를 함께 사용해야 하므로 새 SD 이미지가 필요하다. 보드 전원을 끈 뒤 SD에 기록·검증한다. `/data`를 포함한 기존 카드 내용은 전체 이미지 기록으로 덮어써지므로 필요한 파일은 먼저 보존한다. 버전 확인 후 `boayo-start`, `boayo-status`로 실행/확인한다. HDMI 첫 영상과 UART 전환 완료 시각은 구분한다.

## 재현과 검증 범위

```sh
bash build.sh /var/tmp/oryukdo-boayo-tiles-v4/release
bash validate.sh /var/tmp/oryukdo-boayo-tiles-v4
python3 package.py /var/tmp/oryukdo-boayo-tiles-v4 /mnt/d/temp/gen3bosio/boayo-tiles-v4
```

기존 42개 장면과 7단계 재사용 비교, 실제 커널 공통 코드의 전체/부분 기록·주소 전환·이전 API 사본 무효화·오류/시간 초과, 잘못된 비트마스크 거부, ASan/UBSan, RV32 QEMU BusyBox/상위 DDR/BoAYO 앱/입력, SD/DMA 및 BS25 RTL 회귀, 최종 커널/앱/initramfs/ext4/소스 일치성을 검사한다. QEMU BOSIO와 RTL DDR은 모형이며 실제 보드 속도·HDMI 검증과 다르다. 정확한 결과와 해시는 `evidence`, `result.json`, `SHA256SUMS.txt`를 따른다.
