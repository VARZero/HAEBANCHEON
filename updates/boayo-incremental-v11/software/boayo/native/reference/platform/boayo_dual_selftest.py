"""Same Linux scheduler/output adapter exercised by a model or real RTL bridge."""
import hashlib
import json
import numpy as np
from types import SimpleNamespace
from projection_model import reference_job
from bosio_hw_compositor import HardwareCompositor,HEADER,WORDS
from kfb_projectors import LinuxProjectors,HostBuffer
from bosio_kfb import KFBOutput


class ModelDevice:
    """Explicit integer-model DMA backend, never a hardware PASS."""
    def __init__(self):
        self.regions=[np.zeros(n,dtype=np.uint32) for n in (1048576,1152,1152,1084816,1084816,65536)]
        self.banks=self.regions[3:5]
        self.active=-1;self.uploads=self.commits=0;self.runs=[]
    def info(self):return dict(abi=2,active_bank=self.active,poisoned=0)
    def upload_region(self,region,offset,words):
        assert not (region in (3,4) and region-3==self.active)
        assert offset>=0 and offset+len(words)<=len(self.regions[region])
        self.regions[region][offset:offset+len(words)]=words;self.uploads+=len(words)
    def upload(self,bank,offset,words):self.upload_region(3+bank,offset,words)
    def project(self,count,texture_words,jobs):
        assert count<=65536 and max(jobs)<=64
        cells=[]
        for core,n in enumerate(jobs):
            cells.append(sum(reference_job(self.regions[0][:texture_words],self.regions[5][:count],d)
                for d in self.regions[1+core][:n*18].reshape(-1,18)))
        self.runs.append(jobs)
        return [0,0],cells
    def download(self,offset,count):return self.regions[5][offset:offset+count].copy()
    def present(self,bank,count,resolution,camera):
        assert bank!=self.active and resolution==2
        self.active=bank;self.commits+=1
    def close(self):pass


class CheckedScheduler(LinuxProjectors):
    def run(self,texture,desc,scene,**kwargs):
        expected=scene.copy()
        expected_cells=sum(reference_job(texture,expected,d) for d in desc)
        result=super().run(texture,desc,scene,**kwargs)
        np.testing.assert_array_equal(scene,expected)
        assert sum(result['written_cells_per_core'])==expected_cells
        return result


class RejectFallback:
    def render_packed(self,*args):raise AssertionError('unexpected CPU projection fallback')
    def render_update(self,*args):raise AssertionError('unexpected CPU projection fallback')
    def close(self):pass


def exercise(device,capture=False,output=None):
    from boayo_shell import BoayoLauncherShell
    hw=HardwareCompositor(RejectFallback(),np.empty((0,3),dtype=np.float32),
                          scheduler=CheckedScheduler(device),allocator=HostBuffer)
    display=KFBOutput(32,device);display.compositor=hw
    shell=BoayoLauncherShell(640,360)
    yy,xx=np.mgrid[:64,:96]
    gradient=np.stack([(xx*17+yy*3)%256,(yy*19+xx)%256,(xx*7+yy*11)%256],axis=-1).astype(np.uint8)
    def window(surface,yaw,width,height):
        h,w=surface.shape[:2]
        return SimpleNamespace(surface_width=w,surface_height=h,azimuth=yaw,elevation=0,roll=0,
            width_deg=width,height_deg=height,shape=[],decorated=False,surface_revision=1,
            surface=surface.copy(),dirty_rect=(0,0,w,h))
    windows={1:window(shell.render(),0,42,30),2:window(gradient,16,22,18)}
    order=[1,2];records=[]
    def draw(label):
        before=device.commits
        scene,_,kind=hw.render_update(windows,order,None,(0,0,False),(2,6,14),32)
        if kind=='full':display.upload_words(scene)
        else:display.upload_patch(scene)
        expected=hw.reference_snapshot()
        palette=expected[:256].copy()
        expected[:256]=(palette&0xff0000)|((palette&255)<<8)|((palette>>8)&255)
        np.testing.assert_array_equal(display.shadow,expected)
        for bank in device.banks:np.testing.assert_array_equal(bank[:len(expected)],expected)
        item=dict(label=label,kind=kind,commits=device.commits-before,**hw.metrics)
        records.append(item)
        print('STEP',label,'jobs',item['jobs_per_core'],'changed',item['changed_tiles'],'words',len(scene),'commits',item['commits'],flush=True)
        return item
    try:
        first=draw('initial launcher + gradient')
        assert min(first['jobs_per_core'])>0
        idle=draw('unchanged');assert idle['jobs_per_core']==[0,0] and idle['commits']==0
        windows[1].surface_revision+=1;windows[1].dirty_rect=(0,0,640,360)
        idle=draw('identical upload');assert idle['jobs_per_core']==[0,0] and idle['commits']==0
        w=windows[2];w.surface[28:36,40:48]=[255,0,0];w.surface_revision+=1;w.dirty_rect=(40,28,8,8)
        patch=draw('8x8 content edit')
        assert patch['kind']=='patch' and 0<sum(patch['jobs_per_core'])<sum(first['jobs_per_core'])
        assert patch['output_words']<WORDS
        order.reverse();z=draw('z-order');assert z['jobs_per_core']==[0,0]
        windows[2].azimuth=22;draw('move window')
        windows[1].shape=[[50,36,538,252,18]];shape=draw('rounded shape');assert shape['jobs_per_core']==[0,0]
        order.remove(2);hidden=draw('hide window');assert hidden['jobs_per_core']==[0,0]
        order.append(2);shown=draw('show cached window');assert shown['jobs_per_core']==[0,0]
        if capture:
            a=device.frame();b=device.frame();np.testing.assert_array_equal(a,b)
            colors=len(np.unique(a));assert colors>8
            print('PASS RTL scanout: two identical 1280x720 frames, colors='+str(colors),flush=True)
            if output:
                from PIL import Image
                rgb=np.stack([(a>>16)&255,(a>>8)&255,a&255],axis=-1).astype(np.uint8)
                Image.fromarray(rgb).save(output+'/rtl_boayo_frame.png')
                records.append(dict(frame_sha256=hashlib.sha256(a.tobytes()).hexdigest(),unique_colors=colors,**device.stats()))
        return records
    finally:
        hw.close();display.close()


def main():
    result=exercise(ModelDevice())
    print('BOAYO_DUAL_MODEL_PASS '+json.dumps({'steps':len(result),'scope':'integer model, not FPGA'}),flush=True)

if __name__=='__main__':main()
