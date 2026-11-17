"""Package offline-validated v2. Never access the board, SD device or GitHub."""
from pathlib import Path
import hashlib, json, shutil, sys, zipfile
here=Path(__file__).resolve().parent
out,dest=map(Path,sys.argv[1:])
assert not dest.exists(), 'Choose a new release directory'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
audit=json.loads((out/'reports/release_audit.json').read_text())
assert audit['result']=='PASS'
checks={}
for mode in ('host','sanitize'):
    result=json.loads((out/'reports'/mode/'suite.json').read_text())
    assert result['result']=='PASS' and result['binary_sha256']==sha(out/('boayo-native-'+mode))
    manifest=json.loads((out/('boayo-native-'+mode+'.build.json')).read_text())
    assert manifest['source_sha256']==audit['source_manifest']['source_sha256'], mode
    assert manifest['uapi_sha256']==audit['source_manifest']['uapi_sha256'], mode
    checks[mode]=result['result']
for compression in ('gzip','raw'):
    result=json.loads((out/'reports'/f'qemu-{compression}-final/result.json').read_text())
    assert result['result']=='PASS_QEMU_ONLY'
    assert result['image_sha256']==sha(out/'images'/f'ORYUKDO-KFB-BoAYO-native-v2-{compression}.img')
    checks['qemu-'+compression]=result['result']
for name,digest in audit['source_manifest']['source_sha256'].items():
    assert sha(here/name)==digest,name
for platform in ('host','rv32'):
    assert json.loads((out/'reports/geometry'/f'{platform}.json').read_text())['result']=='PASS'
bench=json.loads((out/'reports/benchmark/comparison.json').read_text())
assert bench['result']=='PASS'
dest.mkdir(parents=True)
shutil.copytree(out/'images',dest/'images')
shutil.copytree(out/'reports',dest/'reports')
for compression in ('gzip','raw'):
    shutil.copytree(out/compression,dest/'build'/compression)
    for kind in ('kernel','kernel_config','sd','verify'):
        shutil.copy2(out/f'{kind}_{compression}.log',dest/'reports'/f'{kind}_{compression}.log')
for name in ('root_audit.json','boayo-native-host.build.json','boayo-native-rv32.build.json','boayo-native-sanitize.build.json'):
    shutil.copy2(out/name,dest/'reports'/name)
shutil.copy2(out/'root/usr/bin/boayo-native',dest/'boayo-native-rv32')
for name in ('README.md','V2_OPTIMIZATION.md','NEXT_SESSION.md'):
    shutil.copy2(here/name,dest/name)
