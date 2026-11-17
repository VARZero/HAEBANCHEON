"""Display the explicitly identified physical HDMI capture card; save its latest raw frame."""
from pathlib import Path
import json,subprocess,threading,time,shutil,tkinter as tk
from PIL import Image,ImageTk
from datetime import datetime
root=Path(__file__).resolve().parent/'hdmi_live';root.mkdir(exist_ok=True)
width,height=1280,720;size=width*height*3
log=(root/'ffmpeg.log').open('wb')
proc=subprocess.Popen(['C:\\Users\\ssm01\\AppData\\Local\\Microsoft\\WinGet\\Packages\\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\\ffmpeg-7.1.1-full_build\\bin\\ffmpeg.exe','-hide_banner','-loglevel','warning',
 '-f','dshow','-video_size','1280x720','-framerate','30','-vcodec','mjpeg',
 '-i','video=USB3.0 Video','-an','-pix_fmt','rgb24','-f','rawvideo','pipe:1'],
 stdout=subprocess.PIPE,stderr=log,creationflags=subprocess.CREATE_NO_WINDOW)
latest=None;counter=0;error=None;closing=False
def read_frames():
 global latest,counter,error
 try:
  while not closing:
   frame=bytearray()
   while len(frame)<size:
    data=proc.stdout.read(size-len(frame))
    if not data:raise EOFError('Capture stopped; see ffmpeg.log')
    frame.extend(data)
   latest=bytes(frame);counter+=1
 except Exception as exc:error=str(exc)
threading.Thread(target=read_frames,daemon=True).start()
window=tk.Tk();window.title('ORYUKDO - LIVE HDMI capture (USB3.0 Video)')
window.geometry('1280x755')
screen=tk.Label(window,bg='black');screen.pack(fill='both',expand=True)
label=tk.Label(window,text='Opening physical HDMI capture...',anchor='w');label.pack(fill='x')
last_count=-1;last_save=0;photo=None;started=time.monotonic()
def update_once():
 global last_count,last_save,photo
 if counter!=last_count and latest is not None:
  frame=Image.frombytes('RGB',(width,height),latest)
  photo=ImageTk.PhotoImage(frame);screen.configure(image=photo)
  last_count=counter
  label.configure(text=f'LIVE HDMI | 1280x720 | received frames: {counter} | {datetime.now():%H:%M:%S}')
  if time.monotonic()-last_save>1:
   temporary=root/'latest.tmp.png';frame.save(temporary);temporary.replace(root/'latest.png')
   state={'source':'USB3.0 Video / user-confirmed board HDMI capture','frames':counter,
          'updated':datetime.now().isoformat(),'elapsed_seconds':round(time.monotonic()-started,1)}
   (root/'status.json').write_text(json.dumps(state,indent=2));last_save=time.monotonic()
 if error:label.configure(text=error,fg='red')
 if not closing:window.after(33,update)
def update():
 try:
  update_once()
 except Exception:
  import traceback
  with (root/'viewer_errors.log').open('a') as errors:
   errors.write(datetime.now().isoformat()+'\n'+traceback.format_exc())
  if not closing:window.after(100,update)

def close():
 global closing
 closing=True;proc.terminate()
 try:proc.wait(timeout=3)
 except subprocess.TimeoutExpired:proc.kill()
 log.close();window.destroy()
window.protocol('WM_DELETE_WINDOW',close);window.after(50,update);window.mainloop()
