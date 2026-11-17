"""Two-core scheduler through Linux ABI 2; no PYNQ/physical-address access."""
import time
import numpy as np

HEADER=4476
TILE_WORDS=256
MAX_BATCH=128


class HostBuffer(np.ndarray):
    def __new__(cls,shape,dtype):
        return np.zeros(shape,dtype=dtype).view(cls)
    def freebuffer(self):
        # This is ordinary process memory. The kernel owns all DMA allocations.
        pass


class LinuxProjectors:
    def __init__(self,device):
        if device.info()['abi'] not in (2,3):
            raise RuntimeError('Dual projection requires BOSIO Linux ABI 2')
        self.device=device
        self.failed=False

    def run(self,texture,descriptors,scene,workers=2,texture_words=None,scene_words=None,**unused):
        if self.failed: raise RuntimeError('Previous projection failed; restart required')
        if workers!=2: raise ValueError('KFB has two projection engines')
        started=time.perf_counter()
        desc=np.ascontiguousarray(descriptors,dtype='<u4').reshape(-1,18)
        texture=np.ascontiguousarray(texture,dtype='<u4').reshape(-1)
        count=len(texture) if texture_words is None else int(texture_words)
        bound=len(scene) if scene_words is None else int(scene_words)
        if not 0<count<=min(len(texture),0x100000) or not 0<=bound<=len(scene):
            raise ValueError('Texture/scene allocation bounds')
        if scene.dtype!=np.uint32 or not scene.flags.c_contiguous: raise ValueError('uint32 scene required')
        destinations=[]
        for d in desc:
            src,dst,width,height,stride,flags=map(int,d[:6])
            if dst+256>bound or not 1<=width<=2048 or not 1<=height<=2048 or stride<width or src+(height-1)*stride+width>count or flags&~0xff01:
                raise ValueError('Invalid projection descriptor')
            destinations.append(dst)
        ordered=sorted(destinations)
        if any(a+256>b for a,b in zip(ordered,ordered[1:])):
            raise ValueError('Overlapping projection destinations')
        jobs=[0,0];cells=[0,0];statuses=[0,0]
        if not len(desc):
            return dict(status=statuses,jobs_per_core=jobs,written_cells_per_core=cells,milliseconds=0,total_call_ms=0)
        try:
            self.device.upload_region(0,0,texture[:count])
            for first in range(0,len(desc),MAX_BATCH):
                chunk=desc[first:first+MAX_BATCH].copy()
                original=chunk[:,1].copy()
                chunk[:,1]=HEADER+np.arange(len(chunk),dtype=np.uint32)*256
                words=(HEADER+len(chunk)*256+15)&~15
                # Only descriptors without CLEAR need previous tile contents.
                for i,d in enumerate(chunk):
                    if not int(d[5])&1:
                        dst=int(original[i]);self.device.upload_region(5,HEADER+i*256,scene[dst:dst+256])
                queues=[chunk[::2],chunk[1::2]]
                sizes=[len(q) for q in queues]
                for core,q in enumerate(queues):
                    if len(q): self.device.upload_region(1+core,0,q.reshape(-1))
                status,written=self.device.project(words,count,sizes)
                if any(status):raise RuntimeError('Projection core error '+repr(status))
                result=self.device.download(HEADER,len(chunk)*256)
                for i,dst in enumerate(original):
                    scene[int(dst):int(dst)+256]=result[i*256:(i+1)*256]
                jobs=[a+b for a,b in zip(jobs,sizes)]
                cells=[a+b for a,b in zip(cells,written)]
        except BaseException:
            self.failed=True
            raise
        elapsed=(time.perf_counter()-started)*1000
        return dict(status=statuses,jobs_per_core=jobs,written_cells_per_core=cells,
                    milliseconds=elapsed,total_call_ms=elapsed)

    def close(self):
        # The output adapter closes the shared, exclusive device after draining.
        pass
