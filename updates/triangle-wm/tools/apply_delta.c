#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char**argv){
 if(argc!=4)return 2;
 FILE*base=fopen(argv[1],"rb"),*patch=fopen(argv[2],"rb"),*out=NULL;uint32_t h[4],written=0,hash=2166136261u;unsigned char buf[16384];int rc=1;
 if(!base||!patch||fread(h,sizeof(h),1,patch)!=1||h[0]!=0x54525031||h[2]>8*1024*1024)goto done;
 if(fseek(base,0,SEEK_END)||ftell(base)!=h[1])goto done;
 out=fopen(argv[3],"wb");if(!out)goto done;
 while(written<h[2]){uint32_t op[3];if(fread(op,sizeof(op),1,patch)!=1||op[0]>1||!op[2]||op[2]>h[2]-written)goto done;
  if(op[0]==0&&(op[1]>h[1]||op[2]>h[1]-op[1]||fseek(base,op[1],SEEK_SET)))goto done;
  uint32_t left=op[2];while(left){unsigned n=left>sizeof(buf)?sizeof(buf):left;if(fread(buf,1,n,op[0]?patch:base)!=n)goto done;for(unsigned i=0;i<n;i++)hash=(hash^buf[i])*16777619u;if(fwrite(buf,1,n,out)!=n)goto done;left-=n;written+=n;}
 }
 if(hash!=h[3]||fgetc(patch)!=EOF)goto done;
 rc=0;
done:
 if(base)fclose(base);
 if(patch)fclose(patch);
 if(out&&fclose(out))rc=1;
 if(rc){if(out)unlink(argv[3]);fputs("PATCH_FAIL\n",stderr);}else puts("PATCH_OK");return rc;
}
