"""Create COPY/literal patches for apply_delta.c, checking exact reconstruction."""
import argparse,gzip,hashlib,json,struct
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('old',type=Path);ap.add_argument('new',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
old=a.old.read_bytes();new=a.new.read_bytes();index={};block=16
for i in range(0,len(old)-block+1,block):
 key=old[i:i+block];values=index.setdefault(key,[])
 if len(values)<4:values.append(i)
ops=[];literal=bytearray();p=0;delta=0
def flush():
 if literal:ops.append((1,0,bytes(literal)));literal.clear()
while p<len(new):
 key=new[p:p+block];candidates=index.get(key,[]);expected=p+delta
 if expected>=0 and old[expected:expected+block]==key:candidates=[expected]+candidates
 best=0;offset=0
 for i in candidates:
  length=block
  for step in [4096,256,16,1]:
   while p+length+step<=len(new) and i+length+step<=len(old) and new[p+length:p+length+step]==old[i+length:i+length+step]:length+=step
  if length>best:best,offset=length,i
 if best>=32:
  flush()
  if ops and ops[-1][0]==0 and ops[-1][1]+ops[-1][2]==offset:ops[-1]=(0,ops[-1][1],ops[-1][2]+best)
  else:ops.append((0,offset,best))
  delta=offset-p;p+=best
 else:literal.append(new[p]);p+=1
flush();checksum=2166136261
for byte in new:checksum=((checksum^byte)*16777619)&0xffffffff
patch=bytearray(struct.pack('<4I',0x54525031,len(old),len(new),checksum));rebuilt=bytearray()
for op,offset,value in ops:
 size=value if op==0 else len(value);patch.extend(struct.pack('<3I',op,offset,size))
 if op:patch.extend(value);rebuilt.extend(value)
 else:rebuilt.extend(old[offset:offset+size])
assert rebuilt==new
a.output.write_bytes(patch)
print(json.dumps({'old_sha256':hashlib.sha256(old).hexdigest(),'new_sha256':hashlib.sha256(new).hexdigest(),'patch_bytes':len(patch),'patch_gzip_bytes':len(gzip.compress(patch,mtime=0)),'operations':len(ops)},indent=2))
