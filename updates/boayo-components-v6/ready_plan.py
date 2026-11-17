"""Build-host device commands. No framebuffer pixels are generated or cleared."""
import struct

def device_plan(data):
    n=struct.unpack_from('<I',data,8)[0]
    records=[struct.unpack_from('<21I',data,4096+i*84) for i in range(n)]
    direct=[r for r in records if r[1]==1]
    masked=[r for r in records if r[1]==2]
    assert len(direct)+len(masked)<=128
    words=[0]*18
    words[:4]=[0x31594452,1,0,struct.unpack_from('<I',data,20)[0]]
    for phase,items in enumerate((direct,masked)):
        for core in range(2):
            slot=phase*2+core;words[8+slot]=len(words)
            for j in range(core,len(items),2):
                r=items[j];desc=list(r[3:]);desc[1]=4476+(j if phase else r[0])*256;desc[5]=1
                words.extend(desc);words[4+slot]+=1
    for kind in range(2):
        for bank in range(2):
            words[12+kind*2+bank]=len(words)
            for j,r in enumerate(masked):
                entry=[0]*40
                if kind==0:entry[:7]=[1,0,3+bank,(4476+r[0]*256)*4,1024,0,0]
                else:
                    entry[:8]=[2,0,bank,r[0],4476+j*256,0,0,0]
                    entry[8:]=struct.unpack_from('<32I',data,4096+r[2]+1024)
                words.extend(entry)
    words[16]=len(words);words.extend([0xffffffff]*4220)
    for r in direct+masked:words[words[16]+r[0]]=r[0]*1024
    words[2]=len(words)
    return struct.pack('<%dI'%len(words),*words)