with zipfile.ZipFile(dest/'boayo-native-v2-source.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for p in sorted(here.rglob('*')):
        if p.is_file() and '__pycache__' not in p.parts:
            z.write(p,'software/boayo/native/'+str(p.relative_to(here)))
    z.write(here.parents[1]/'bosio/include/bosio_uapi.h','software/bosio/include/bosio_uapi.h')
old={'raw':10238976,'gzip':6945792}
rows='\n'.join(f"| {i['file']} | {i['bytes']:,} | {100*(1-i['bytes']/old['raw' if '-raw' in i['file'] else 'gzip']):.1f}% |" for i in audit['images'])
root=audit['root'];a=bench['baseline'];b=bench['optimized']
responsive=json.loads((out/'reports/host/test_responsive.py.log').read_text())
text=f'''# BoAYO C++ v2 오프라인 검증 결과

2026-11-09. 고정 기하 사전 계산, 복사 감소, 화면 계산 중 상태 조회를 적용한 새 SD 이미지다. 보드에는 접속하거나 기록하지 않았다. 실제 HDMI·WASD 체감·보드 시작 시간은 다음 실보드 확인 사항이다. 기존 RTL·MIG·HDMI IP·드라이버 ABI·비트스트림은 유지한다.

## 이미지와 데이터량

| 이미지 | 바이트 | C++ v1 동일 압축 방식 대비 감소 |
|---|---:|---:|
{rows}

루트 일반 파일 합계: v1 6,127,286 → v2 {root['root_regular_bytes']:,}바이트. 새 정적 실행 파일은 {root['native_binary_bytes']:,}바이트다. 실행 파일 자체는 커졌지만 미사용 공유 라이브러리를 제외해 전체 루트는 작아졌다. BusyBox·init·ddrcheck·bosioctl은 v1과 바이트 단위로 같다. Python·Bash·Vim·SD 영구 파일시스템은 추가하지 않았다.

초기 빈 장면의 양쪽 뱅크 전송량은 {a['initial_upload_bytes']:,} → {b['initial_upload_bytes']:,}바이트로 {bench['reduction_percent']['initial_upload_bytes']:.2f}% 감소했다. 이것은 드라이버에 요청한 데이터량이며 실제 DDR 버스 측정은 아니다. 한 타일 부분 갱신의 shadow 복사는 1,028바이트다.

## 기능 및 안전성 검사

| 검사 | 결과 |
|---|---|
| 고정 표 | PC·RV32에서 v1 기하 double 비트 일치, 카메라·33,760개 기술자 일치 |
| Python 기준 상태·기하 | PASS, 기존 1,595 요청 시험 |
| UI 픽셀·앱 | PASS, 59개 이미지·44개 앱 상태 요청 |
| 투영코어 2개·출력 모델 | PASS, 10개 장면 전체 결과와 BPT2 패치 일치 |
| 원본 CPU 대체 합성 | PASS, 3개 장면 |
| IPC·WASD | PASS, Python 클라이언트·세 앱·최소화/복원/닫기·CRLF 한 번 클릭·TTY 복원 |
| 출력 실패·오래된 RAM | PASS, 전환 전/중/후 실패, 무효 디렉터리, 활성화 타일 전체 덮어쓰기 |
| 상태 응답 | PASS, Mock 합성을 3초 지연한 동안 {responsive['samples']}회 응답; PC 최장 {responsive['maximum_status_ms']:.3f}ms |
| ASan/UBSan | 동일 시험 전부 PASS |
| SD/소스 감사 | PASS, 원본 Python {audit['reference_files']}개 파일 유지, 세 빌드 동일 소스, 실제 내장 파일·링크·모드·CRC·SHA256 일치 |
| RV32 의존성 | {len(audit['elf'])}개 ELF, RV32IMA/ILP32·정적 연결, C/F/D/V 요구 없음 |
| 무압축 SD QEMU | PASS, Linux 쉘·상위 RAM 16페이지·자체 검사·BoAYO Mock 런처/세 앱·WASD·중앙 클릭·종료 |
| 압축 SD QEMU | PASS, Linux 쉘·상위 RAM 16페이지·자체 검사 |
| 실보드 / 전체 CPU RTL | 이번에는 실행하지 않음 |

RV32 사용자 에뮬레이터의 별도 비교에서 기하 생성 {a['geometry_ms']:.4f} → {b['geometry_ms']:.4f}ms, 4,220개 기술자 묶음 {a['descriptor_batch_ms']:.3f} → {b['descriptor_batch_ms']:.3f}ms, 한 타일 갱신 {a['one_tile_patch_ms']:.3f} → {b['one_tile_patch_ms']:.3f}ms였다. 기술자 체크섬과 최종 shadow 해시는 같다. 이 숫자는 실제 50MHz FPGA 성능 예측값이 아니다.

## 보드를 나중에 켤 때

`images/ORYUKDO-KFB-BoAYO-native-v2-raw.img` 전체를 Etcher로 SD에 기록·검증한다. 기존 비트스트림 SHA256은 `9e762305efd3c376cce979bb6857f428e02ccfe275388b85d412e39d44f9296b`다.

```sh
cat /etc/oryukdo-image
boayo-start
boayo-status
boayo-keys
```

시작 시 런처가 자동으로 열리며 다시 열려면 쉘에서 `boayo-launcher`를 입력한다. WASD/방향키는 시야, Space/Enter는 중앙 클릭, R은 초기화, Q는 쉘 복귀다. 상태에는 현재 처리 단계와 마지막 완료 스냅샷의 나이가 표시된다. 서비스 시작/초기 장면 완료 문구는 실제 HDMI 런처 표시 완료를 뜻하지 않는다. 실제 입력 명령은 진행 중인 한 프레임이 끝날 때까지 대기할 수 있다.

## 남은 확인과 한계

- 실제 보드의 SD 부팅, 초기화 시간, HDMI 화면, 입력 응답 및 장시간 구동 확인이 남아 있다.
- QEMU는 정확한 SD payload를 메모리에 적재하지만 SD SPI·ORYUKDO CPU RTL·MIG·실제 BOSIO·HDMI를 실행하지 않는다. 상위 RAM 검사도 QEMU 가상 RAM 검사다.
- 시험한 기능의 동등성 근거이며 모든 입력에 대한 형식적 증명은 아니다. CPU 대체 합성 경로의 큰 광선 배열 계산과 일부 복사는 남아 있다.
- v1 원본·이미지·Python 기준과 기존 실보드 미완료 기록을 보존했다. 원격 저장소 변경은 하지 않았다.

구조는 `V2_OPTIMIZATION.md`, 소스는 `boayo-native-v2-source.zip`, 실행 로그는 `reports/`, 커널 설정과 manifest는 `build/`, 파일별 해시는 `release.json`에 있다. Linux 전체 소스·도구 체인·기존 루트는 README의 로컬 경로를 사용하며 이 ZIP에 모두 포함하지 않는다.
'''
(dest/'RESULTS.md').write_text(text)
files={str(p.relative_to(dest)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(dest.rglob('*')) if p.is_file()}
release=dict(image_id='ORYUKDO-KFB-BoAYO-native-v2',status='OFFLINE_VALIDATED_BOARD_PENDING',files=files,
 tests=checks,source_binary_manifest=audit['source_manifest'],hardware_tested=False,rtl_tested=False,
 preferred_image='images/ORYUKDO-KFB-BoAYO-native-v2-raw.img')
(dest/'release.json').write_text(json.dumps(release,indent=2)+'\n')
print(json.dumps(dict(result=release['status'],directory=str(dest),files=len(files),images=audit['images'])))
