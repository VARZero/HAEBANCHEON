"""Independent minimal Python implementation of BWM1/CBOR integer messages."""
import struct,os,json
MAGIC=0x314d5742
KEY='\0q24'
ANGLE='\0a2'
ANGLES={'yaw','pitch','roll','azimuth','elevation','width_deg','height_deg','padding_deg'}
def head(major,n):
    if n<24:return bytes([major*32+n])
    for info,size in [(24,1),(25,2),(26,4),(27,8)]:
        if n<1<<(size*8):return bytes([major*32+info])+n.to_bytes(size,'big')
    raise ValueError('CBOR overflow')
def cbor(v):
    if v is None:return b'\xf6'
    if isinstance(v,bool):return b'\xf5' if v else b'\xf4'
    if isinstance(v,int):return head(0,v) if v>=0 else head(1,-1-v)
    if isinstance(v,float):return cbor({KEY:int(v*16777216)})
    if isinstance(v,str):
        s=v.encode();return head(3,len(s))+s
    if isinstance(v,list):return head(4,len(v))+b''.join(map(cbor,v))
    if isinstance(v,dict):
        pairs=[]
        for k,x in v.items():
            if k in ANGLES and isinstance(x,(int,float)):
                raw=int(x*16777216);q2=(abs(raw)+(1<<21))>>22
                x={ANGLE:-q2 if raw<0 else q2}
            pairs.append(cbor(k)+cbor(x))
        return head(5,len(v))+b''.join(pairs)
    raise TypeError(type(v))
def encode(v):
    if os.environ.get('TRIANGLE_WIRE')=='json':return (json.dumps(v)+'\n').encode()
    p=cbor(v);assert 0<len(p)<=65536
    return struct.pack('<II',MAGIC,len(p))+p
def decode(p):
    offset=0
    def read(depth=0):
        nonlocal offset
        assert depth<=32
        initial=p[offset];offset+=1;major=initial>>5;info=initial&31
        if major==7:
            assert info in [20,21,22];return {20:False,21:True,22:None}[info]
        if info<24:n=info
        else:
            count={24:1,25:2,26:4,27:8}[info];n=int.from_bytes(p[offset:offset+count],'big');offset+=count
        if major==0:return n
        if major==1:return -1-n
        if major==3:s=p[offset:offset+n].decode();offset+=n;return s
        if major==4:return [read(depth+1) for _ in range(n)]
        if major==5:
            result={}
            for _ in range(n):k=read(depth+1);result[k]=read(depth+1)
            if len(result)==1 and KEY in result:return result[KEY]/16777216
            if len(result)==1 and ANGLE in result:return result[ANGLE]/4
            return result
        raise ValueError('unsupported CBOR')
    result=read();assert offset==len(p);return result
def read(stream):
    if os.environ.get('TRIANGLE_WIRE')=='json':return json.loads(stream.readline())
    def exact(n):
        chunks=[]
        while n:
            b=stream.read(n)
            if not b:raise EOFError('binary frame')
            chunks.append(b);n-=len(b)
        return b''.join(chunks)
    magic,n=struct.unpack('<II',exact(8));assert magic==MAGIC and 0<n<=65536
    return decode(exact(n))
