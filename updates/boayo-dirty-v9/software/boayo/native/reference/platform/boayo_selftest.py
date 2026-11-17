"""Offline runtime + real WM IPC + native compositor test, no hardware access."""
import io
import json
import os
from pathlib import Path
import tempfile
import threading
import time
import numpy as np
from PIL import Image
from bosio_kfb import KFBOutput


class MemoryDevice:
    """Explicit mock: checks buffer ownership, does not model scanout timing."""
    def __init__(self):
        self.active=-1
        self.banks=[np.zeros(1084816,dtype=np.uint32) for _ in range(2)]
        self.present_count=0
        self.write_words=0
    def info(self): return {'abi':1,'active_bank':self.active,'poisoned':0}
    def upload(self, bank, offset, words):
        assert bank!=self.active, 'write to active bank'
        self.banks[bank][offset:offset+len(words)]=words
        self.write_words+=len(words)
    def present(self,bank,count,resolution,camera):
        assert bank!=self.active and count%16==0 and len(camera)==180
        self.active=bank
        self.present_count+=1
    def close(self): pass


def backend_test():
    device=MemoryDevice()
    output=KFBOutput(8,device)
    scene=np.zeros(4496,dtype=np.uint32)
    scene[256:4476]=0xffffffff
    scene[256]=0
    scene[1:4]=[0xff0000,0x0000ff,0x00ff00]
    output.upload_words(scene)
    assert list(device.banks[device.active][1:4])==[0xff0000,0x00ff00,0x0000ff]
    patch=np.zeros(48,dtype=np.uint32)
    patch[:4]=[0x42505431,16,1,48]
    patch[16:18]=[0,0]
    for n in range(6):
        patch[32:48]=0x01010101+n
        old=device.write_words
        assert output.upload_patch(patch)==1
        assert device.write_words-old==32, 'partial update expanded to full upload'
        for bank in device.banks:
            assert np.all(bank[4476:4492]==0x01010101+n)
    before=device.present_count
    output.upload_patch([])
    empty=np.zeros(16,dtype=np.uint32); empty[:4]=[0x42505431,16,0,16]
    output.upload_patch(empty)
    assert device.present_count==before
    patch[17]=4220
    try: output.upload_patch(patch)
    except ValueError: pass
    else: raise AssertionError('invalid patch accepted')
    assert device.present_count==before
    def fail_present(*args): raise OSError('injected frame acknowledgement failure')
    device.present=fail_present
    patch[17]=0
    try: output.upload_patch(patch)
    except OSError: pass
    else: raise AssertionError('completion failure hidden')
    assert output.failed
    writes=device.write_words
    try: output.upload_patch(patch)
    except RuntimeError: pass
    else: raise AssertionError('failed buffer reused')
    assert device.write_words==writes
    print('PASS backend mock: palette RGB, six bank swaps, dirty ranges, no-op, invalid patch',flush=True)


def main():
    import ctypes
    import socket
    import PIL
    print('RUNTIME',os.uname().machine,'numpy',np.__version__,'Pillow',PIL.__version__,flush=True)
    assert socket.AF_UNIX
    assert np.dtype('intp').itemsize==ctypes.sizeof(ctypes.c_void_p)
    matrix=np.array([[4.,7.],[2.,6.]])
    np.testing.assert_allclose(np.linalg.inv(matrix),[[.6,-.7],[-.2,.4]],atol=1e-12)
    np.testing.assert_allclose(matrix@np.linalg.inv(matrix),np.eye(2),atol=1e-12)
    assert int(np.array([0x100000000,7],dtype=np.uint64).sum())==0x100000007
    storage=bytearray(33);unaligned=np.ndarray((4,),dtype=np.float64,buffer=storage,offset=1)
    unaligned[:]=[1.25,-2.5,3.75,4.]
    np.testing.assert_allclose(unaligned*2,[2.5,-5.,7.5,8.])
    stream=io.BytesIO(); Image.new('RGB',(8,8),(20,40,60)).save(stream,format='PNG')
    stream.seek(0); assert Image.open(stream).getpixel((0,0))==(20,40,60)
    backend_test()
    # Compare actual native dirty-tile output against a separate full-render context.
    import base64
    from bosio_window_manager import SphericalWindowManager
    managers=[SphericalWindowManager(8) for _ in range(2)]
    devices=[MemoryDevice(),MemoryDevice()]
    outputs=[KFBOutput(8,d) for d in devices]
    ids=[m.create_window('test','dirty reference',surface_width=128,surface_height=128)['window_id'] for m in managers]
    kinds=[]
    for step in range(4):
        if step:
            pixels=np.full((16,16,3),[32*step,128,224],dtype=np.uint8)
            for manager,wid in zip(managers,ids):
                manager.update_surface('test',wid,48+step*2,48,16,16,base64.b64encode(pixels.tobytes()).decode())
        incremental,_,kind=managers[0].render_update();kinds.append(kind)
        if kind=='full': outputs[0].upload_words(incremental)
        else: outputs[0].upload_patch(incremental)
        full,_=managers[1].render_packed();outputs[1].upload_words(full)
        np.testing.assert_array_equal(outputs[0].shadow,outputs[1].shadow)
    assert kinds[0]=='full' and 'patch' in kinds[1:],kinds
    packet,_,kind=managers[0].render_update()
    assert kind=='patch' and (len(packet)==0 or int(packet[2])==0)
    before=outputs[0].commits;outputs[0].upload_patch(packet)
    assert before==outputs[0].commits
    for manager in managers: manager.native.close()
    print('PASS native incremental/full reference: four equal scenes, unchanged frame zero commit',flush=True)
    from bosio_wm_daemon import BosioWindowDaemon
    from bosio_wm_client import BosioWMClient
    from boayo_shell import BoayoLauncherShell
    with tempfile.TemporaryDirectory(prefix='boayo-test-') as tmp:
        sock=str(Path(tmp)/'wm.sock')
        daemon=BosioWindowDaemon(sock,m=8,fps=2,headless=True,sensor=False,antialias=False)
        assert daemon.manager.native is not None, 'native compositor missing'
        thread=threading.Thread(target=daemon.run,daemon=True); thread.start()
        end=time.monotonic()+120
        while not Path(sock).exists():
            assert thread.is_alive() and time.monotonic()<end
            time.sleep(.05)
        try:
            with BosioWMClient('selftest',sock,timeout=120) as c:
                assert 'spatial-workspace' in c.ping()['features']
                win=c.create_window('BoAYO runtime test',surface_width=32,surface_height=24)
                wid=win['window_id']
                c.fill(wid,[255,0,0])
                rgb=np.zeros((4,4,3),dtype=np.uint8);rgb[:,:,1]=255
                c.update_surface(wid,rgb,x=2,y=2)
                c.pointer_warp(0,0);c.call('input_click',button='left')
                state=c.get_state()
                assert state['render_error'] is None
                # Synchronize explicitly with the compositor to inspect actual packed output.
                with daemon.manager.lock:
                    full,_,_=daemon.manager.render_update()
                assert full.dtype==np.uint32
                shell=BoayoLauncherShell(640,360,os.environ.get('BOAYO_APPS_PATH'))
                assert shell.width==640
                assert shell.render().shape==(360,640,3)
                c.destroy_window(wid)
                print('PASS Unix IPC: create/fill/partial surface/pointer/click/destroy; native compositor and launcher loaded',flush=True)
        finally:
            daemon.stop();thread.join(10)
            assert not thread.is_alive()
    print('BOAYO_SELFTEST_PASS (software + mock backend; no FPGA access)',flush=True)


if __name__=='__main__': main()
