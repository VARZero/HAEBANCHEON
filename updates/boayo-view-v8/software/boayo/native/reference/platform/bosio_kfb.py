"""KFB Linux backend with frame-atomic BPT1/BPT2 scanout updates."""
import fcntl
import os
import struct
import numpy as np
from bosio_geometry_v2 import camera_coefficients, pack_scene


class LinuxDevice:
    def __init__(self, path='/dev/bosio0'):
        self.fd = os.open(path, os.O_RDWR | os.O_CLOEXEC)
        try:
            info = self.info()
            if info['abi'] not in (1,2,3) or info['poisoned']:
                raise RuntimeError('BOSIO ABI mismatch or poisoned device')
        except BaseException:
            os.close(self.fd)
            raise

    def info(self):
        data = bytearray(36)
        fcntl.ioctl(self.fd, 0x80244240, data, True)
        return dict(zip(('abi','signature','capability','status','active_base',
                         'active_bank','poisoned','max_jobs','max_scene_words'),
                        struct.unpack('<5Ii3I', data)))

    def upload(self, bank, offset, words):
        self.upload_region(3+bank,offset,words)

    def upload_region(self, region, offset, words):
        words = np.ascontiguousarray(words, dtype='<u4')
        fcntl.ioctl(self.fd, 0x40184241,
                    struct.pack('<4IQ', region, offset*4, words.nbytes, 0,
                                words.ctypes.data))

    def project(self, count, texture_words, jobs):
        data=bytearray(struct.pack('<10I',2,count,texture_words,*jobs,30000,0,0,0,0))
        fcntl.ioctl(self.fd,0xc0284242,data,True)
        values=struct.unpack('<10I',data)
        return list(values[6:8]),list(values[8:10])

    def download(self, offset, count):
        words=np.empty(count,dtype='<u4')
        fcntl.ioctl(self.fd,0x40184245,struct.pack('<4IQ',5,offset*4,words.nbytes,0,words.ctypes.data))
        return words

    def present(self, bank, count, resolution, camera):
        fcntl.ioctl(self.fd, 0x42e04243,
                    struct.pack('<184I', bank, count, resolution, 30000,
                                *(int(x) & 0xffffffff for x in camera)))

    def close(self):
        if self.fd is not None:
            try:
                fcntl.ioctl(self.fd, 0x4244)
            finally:
                os.close(self.fd)
                self.fd = None

    def set_pose(self, camera):
        if self.info()['abi']<3:
            raise RuntimeError('View control requires the UART-view SD image (BOSIO ABI 3)')
        fcntl.ioctl(self.fd,0x42d44246,struct.pack('<181I',30000,
                    *(int(x)&0xffffffff for x in camera)))


class KFBOutput:
    def __init__(self, m=16, device=None):
        if m not in (8,16,32):
            raise ValueError('M must be 8, 16, or 32')
        self.m = m
        self.device = device if device is not None else LinuxDevice()
        self.active = self.device.info()['active_bank']
        self.shadow = None
        self.failed = False
        self.commits = self.uploaded_words = 0
        self.camera = camera_coefficients().reshape(-1)
        self.compositor = None

    def _upload(self, bank, offset, words):
        self.device.upload(bank, offset, words)
        self.uploaded_words += len(words)

    def _commit(self, scene, ranges):
        if self.failed:
            raise RuntimeError('Previous upload failed; restart required')
        target = 0 if self.active < 0 else self.active ^ 1
        try:
            for offset, words in ranges:
                self._upload(target, offset, words)
            self.device.present(target, len(scene), {8:0,16:1,32:2}[self.m], self.camera)
            # PRESENT acknowledges the new active base and the frame boundary.
            self.active = target
            self.commits += 1
            for offset, words in ranges:
                self._upload(target ^ 1, offset, words)
            self.shadow = scene
            if self.compositor is not None:
                self.compositor.ack_presented()
        except BaseException:
            self.failed = True
            if self.compositor is not None:
                self.compositor.invalidate_presentation()
            raise

    def upload(self, rgb):
        return self.upload_words(pack_scene(rgb, self.m)[0])

    def upload_words(self, words):
        scene = np.array(words, dtype='<u4', copy=True).reshape(-1)
        if len(scene)<4480 or len(scene)>1084816 or len(scene)%16:
            raise ValueError('Invalid full scene length')
        # Upstream software palette is RBG; KFB scanout/HDMI is RGB888.
        palette = scene[:256].copy()
        scene[:256] = (palette & 0xff0000) | ((palette & 255)<<8) | ((palette>>8)&255)
        self._commit(scene, [(0, scene)])

    def upload_patch(self, words):
        patch = np.asarray(words, dtype='<u4').reshape(-1)
        if not len(patch):
            return 0
        if self.shadow is None or len(patch)<16 or int(patch[0]) not in (0x42505431,0x42505432):
            raise ValueError('BPT1/BPT2 requires a full scene and valid header')
        bpt2=int(patch[0])==0x42505432
        size, count = int(patch[1]), int(patch[2])
        if size!=self.m*self.m//4 or count>4220 or int(patch[3])!=len(patch) or len(patch)!=16+count*(16+size):
            raise ValueError('Invalid BPT1 size')
        ranges=[]
        for n in range(count):
            start=16+n*(16+size)
            offset,tile=int(patch[start]),int(patch[start+1])
            if tile>=4220 or offset+4476+size>len(self.shadow):
                raise ValueError('BPT1 directory mismatch')
            if bpt2:
                entry=int(patch[start+2])
                if self.m!=32 or offset!=tile*256 or entry not in (offset*4,0xffffffff):
                    raise ValueError('BPT2 fixed-slot directory mismatch')
                ranges.append((256+tile,np.array([entry],dtype='<u4')))
            elif int(self.shadow[256+tile])!=offset*4:
                raise ValueError('BPT1 directory mismatch')
            ranges.append((4476+offset, patch[start+16:start+16+size].copy()))
        if not ranges:
            return 0
        scene=self.shadow.copy()
        for offset,data in ranges:
            scene[offset:offset+len(data)]=data
        self._commit(scene, ranges)
        return count

    def set_antialias(self, enabled, *args):
        if enabled:
            raise ValueError('Linux ABI 1 does not expose antialias control')

    def set_pose(self, yaw, pitch):
        camera=camera_coefficients(yaw,pitch).reshape(-1)
        try:
            self.device.set_pose(camera)
            self.camera=camera
        except BaseException:
            self.failed=True
            raise

    def status(self):
        return dict(self.device.info(), backend='kfb-linux-frame-atomic-output',
                    commits=self.commits, uploaded_words=self.uploaded_words,
                    sensor_available=False, antialias=False)

    def close(self):
        self.device.close()
