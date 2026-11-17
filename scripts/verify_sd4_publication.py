"""SD4 공개 소스·증거의 무결성을 확인한다. 보드 동작 검증은 수행하지 않는다."""
from pathlib import Path
import hashlib,json
from publication_hashes import check_published
root=Path(__file__).resolve().parents[1]/'variants/sd4-dma'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
sources=json.loads((root/'provenance/source_sha256.json').read_text())
source_checks=[check_published(root,name,digest) for name,digest in sources.items()]
for line in (root/'soc/bosio_sources.f').read_text().splitlines():
 if line.strip():assert (root/line.strip()).is_file(),'없는 RTL 입력: '+line
files=json.loads((root/'provenance/publication_files.json').read_text())
file_checks=[check_published(root,name,digest) for name,digest in files.items()]
status=json.loads((root/'validation/status.json').read_text())
assert all(check['pass_marker'] for check in status['checks'].values())
assert status['actual_image_loader']['current_rtl_matches']
assert status['full_board']['source_match'] and status['full_board']['route_pass_marker']
board=json.loads((root/'provenance/board_manifest.json').read_text())
assert board['hardware_tested'] is False
assert board['timing_ns']['wns']>=0 and board['timing_ns']['whs']>=0
assert board['timing_ns']['tns']==0 and board['timing_ns']['ths']==0
assets=json.loads((root/'provenance/release_assets.json').read_text())['assets']
for key in ('image','bitstream'):
 asset=next(a for a in assets if a['name']==board[key])
 assert asset['sha256']==board[key+'_sha256']
assert json.loads((root/'validation/actual_image_loader.json').read_text())['image_sha256']==board['image_sha256']
assert json.loads((root/'validation/qemu_shell/result.json').read_text())['image_sha256']==board['image_sha256']
print(f"PASS SD4 검증 소스 {len(sources)-source_checks.count('external_unused_example')}개, 게시 파일 {len(files)-file_checks.count('external_unused_example')}개, 기능시험 기록, 배선 결과 및 이미지 대응")
print('위 PASS는 v1 제작 기록의 보존 검사입니다. 후속 실보드 결과와 수정은 updates/sd4-linux-v2/README_KO.md를 확인하세요.')
print('외부 HDMI 분리와 게시용 도구 변경은 별도 해시 기록을 적용하며, 미사용 보드 예제 HDL은 검사 대상에서 제외합니다.')
