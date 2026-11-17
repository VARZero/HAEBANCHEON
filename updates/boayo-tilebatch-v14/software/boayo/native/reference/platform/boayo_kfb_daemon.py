import argparse
import signal
import math
import threading
from bosio_wm_daemon import BosioWindowDaemon
from bosio_kfb import KFBOutput
from kfb_projectors import LinuxProjectors,HostBuffer


class KFBDaemon(BosioWindowDaemon):
    def __init__(self,*args,device=None,cpu=False,**kwargs):
        self.kfb_device=device
        self.cpu=cpu
        self.view_lock=threading.RLock()
        self.view_yaw=self.view_pitch=0.0
        super().__init__(*args,**kwargs)

    def dispatch(self, request):
        op=request.get('op')
        if op in ('view_move','view_reset','view_click','view_state'):
            with self.view_lock:
                if op in ('view_move','view_reset'):
                    args=request.get('args',{})
                    dy=float(args.get('yaw',0));dp=float(args.get('pitch',0))
                    if not (math.isfinite(dy) and math.isfinite(dp)):
                        raise ValueError('View angles must be finite')
                    yaw=0.0 if op=='view_reset' else (self.view_yaw+dy+180)%360-180
                    pitch=0.0 if op=='view_reset' else max(-85.0,min(85.0,self.view_pitch+dp))
                    if self.driver is not None:
                        with self.driver_lock:self.driver.set_pose(yaw,pitch)
                    self.view_yaw,self.view_pitch=yaw,pitch
                    self.manager.pointer_warp(yaw,pitch)
                if op=='view_click':
                    # Always re-align the hit-test ray to the acknowledged display pose.
                    self.manager.pointer_warp(self.view_yaw,self.view_pitch)
                    click=dict(request,op='input_click',args={'button':'left'})
                    pointer=super().dispatch(click)
                    return dict(yaw=self.view_yaw,pitch=self.view_pitch,pointer=pointer)
                return dict(yaw=self.view_yaw,pitch=self.view_pitch)
        if request.get('op')=='shutdown':
            self.stop()
            return True
        # External scene replacement must invalidate the incremental WM snapshot.
        if request.get('op') in ('claim_scene','upload_scene_words','release_scene'):
            if hasattr(self.manager.native,'invalidate_presentation'):
                self.manager.native.invalidate_presentation()
        result=super().dispatch(request)
        if request.get('op')=='get_state' and isinstance(result,dict):
            result['compositor']=getattr(self.manager.native,'name','cpp-scalar-rv32')
            result['projection']=dict(getattr(self.manager.native,'metrics',{}))
        return result

    def _start_driver(self):
        if self.headless:
            return
        self.driver = KFBOutput(self.manager.m,device=self.kfb_device)
        try:
            if not self.cpu:
                if self.manager.m!=32:raise ValueError('Hardware projectors require M=32')
                from bosio_hw_compositor import HardwareCompositor
                self.manager.native=HardwareCompositor(self.manager.native,self.manager._rays,
                    scheduler=LinuxProjectors(self.driver.device),allocator=HostBuffer)
                self.manager.native.name='kfb-dual-projectors+cpu-tile-composition'
                self.driver.compositor=self.manager.native
            scene, _, _ = self.manager.render_update()
            self.driver.upload_words(scene)
            self.output_status = self.driver.status()
        except BaseException:
            self.driver.close()
            raise


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--headless',action='store_true',help='Explicit software-only test; no device access')
    parser.add_argument('--socket',default='/tmp/bosio-wm.sock')
    parser.add_argument('--m',type=int,choices=(8,16,32))
    parser.add_argument('--cpu',action='store_true',help='Explicit CPU projection fallback')
    parser.add_argument('--fps',type=float,default=2)
    args=parser.parse_args()
    m=args.m or (16 if args.headless or args.cpu else 32)
    daemon=KFBDaemon(args.socket,m=m,cpu=args.cpu,fps=args.fps,headless=args.headless,sensor=False,antialias=False)
    if daemon.manager.native is None:
        raise RuntimeError('RV32 native compositor is required')
    signal.signal(signal.SIGINT,daemon.stop)
    signal.signal(signal.SIGTERM,daemon.stop)
    daemon.run()


if __name__=='__main__':
    main()
