from pathlib import Path
import tkinter as tk
from tkinter.scrolledtext import ScrolledText
import time

root=Path(__file__).parent
window=tk.Tk()
window.title('HAEBANCHEON LIVE UART - COM4 - L1 16KB DMA3')
window.geometry('1100x720')
label=tk.Label(window,text='Live board UART / COM4 / 115200 baud / read-only',anchor='w')
label.pack(fill='x')
text=ScrolledText(window,bg='#101820',fg='#dce9e7',font=('Consolas',11),wrap='word')
text.pack(fill='both',expand=True)
offset=0
def update():
 global offset
 try:
  with (root/'uart.log').open('rb') as f:
   f.seek(offset);data=f.read();offset=f.tell()
  if data:
   text.insert('end',data.decode('utf-8','replace').replace('\r',''));text.see('end')
  label.config(text=f'Live board UART / COM4 / 115200 baud / {offset:,} bytes / {time.strftime("%H:%M:%S")}')
 except OSError:pass
 window.after(100,update)
window.after(10,update)
window.mainloop()
