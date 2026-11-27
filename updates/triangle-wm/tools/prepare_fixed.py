"""Generate an isolated fixed-math service from the maintained WM state machine.

Only geometry, numeric types and math calls are replaced. Legacy/reference sources
remain available for independent validation. Output is build material, not edited.
"""
import argparse,re,shutil
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);a=ap.parse_args()
src=Path(__file__).resolve().parents[1]/'src';a.output.mkdir(parents=True,exist_ok=True)
files=['triangle_wm.cpp','boayo_service.cpp','app_input.inc','ui_input.inc','ui_render.inc','content_render.inc','move_marker.inc','move_commit.inc','ui_types.hpp','window_transform.hpp','boayo_app.cpp','input_client.cpp']
for p in src.iterdir():
 if p.is_file():shutil.copyfile(p,a.output/p.name)
for name in files:
 s=(src/name).read_text()
 s=re.sub(r'\bdouble\b','Fx',s);s=re.sub(r'\bGeometry\b','FxGeometry',s);s=re.sub(r'\bFastProjection\b','FxProjection',s);s=re.sub(r'\bV\b','FxV',s)
 for fun in ['abs','isfinite','lround','llround','tan','sin','cos','atan','atan2','asin','hypot','sqrt']:
  s=s.replace('std::'+fun,'fixed::'+fun)
 s=re.sub(r'(?<![\w.])(?:\d+\.\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)',lambda m:'Fx::ratio('+str(int(__import__('decimal').Decimal(m[0])*10**8))+',100000000)',s)
 s=s.replace('boayo::FxV','FxV').replace('boayo::FxGeometry','FxGeometry')
 s=s.replace('0x1p30','Fx(1073741824)')
 for fun in ['rad','axes','dot','cross','unit','azimuth']:
  s=re.sub(r'(?<![:\w])'+fun+r'\(', 'fixed::'+fun+'(',s)
 s=s.replace('/pi','/Fx::bits(52707179)')
 s=s.replace('2*pi*','2*Fx::bits(52707179)*')
 s=s.replace('fast_camera(', 'fx_camera(')
 if name=='triangle_wm.cpp':
  for plane in ['normal','content_normal']:
   old='int32_t(fixed::lround('+plane+'[p][k]/largest*8192))'
   assert s.count(old)==1,old
   s=s.replace(old,'fixed::plane_q13('+plane+'[p][k],largest)')
  s=s.replace('#include "fast_camera.hpp"','#include "fixed_geometry.hpp"').replace('#include "fast_projection.hpp"','')
  s=s.replace('std::chrono::duration<Fx>(std::chrono::steady_clock::now().time_since_epoch()).count()', 'Fx::ratio(microseconds_now(),1000000)')
  # Standalone legacy command runner is a reference tool, not this service.
  s=s[:s.index('#ifndef TRIANGLE_ENGINE_ONLY')]+ '\n'
  s=s.replace(' static void validate(const Window&w)', ' static void quantize(Window&w){w.yaw=fixed::quarter(w.yaw);w.pitch=fixed::quarter(w.pitch);w.width=fixed::quarter(w.width);w.height=fixed::quarter(w.height);w.padding=fixed::quarter(w.padding);}\n static void validate(const Window&w)')
  s=s.replace('call(BO_PUBLISH,&p);', 'call(publish_copy?BO_PUBLISH_COPY:BO_PUBLISH,&p);')
  s=s.replace(' bool mock;int fd=', ' bool publish_copy=false;bool mock;int fd=')
  s=s.replace('  if(const char *v=getenv("TRIANGLE_UI_DELTA"))', '  publish_copy=!(getenv("TRIANGLE_PUBLISH_COPY")&&std::string(getenv("TRIANGLE_PUBLISH_COPY"))=="0");\n  if(const char *v=getenv("TRIANGLE_UI_DELTA"))')
  s=s.replace('else if(op==BO_PUBLISH)', 'else if(op==BO_PUBLISH||op==BO_PUBLISH_COPY)')
 if name in ['content_render.inc','move_marker.inc']:s=s.replace('call(BO_PUBLISH,&p);','call(publish_copy?BO_PUBLISH_COPY:BO_PUBLISH,&p);')
 if name=='boayo_service.cpp':
  s=s.replace('{"renderer","triangle"}', '{"renderer","triangle"},{"numeric_format","fixed-q24"},{"angle_step_q2",1},{"wire_version","BWM1"}')
  s=s.replace('Engine::validate(w);','Engine::validate(w);Engine::quantize(w);').replace('Engine::validate(bounds);','Engine::validate(bounds);Engine::quantize(bounds);')
  s=s.replace('auto c=fx_camera(y,pi);','y=fixed::quarter(y);pi=fixed::quarter(pi);auto c=fx_camera(y,pi);')
 if name=='ui_input.inc':s=s.replace('  return proposed;', '  Engine::quantize(proposed);return proposed;')
 if name in ['boayo_app.cpp','input_client.cpp']:
  s=s.replace('using namespace boayo;', 'using namespace boayo;\n#include "fixed_math.hpp"\nusing Fx=fixed::Number;')
  s=s.replace('std::chrono::duration<Fx,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count()', 'Fx::ratio(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),1000)')
  s=s.replace('std::stod(arg)','J::parse(arg).get<Fx>()')
 if name=='window_transform.hpp':
  for angle in ['w/2','h/2','(w+2*pad)/2','(h+2*pad)/2']:
   old='fixed::tan(fixed::rad('+angle+'))';assert s.count(old)==1,old
   s=s.replace(old,'fixed::tan_degrees('+angle+')')
  s=s.replace('yaw=y;pitch=p;width=w;height=h;padding=pad;ready=true;', 'yaw=y;pitch=p;width=w;height=h;padding=pad;ready=true;')
 (a.output/name).write_text(s)
# Integer-only host reference for mock pixels; no Geometry floating dependency.
native=src.parents[1]/'boayo-tilebatch-v14/software/boayo/native/src/geometry.cpp'
s=native.read_text();s=s[s.index('std::array<int64_t, 3> coordinates'):s.rindex('} // namespace boayo')]
s=s.replace('unsigned row = unsigned(std::sqrt(cell)), col = (cell - row * row) / 2,', 'unsigned row=0;while((row+1)*(row+1)<=cell)++row;unsigned col = (cell - row * row) / 2,')
(a.output/'fixed_reference.cpp').write_text('#include "geometry.hpp"\nnamespace boayo {\n'+s+'}\n')
print('PREPARED_FIXED',a.output)
