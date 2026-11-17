from pathlib import Path
import hashlib,json,shutil,struct,subprocess,sys,tempfile,zlib
source=Path(sys.argv[1]);installer=Path(__file__).resolve().parent.parent/'install_assets.py'
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp);assets=root/'assets';shutil.copytree(source,assets)
 manifest=json.loads((assets/'manifest.json').read_text());entry=manifest['assets'][0]
 path=assets/entry['file'];original=path.read_bytes()
 def check(data,valid,resign=False):
  path.write_bytes(data)
  entry['sha256']=hashlib.sha256(data).hexdigest() if resign else hashlib.sha256(original).hexdigest()
  (assets/'manifest.json').write_text(json.dumps(manifest))
  p=subprocess.run([sys.executable,str(installer),str(assets),str(root/'root')],capture_output=True)
  assert (p.returncode==0)==valid,(p.stdout,p.stderr)
 check(original,True)
 bad=bytearray(original);bad[-1]^=1;check(bad,False);check(bad,False,True)
 bad=bytearray(original);struct.pack_into('<I',bad,8,1)
 k=struct.unpack_from('<I',bad,16)[0];struct.pack_into('<I',bad,28,zlib.crc32(bad[:28]+bad[32:32+k]))
 check(bad,False,True)
 check(original,True)
print('PASS BUILD_ASSET_GATE corruption and signed malformed dimensions rejected')
