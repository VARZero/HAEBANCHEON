from pathlib import Path
import sys,math,struct,json,hashlib
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'software/boayo/native/reference/wm'))
from bosio_geometry_v2 import camera_coefficients
out=Path(sys.argv[1]);count=120;us=50000
body=bytearray()
for i in range(count):
 t=2*math.pi*i/count
 # Quantize pose first: stored angles and camera refer to exactly the same pose.
 angles=[round((8+6*math.sin(t))*1000),round((4+4*math.sin(2*t))*1000),round((8+8*math.sin(t))*1000)]
 cam=camera_coefficients(*(a/1000 for a in angles)).reshape(-1).tolist()
 body.extend(struct.pack('<183i',*angles,*cam))
h=2166136261
for b in body:h=((h^b)*16777619)&0xffffffff
out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(b'BOVIEW01'+struct.pack('<III',count,us,h)+body)
print(json.dumps(dict(frames=count,period_us=us,bytes=out.stat().st_size,sha256=hashlib.sha256(out.read_bytes()).hexdigest())))
