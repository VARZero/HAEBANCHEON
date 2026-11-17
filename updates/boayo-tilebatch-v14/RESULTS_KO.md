# v14 로컬 검증 결과

> 2026-11-16 후속: [실보드 비교](BOARD_RESULT_KO.md)에서 Linux·HDMI 출력은 확인했으나 Telemetry 31.6%, SDK Pulse 26.7% 지연 증가가 확인됐다. 아래는 11월 15일의 로컬 검증 기록이며 성능 향상 증거가 아니다.

2026-11-15. 최종 이미지 해시는 `cccee8bb4704fa2ec14a4b1f71d882c4d3a541809c0b91c2506467b3850cff30`이다. **실보드 미실행, 실제 처리 시간 미측정**이다.

## 기능·오류 처리

| 검증 | 결과 | 증거 |
|---|---|---|
| host/RV32/sanitizer 소스 47개 일치 | PASS | `evidence/source-consistency.json`, `*-build.json` |
| 상주 타일·창 장면 42개 | PASS | `evidence/reports-final/resident/result.json` |
| 공통 버튼 수명·동작 13개 | PASS | `evidence/reports-final/captions/result.json` |
| 동작 장면 26개 및 에셋 없는 동일 26개 | PASS | `evidence/reports-final/motion/result.json`, `no-assets/result.json` |
| 앱 초기 상태·12회 갱신, 총 13개 | PASS | `evidence/reports-final/app-updates/result.json` |
| 상태 캐시 250개 스냅샷·입력·기하 무효화 | PASS | `evidence/reports-final/state_cache.log` |
| 주기 제한·시점 재생 서비스 | PASS | `evidence/reports-final/frame_pacer.log`, `pacing-service.log`, `view-service.log` |
| 100개 무작위 이미지 레시피·60개 CPU 편집·프레임 생략·다중 창·퇴출·오류 | PASS | `evidence/reports-final/texture_pool.log` |
| 3,000개 변경 영역, 51,063,722픽셀 검사 | PASS | `evidence/extra/host-test_damage.log` |
| 외부 fill/update_surface의 대각선 변경 영역 보존 | PASS | 같은 damage 시험 |
| 캐시 퇴출 중 순서·겹치는 복사·12개 실패 위치 | PASS | `evidence/sanitizer-reports/test_asset_lifetime.log` |
| ASan/UBSan 변경 영역 검사 | PASS | `evidence/sanitizer-reports/test_damage.log` |
| 사각형 본문 에셋 689개·공통 버튼 18개 동일성 | PASS | `evidence/rect-assets.json` |

장면 비교 총 120개는 의도한 사각형 UI를 양쪽에 동일하게 적용한 기준 구현과 최적화 구현을 비교한다. 이전 둥근 본문과 픽셀이 같다고 주장하지 않는다. 기준 구현 차이는 `tests/reference-ui.patch`에 보존했다.

최초 구현에서 같은 32×32 구획 안의 여러 변경을 경계 상자로 합쳐 배경까지 다시 업로드하는 회귀가 발견됐다. 구획 내부의 분리된 사각형을 보존하도록 수정한 뒤 최종 시험을 통과했다. 실패 기록은 `evidence/resolved-attempt1/`이며 최종 결과와 구분한다.

ASan/UBSan은 수정된 전체 C++ 실행 파일과 테스트에 적용했다. 런타임 오류는 검출되지 않았다. 계측 빌드에서는 GCC의 벡터 initializer-list 관련 `-Wstringop-overread` 경고와 resident/backend의 32KiB 초과 스택 프레임 경고가 나왔다. 일반 host/RV32 빌드 출력에는 해당 경고가 없었다. 런타임 검사 통과만으로 컴파일러 경고가 오탐이라고 단정하지 않는다.

## 전송·요청량

`evidence/extra/comparison.json`은 같은 픽셀 결과를 확인한 호스트 모델 비교다. 10회 준비 후 40회 반복 글자 갱신에서 업로드 491,520→0바이트, 업로드 호출 320→0회, 복사 묶음 호출 320→80회다. DDR 내부에서 실제 픽셀을 복사하는 작업은 남는다.

대각선으로 맞닿은 32개 요소의 업로드는 1,081,344→1,024바이트다. 이는 과도한 배경 포함 제거와 같은 에셋 재사용이 함께 적용된 결과다. 반면 일반 앱 12회 갱신 시험은 양쪽 모두 460,800바이트이며 이 시험에서 전송량 개선은 0%다. 새로운 에셋 위주의 작업에는 캐시 적재·복사 오버헤드가 추가될 수 있으므로 모든 앱의 속도 향상을 확정하지 않는다.

`evidence/extra/host-corner_work.log`의 둥근/사각 본문 비교:

| 항목 | 반경 14 | 반경 0 |
|---|---:|---:|
| 완전 포함 타일 | 65 | 65 |
| 부분 타일 | 51 | 52 |
| 제외 타일 | 25 | 24 |
| 샘플 계산 타일 | 82 | 82 |
| 정수 판정 | 45,285 | 45,285 |
| 정밀 곡선 판정 | 284 | 0 |

사각형으로 바뀌어 모서리 면적이 추가되므로 한 타일이 제외에서 부분으로 바뀐다. 위 수치로 창 이동 시간이 몇 초 줄어드는지 환산할 수 없다. 이전 v13의 실보드 시간은 v14 측정값이 아니다.

## SD·QEMU

`evidence/audit.json`은 최종 RV32 빌드/설치 파일, 커널 내 initramfs, BusyBox/OpenSBI 보존, 변경 없는 BOSIO 드라이버·UAPI·DT, 689개 에셋과 37개 계획, SD CRC·파티션·payload 범위·ext4 fsck를 확인한다. SD는 Windows 전달 경로로 복사한 뒤 SHA256을 다시 비교했다.

`evidence/qemu/result.json`과 `uart.log`는 이 SD에서 추출한 펌웨어·커널·DT를 QEMU RV32IMA에 직접 적재해 수행한 결과다.

- BusyBox 쉘, 이미지 식별, 실행 파일·기본 에셋 존재, Python 없는 구성, 쉘 연산: PASS.
- `ddrcheck`: 상위 주소 16개 페이지, `0xbee00000`부터 `0xbef0bffc`까지 검사 PASS. QEMU 메모리 검사이며 MIG 실물 검사 아님.
- BoAYO 자체 시험과 두 코어 모델 시험: PASS.
- RV32에서 실제 계산한 첫 런처의 투영계수·마스크를 PC 사전 계산 파일과 바이트 단위 비교: PASS.
- BoAYO 서비스 Mock Backend, 첫 런처·Dashboard·Telemetry·SDK Pulse, WASD·클릭·키 입력 종료: PASS.
- 첫 런처는 사전 계산 자료 1개를 읽고 실행 중 기하/마스크 계산 0회, 사용자 장면 전체 버퍼 0워드, 투영 결과 CPU 회수 0워드. 단, 출력 메타데이터 사본 4,480워드는 기존대로 유지한다.
- 시험 종료 시 서비스 정상 종료: PASS.

QEMU는 실제 SD 컨트롤러와 ext4 마운트, FPGA CPU/캐시/DMA/투영코어/HDMI를 검증하지 않는다. ext4 내용·무결성은 정적 이미지 검사로 확인했으며 실제 드라이버 실행·출력 영상·속도는 새 SD의 실보드 시험으로 남겨 둔다.
