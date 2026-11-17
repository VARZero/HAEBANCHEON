from pathlib import Path
import hashlib,json,shutil,struct,subprocess,sys,tempfile,zlib
plans=Path(sys.argv[1]);here=Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as temp:
 root=Path(temp);source=root/'plans';shutil.copytree(plans,source)
 manifest=json.loads((source/'manifest.json').read_text());name=manifest['bootstrap']
 original=(source/name).read_bytes()
 def attempt(data,expected):
  (source/name).write_bytes(data)
  manifest['plans'][0]['sha256']=hashlib.sha256(data).hexdigest()
  (source/'manifest.json').write_text(json.dumps(manifest))
  p=subprocess.run([sys.executable,str(here/'install_plans.py'),str(source),str(root/'image')],capture_output=True)
  assert (p.returncode==0)==expected,(p.stdout,p.stderr)
 def signed(data):
  # Even a matching SHA and CRC cannot authorize malformed descriptors/masks.
  struct.pack_into('<I',data,20,zlib.crc32(data[4096:]))
  k=struct.unpack_from('<I',data,16)[0]
  struct.pack_into('<I',data,24,zlib.crc32(data[:24]+data[28:28+k]))
  return data
 attempt(original,True)
 data=bytearray(original);data[-1]^=0xff;attempt(data,False)
 data=bytearray(original);struct.pack_into('<I',data,4096+12,1);attempt(signed(data),False)
 n=struct.unpack_from('<I',original,8)[0]
 for i in range(n):
  tile,kind,offset=struct.unpack_from('<III',original,4096+i*84)
  if kind==2:break
 else:raise AssertionError('missing boundary')
 data=bytearray(original);data[4096+offset]=2;attempt(signed(data),False)
 data=bytearray(original);data[4096+offset:4096+offset+1024]=bytes(1024);attempt(signed(data),False)
 data=bytearray(original);data[4096+offset+1024]^=1;attempt(signed(data),False)
 attempt(original,True)
 ready=source/manifest['plans'][0]['ready_file'];bad=bytearray(ready.read_bytes());bad[-1]^=1;ready.write_bytes(bad)
 manifest['plans'][0]['ready_sha256']=hashlib.sha256(bad).hexdigest()
 attempt(original,False)
print('PASS BUILD_PLAN_GATE valid input, CRC corruption, signed invalid descriptor/mask/classification rejected')
