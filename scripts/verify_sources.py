"""실보드에 사용한 RTL 및 패키지의 변경 여부를 확인한다."""
from pathlib import Path
import hashlib, json
from publication_hashes import check_published
root=Path(__file__).resolve().parents[1]
def check(rel, expected):
    return check_published(root,rel,expected)
sources=json.loads((root/'provenance/board_sources.json').read_text())['files']
source_checks=[check(name, sha) for name, sha in sources.items()]
for line in (root/'soc/bosio_sources.f').read_text().splitlines():
    if line.strip() and not (root/line.strip()).is_file(): raise SystemExit('Missing '+line)
for entry in json.loads((root/'software/linux/source_manifest.json').read_text())['changes']:
    check('software/linux/modified/'+entry['path'],entry['sha256'])
for entry in json.loads((root/'provenance/trace_v2_image.json').read_text())['segments']:
    check('software/firmware/'+entry['file'],entry['sha256'])
excluded=source_checks.count('external_unused_example')
print(f'PASS 보드 입력 {len(sources)-excluded}개, Linux 수정 파일, 부팅 페이로드 일치; 미사용 외부 예제 제외 {excluded}개')
print('게시용 도구 변경은 HDMI_EXTERNALIZATION_HASHES.json으로 별도 확인. 미사용 예제 HDMI는 제외하며 공식 IP는 외부 준비 후 검사.')
