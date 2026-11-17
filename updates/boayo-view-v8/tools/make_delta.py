from pathlib import Path
import sys,struct,bz2,ctypes,hashlib,json
sys.path.insert(0,'/var/tmp/haebancheon-view-v8/host-tools');import bsdiff4
r=Path('/var/tmp/haebancheon-view-v8');old=Path(sys.argv[1] if len(sys.argv)>1 else '/var/tmp/haebancheon-motion-v7/release/root/usr/bin/boayo-native').read_bytes();new=(r/'boayo-v8').read_bytes()
p=bsdiff4.diff(old,new)
def num(b):
 v=int.from_bytes(b,'little');return -(v&((1<<63)-1)) if v>>63 else v
cs,ds,ns=[num(p[i:i+8]) for i in (8,16,24)]
c=bz2.decompress(p[32:32+cs]);diff=bz2.decompress(p[32+cs:32+cs+ds]);extra=bz2.decompress(p[32+cs+ds:]);ctrl=[num(c[i:i+8]) for i in range(0,len(c),8)]
raw=struct.pack('<'+'i'*len(ctrl),*ctrl)+diff+extra
lib=ctypes.CDLL('liblz4.so.1');out=ctypes.create_string_buffer(lib.LZ4_compressBound(len(raw)));n=lib.LZ4_compress_default(raw,out,len(raw),len(out));assert n>0
result=bytearray();oi=di=ei=0
for a,b,seek in zip(ctrl[::3],ctrl[1::3],ctrl[2::3]):
 for j in range(a):result.append((diff[di+j]+(old[oi+j] if 0<=oi+j<len(old) else 0))&255)
 di+=a;oi+=a;result+=extra[ei:ei+b];ei+=b;oi+=seek
assert result==new
def fnv(b):
 h=2166136261
 for v in b:h=((h^v)*16777619)&0xffffffff
 return h
h=struct.pack('<10I',0x32504442,len(old),fnv(old),len(new),fnv(new),len(raw),n,len(ctrl)//3,len(diff),len(extra))
(r/(sys.argv[2] if len(sys.argv)>2 else 'view.delta')).write_bytes(h+out.raw[:n]);print(json.dumps(dict(patch_bytes=len(h)+n,target_bytes=len(new),sha256=hashlib.sha256(new).hexdigest(),exact_reconstruction=True)))
