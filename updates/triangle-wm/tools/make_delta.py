"""Binary COPY/LITERAL patch for faster iterative UART deployment."""
import hashlib,json,struct,sys
from pathlib import Path
old=Path(sys.argv[1]).read_bytes();new=Path(sys.argv[2]).read_bytes();size=32
index={}
for i in range(0,len(old)-size+1,4):
 key=old[i:i+size]
 if key not in index:index[key]=i
hash=2166136261
for x in new:hash=((hash^x)*16777619)&0xffffffff
out=bytearray(struct.pack('<4I',0x54525031,len(old),len(new),hash));copied=literal=0;pos=0;start=0;rebuilt=bytearray()
while pos+size<=len(new):
 source=index.get(new[pos:pos+size])
 if source is None:pos+=1;continue
 if start<pos:
  data=new[start:pos];out+=struct.pack('<3I',1,0,len(data))+data;literal+=len(data);rebuilt+=data
 length=size;limit=min(len(new)-pos,len(old)-source)
 while length+1024<=limit and new[pos+length:pos+length+1024]==old[source+length:source+length+1024]:length+=1024
 while length<limit and new[pos+length]==old[source+length]:length+=1
 out+=struct.pack('<3I',0,source,length);copied+=length;rebuilt+=old[source:source+length];pos+=length;start=pos
if start<len(new):
 data=new[start:];out+=struct.pack('<3I',1,0,len(data))+data;literal+=len(data);rebuilt+=data
assert rebuilt==new,'patch reconstruction mismatch'
Path(sys.argv[3]).write_bytes(out);print(json.dumps({'patch_bytes':len(out),'copied':copied,'literal':literal,'result_sha256':hashlib.sha256(new).hexdigest()}))
