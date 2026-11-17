"""Interactive keys on the existing Linux UART console; no host-side package."""
import math
import os
import select
import sys
import termios
import tty
from bosio_wm_client import BosioWMClient


class Keys:
    def __init__(self,client,step=2.0):
        if not math.isfinite(step) or not 0.1<=step<=30:raise ValueError('step must be 0.1..30 degrees')
        self.client=client;self.step=step;self.escape=b'';self.last_cr=False

    def byte(self,b):
        # CSI and application-mode arrow keys, including split UART reads.
        if self.escape:
            self.escape+=bytes([b])
            if self.escape in (b'\x1b[',b'\x1bO'):return None
            arrows={b'\x1b[A':'w',b'\x1b[B':'s',b'\x1b[C':'d',b'\x1b[D':'a',
                    b'\x1bOA':'w',b'\x1bOB':'s',b'\x1bOC':'d',b'\x1bOD':'a'}
            key=arrows.get(self.escape);self.escape=b''
            if key is None:return None
        elif b==27:self.escape=b'\x1b';return None
        else:key=chr(b).lower()
        if key=='\n' and self.last_cr:self.last_cr=False;return None
        self.last_cr=key=='\r'
        if key in ('q','\x03','\x04'):return 'quit'
        moves={'w':(0,self.step),'s':(0,-self.step),'a':(-self.step,0),'d':(self.step,0)}
        if key in moves:
            yaw,pitch=moves[key];return self.client.call('view_move',yaw=yaw,pitch=pitch)
        if key in (' ','\r','\n'):return self.client.call('view_click')
        if key=='r':return self.client.call('view_reset')
        if key in ('+','='):self.step=min(30,self.step*2)
        elif key=='-':self.step=max(.1,self.step/2)
        else:return None
        return dict(step=self.step)


def main(step=2.0):
    fd=sys.stdin.fileno()
    if not os.isatty(fd):raise RuntimeError('boayo-keys requires an interactive UART terminal')
    saved=termios.tcgetattr(fd)
    try:
        with BosioWMClient('uart-keyboard',timeout=120) as client:
            keys=Keys(client,step)
            pose=client.call('view_state')
            tty.setcbreak(fd)
            mode=termios.tcgetattr(fd)
            mode[0]&=~(termios.ICRNL|termios.INLCR|termios.IGNCR)
            termios.tcsetattr(fd,termios.TCSANOW,mode)
            print('BOAYO_KEYS_READY WASD/arrows=view Space/Enter=center-click R=reset +/-=step Q=exit',flush=True)
            print(pose,flush=True)
            while True:
                if not select.select([fd],[],[],.2)[0]:
                    keys.escape=b'';continue
                data=os.read(fd,32)
                if not data:break
                for b in data:
                    result=keys.byte(b)
                    if result=='quit':return
                    if result is not None:print(result,flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(fd,termios.TCSANOW,saved)
        print('\nBOAYO_KEYS_EXIT (WM remains running)',flush=True)


if __name__=='__main__':main()
