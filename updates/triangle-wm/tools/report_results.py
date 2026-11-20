"""Export actual completed board samples and their measured limits to repo docs."""
import hashlib,json,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1];evidence=Path(sys.argv[1]);reports=[];baseline=[]
for n in range(1,7):reports.append(json.loads((evidence/('stage'+str(n)+'.json')).read_text(encoding='utf-8')))
for n in range(1,6):baseline.append(json.loads((evidence/('baseline-stage'+str(n)+'.json')).read_text(encoding='utf-8')))
result=root/'results';result.mkdir(exist_ok=True)
manifest={'base_commit':'5f6ff539fae3e529ef53c1d90952dbe72ba609ea','date':'2026-11-20','timezone':'Asia/Seoul','board':'PZ-A7200T-KFB / RV32IMA 50 MHz / BOSIO BS25 ABI3','final_sha256':hashlib.sha256((evidence/'triangle-sparse').read_bytes()).hexdigest(),'baseline_solid_sha256':hashlib.sha256((evidence/'triangle-stage1').read_bytes()).hexdigest(),'baseline_content_sha256':hashlib.sha256((evidence/'triangle-final').read_bytes()).hexdigest(),'client_sha256':hashlib.sha256((evidence/'board-client').read_bytes()).hexdigest(),'stages':reports,'baseline':baseline,'host_checks':['solid-window regression: PASS','incremental pixels equal full reconstruction: PASS','ASan and UBSan: PASS','1196 camera poses: maximum coefficient error 1 Q24 unit']}
(result/'board-20261120.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# 삼각 타일 WM 실보드 결과 — 2026-11-20','', '6단계를 실제 보드에서 실행하고 USB3.0 Video의 HDMI 캡처로 확인했다. 아래 표는 최종 최적화 실행 파일의 결과다. 이전 중간판과 원시 표본은 `results/board-20261120.json`에 함께 보존한다.','','실시간 모니터: http://127.0.0.1:8766 (테스트 호스트에서 서비스가 실행 중인 동안).','','## 지연시간 (ms)','','| 단계 / 동작 | 표본 수 | 중앙값 | P95 | 최대 |','|---|---:|---:|---:|---:|']
for report in reports:
 for r in report['results']:lines.append(f"| {r['name']} | {r['n']} | {r['median_ms']:.3f} | {r['p95_ms']:.3f} | {r['max_ms']:.3f} |")
lines+=['','측정 범위: C 클라이언트의 요청 송신 시작부터 WM의 출력 완료 응답 수신까지. 소켓 연결 설정, 클라이언트 요청 파일 읽기, UART 출력은 제외한다. WM의 파싱·준비·투영·게시·응답 직렬화는 포함한다. P95는 정렬 표본의 ceil(0.95×N)번째 값이다. 1~3회 측정값은 통계적 꼬리 지연 추정으로 해석하지 않는다.','','출력 시점 행은 미리 준비한 카메라를 적용하는 호출 시간이다. ioctl 완료 및 호출부 처리 비용을 포함하며 사전 좌표 계산과 16.667ms 간격 대기는 제외한다. 실제 광학 지연이 아니다. 모든 단계에서 시점 이동 전후 장면 commit 수가 동일했다. 캡처 카드의 60fps는 WM 갱신율을 의미하지 않는다.','','## 콘텐츠 준비와 부분 갱신','','| 단계 | 시점 61개 좌표 준비 (ms) |','|---|---:|']
for report in reports:
 r=next(r for r in report['results'] if 'preparation_ms' in r);lines.append(f"| {report['stage']} | {r['preparation_ms']:.3f} |")
lines+=['','| 5단계 동작 | 중간판 중앙값 | 최종 중앙값 | 감소 | 재투영 타일 중앙값 | 원본 전송 바이트 중앙값 |','|---|---:|---:|---:|---:|---:|']
for r in reports[4]['results']:
 if 'preparation_ms' in r:continue
 old=next(o for o in baseline[4]['results'] if o['name']==r['name']);samples=r['samples'];median=lambda k:sorted(s['response'][k] for s in samples)[len(samples)//2]
 lines.append(f"| {r['name']} | {old['median_ms']:.3f} | {r['median_ms']:.3f} | {(1-r['median_ms']/old['median_ms'])*100:.1f}% | {median('projected_tiles')} | {median('texture_upload_bytes')} |")
lines+=['','최초 콘텐츠 구성은 원본 생성·좌표 디스크립터 계산·원본 전체 업로드·투영을 포함한다. 부분 갱신은 디스크립터와 원본을 재사용한다. 다른 버퍼에 아직 없는 변경되지 않은 타일은 DMA로 복사하므로 초기 동기화 표본이 최댓값에 포함될 수 있다. 원본 업로드는 변경 영역을 포함하는 전체 행 범위이며 최소 픽셀 바이트 수와 같지 않다.','','## 화면 검증과 범위','','- 단색 창 외곽은 삼각 타일의 합집합이며 내부·외부 장식 테두리가 없다.','- 창 이동, 중첩 순서 변경, 닫기 뒤 아래 창 복원을 실제 캡처로 확인했다.','- 글자·막대 배치, 글자/막대 갱신, 겹친 컴포넌트 삭제 뒤 복원을 확인했다.','- 호스트 참조 검사에서는 부분 갱신의 모든 타일 픽셀이 독립적인 전체 재생성과 일치했다. 하드웨어 화면 전체의 픽셀 단위 참조 대조와 광학 지연 측정은 수행하지 않았다.','- 화면의 글자 가장자리 품질은 기존 삼각 셀 해상도와 RGB332 양자화의 영향을 받는다.','- 최종 코드는 로컬 `/tmp/triangle-wm-v3`에서 실행한다. 기존 SD 부팅 WM은 교체하지 않았다. 기존 BoAYO 앱 프로토콜·입력 라우팅·세션 수명주기 호환은 별도 통합 작업이다.','- 현재 메타데이터 게시 ABI의 전체 검증/전송 비용과 콘텐츠 위치 변경 시 디스크립터 계산 비용은 남아 있다.','',f"최종 RV32 SHA256: `{manifest['final_sha256']}`",'', '재현: `README.md`의 빌드·검증·단계 실행 절차를 따른다. 원시 UART 로그, AVI, 개별 캡처는 로컬 `evidence/`에 있다.']
(root/'RESULTS.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Exported six measured stages and baseline samples')
