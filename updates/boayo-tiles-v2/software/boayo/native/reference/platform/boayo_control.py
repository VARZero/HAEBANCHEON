"""UART control entry point; no systemd/PYNQ or board reconfiguration."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from bosio_wm_client import BosioWMClient


def main():
    p=argparse.ArgumentParser()
    p.add_argument('command',choices=('start','stop','status','move','click','app','selftest','dual-selftest','keys','view','center-click'))
    p.add_argument('values',nargs='*')
    p.add_argument('--headless',action='store_true')
    p.add_argument('--cpu',action='store_true')
    a=p.parse_args()
    if a.command=='keys':
        from boayo_keys import main as keys
        if len(a.values)>1:p.error('keys accepts an optional step in degrees')
        return keys(float(a.values[0]) if a.values else 2.0)
    if a.command=='selftest':
        from boayo_selftest import main as test
        return test()
    if a.command=='dual-selftest':
        from boayo_dual_selftest import main as test
        return test()
    if a.command=='start':
        import fcntl
        lock=open('/tmp/boayo-control.lock','w')
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        if Path('/tmp/bosio-wm.sock').exists():
            raise RuntimeError('WM socket already exists; inspect boayo-status first')
        log=open('/tmp/boayo-wm.log','ab',buffering=0)
        cmd=[sys.executable,'/opt/boayo/platform/boayo_kfb_daemon.py']
        if a.headless: cmd+=['--headless']
        if a.cpu: cmd+=['--cpu']
        daemon=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
        try:
            end=time.monotonic()+600
            while not Path('/tmp/bosio-wm.sock').exists():
                if daemon.poll() is not None:
                    raise RuntimeError('WM failed; read /tmp/boayo-wm.log')
                if time.monotonic()>end:
                    raise TimeoutError('WM startup exceeded 600 seconds')
                time.sleep(.2)
            desktop_log=open('/tmp/boayo-desktop.log','ab',buffering=0)
            desktop=subprocess.Popen([sys.executable,'/opt/boayo/apps/boayo_native_desktop.py'],
                stdin=subprocess.DEVNULL,stdout=desktop_log,stderr=desktop_log,start_new_session=True)
            Path('/tmp/boayo-processes.json').write_text(json.dumps({'wm':daemon.pid,'desktop':desktop.pid}))
            print('BoAYO started; boayo-status / boayo-move 5 0 / boayo-click / boayo-stop')
        except BaseException:
            daemon.terminate()
            raise
        return
    if a.command=='stop':
        # Only terminate the desktop when /proc still identifies our entry point.
        path=Path('/tmp/boayo-processes.json')
        if path.exists():
            processes=json.loads(path.read_text())
            for pid in [processes['desktop']]+processes.get('apps',[]):
                cmdline=Path(f'/proc/{int(pid)}/cmdline')
                if cmdline.exists() and b'/opt/boayo/apps/boayo_' in cmdline.read_bytes():
                    try: os.kill(int(pid),signal.SIGTERM)
                    except ProcessLookupError: pass
            path.unlink()
        with BosioWMClient('uart-stop',timeout=120) as c:
            c.call('shutdown')
        return
    if a.command=='app':
        apps={'dashboard':('boayo_example_app.py','Dashboard'),
              'telemetry':('boayo_telemetry_app.py','Telemetry'),
              'pulse':('boayo_pulse_app.py','SDK Pulse')}
        if len(a.values)!=1 or a.values[0] not in apps:
            p.error('app requires dashboard, telemetry, or pulse')
        name,title=apps[a.values[0]]
        path=Path('/tmp/boayo-processes.json');processes=json.loads(path.read_text())
        log=open('/tmp/boayo-'+a.values[0]+'.log','ab',buffering=0)
        child=subprocess.Popen([sys.executable,'/opt/boayo/apps/'+name],stdin=subprocess.DEVNULL,
                               stdout=log,stderr=log,start_new_session=True)
        processes.setdefault('apps',[]).append(child.pid)
        path.write_text(json.dumps(processes))
        with BosioWMClient('uart-app',timeout=120) as c:
            end=time.monotonic()+600
            while True:
                if child.poll() is not None: raise RuntimeError('App failed; inspect /tmp/boayo-'+a.values[0]+'.log')
                if any(w.get('title')==title for w in c.get_state().get('windows',[])): break
                if time.monotonic()>end:
                    child.terminate();raise TimeoutError('App window startup')
                time.sleep(.2)
        desktop=int(processes['desktop']);cmdline=Path(f'/proc/{desktop}/cmdline')
        if cmdline.exists() and b'/opt/boayo/apps/boayo_native_desktop.py' in cmdline.read_bytes():
            os.kill(desktop,signal.SIGUSR1)
        print('BOAYO_APP_READY',title,child.pid)
        return
    with BosioWMClient('uart-control',timeout=120) as c:
        if a.command=='status': print(json.dumps(c.get_state(),indent=2))
        elif a.command=='move':
            if len(a.values)!=2: p.error('move requires azimuth elevation deltas')
            print(c.pointer_move(*map(float,a.values)))
        elif a.command=='click': print(c.call('input_click',button='left'))
        elif a.command=='view':
            if len(a.values)!=2:p.error('view requires yaw pitch deltas in degrees')
            print(c.call('view_move',yaw=float(a.values[0]),pitch=float(a.values[1])))
        elif a.command=='center-click':print(c.call('view_click'))


if __name__=='__main__':
    main()
