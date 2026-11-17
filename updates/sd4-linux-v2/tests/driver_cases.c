static struct or_sd *active;
static u32 regs[16];
static unsigned char disk[512*256],bounce[512],user[4096];
static unsigned commands,data_commands,status_commands,fail_block,fail_code;
static unsigned r1_error_block,status_busy,status_error,timeout_block;
static u32 seen_args[32],seen_ops[32];
static void writel(u32 v,void *p)
{
 if(p==regs+3) regs[3]&=~v; else *(u32*)p=v;
}
static int wait_for_completion_timeout(struct completion *c,unsigned long ticks)
{
 (void)ticks;
 unsigned op=regs[2]&63;
 jiffies+=10;commands++;
 regs[8]=R1_READY_FOR_DATA|(4<<9);
 if(op==13) {
  status_commands++;
  if(status_busy) {status_busy--;regs[8]=7<<9;}
  if(status_error) regs[8]|=BIT(19);
 } else if(regs[2]&BIT(9)) {
  assert(regs[7]<=512);
  unsigned idx=data_commands++;
  seen_args[idx]=regs[1];seen_ops[idx]=op;
  if(data_commands==timeout_block) return 0;
  if(data_commands==fail_block) {
   regs[3]=2|(fail_code<<4);or_sd_irq(0,active);return c->done;
  }
  if(data_commands==r1_error_block) regs[8]|=BIT(19);
  unsigned addr=active->mmc->card->blockaddr?regs[1]*512:regs[1];
  if(op==17 || op==24) {
   assert(addr+regs[7]<=sizeof(disk));
   if(op==24) memcpy(disk+addr,bounce,regs[7]);
   else memcpy(bounce,disk+addr,regs[7]);
  } else memset(bounce,0x53,regs[7]); /* SCR and other data commands */
 }
 regs[3]=2;assert(or_sd_irq(0,active)==IRQ_HANDLED);
 return c->done;
}
static struct mmc_card card;
static struct mmc_host host;
static struct or_sd sd;
static struct mmc_command cmd,stop,sbc;
static struct mmc_data data;
static struct mmc_request request;
static struct scatterlist sg[16];
static void setup(unsigned blocks,bool write,bool blockaddr)
{
 memset(regs,0,sizeof(regs));memset(&sd,0,sizeof(sd));
 memset(&cmd,0,sizeof(cmd));memset(&stop,0,sizeof(stop));memset(&sbc,0,sizeof(sbc));
 memset(user,0xcc,sizeof(user));memset(bounce,0,sizeof(bounce));
 for(unsigned i=0;i<sizeof(disk);i++) disk[i]=(i*13+(i>>9)*37)&255;
 card=(struct mmc_card){.rca=1,.blockaddr=blockaddr};host.card=&card;
 sd.mmc=&host;sd.regs=regs;sd.bounce=bounce;sd.dma=0x80010000;active=&sd;
 commands=data_commands=status_commands=completed=0;jiffies=0;
 fail_block=fail_code=r1_error_block=status_busy=status_error=timeout_block=0;
 /* Awkward SG boundaries exercise offset copies across 512-byte sectors. */
 size_t offset=0;
 for(unsigned i=0;i<8;i++) {sg[i]=(struct scatterlist){user+offset, i==7?4096-offset:(i*73+1)};offset+=sg[i].len;}
 cmd.opcode=write?25:18;cmd.arg=blockaddr?7:7*512;cmd.flags=MMC_RSP_R1;
 stop.opcode=12;stop.flags=MMC_RSP_R1|MMC_RSP_BUSY;
 sbc.opcode=23;sbc.arg=blocks;sbc.flags=MMC_RSP_R1;
 data=(struct mmc_data){.blksz=512,.blocks=blocks,.flags=write?MMC_DATA_WRITE:MMC_DATA_READ,.sg=sg,.sg_len=8};
 request=(struct mmc_request){.cmd=&cmd,.sbc=&sbc,.stop=&stop,.data=&data};
 sd.request=&request;
}
static void run(void) {or_sd_work(&sd.work);assert(completed==1);assert(!sd.request);}
int main(void)
{
 unsigned cases=0;
 for(unsigned blocks=1;blocks<=8;blocks++) for(int wr=0;wr<2;wr++) for(int ba=0;ba<2;ba++) {
  setup(blocks,wr,ba);
  if(wr) for(unsigned i=0;i<sizeof(user);i++) user[i]=(i*29+(i>>9)*11)&255;
  run();assert(!cmd.error && !data.error && data.bytes_xfered==blocks*512);
  assert(!stop.error && !sbc.error && data_commands==blocks);
  assert(commands==blocks*(wr?2:1));
  for(unsigned i=0;i<blocks;i++) {assert(seen_ops[i]==(wr?24:17));assert(seen_args[i]==(7+i)*(ba?1:512));}
  assert(!memcmp(user,disk+7*512,blocks*512));
  if(!wr) for(unsigned i=blocks*512;i<4096;i++) assert(user[i]==0xcc);
  cases++;
 }
 for(unsigned code=1;code<=5;code++) {
  setup(8,false,true);fail_block=4;fail_code=code;run();
  assert(cmd.error && data.error && data.bytes_xfered==1536 && data_commands==4);
  assert(!sd.dead);cases++;
 }
 setup(8,false,true);timeout_block=3;run();
 assert(sd.dead && data.error==-ETIMEDOUT && data.bytes_xfered==1024 && commands==3);
 cmd.error=0;or_sd_command(&sd,&cmd,NULL,0);assert(cmd.error==-EIO && commands==3);cases++;
 setup(8,false,true);r1_error_block=3;run();assert(data.error==-EIO && data.bytes_xfered==1024);cases++;
 setup(8,true,true);status_error=1;run();assert(data.error==-EIO && !data.bytes_xfered && commands==2);cases++;
 setup(1,true,true);status_busy=3;run();assert(!data.error && status_commands==4);cases++;
 setup(1,true,true);status_busy=10000;run();assert(data.error==-ETIMEDOUT && !data.bytes_xfered);cases++;
 setup(8,false,true);sg[7].len=1;run();assert(data.error==-EINVAL && data.bytes_xfered<4096);cases++;
 setup(8,true,true);sg[7].len=1;run();assert(cmd.error==-EINVAL && data.bytes_xfered<4096);cases++;
 setup(8,false,true);cmd.arg=U32_MAX-3;run();assert(data.error==-EINVAL && !commands);cases++;
 setup(8,false,true);data.flags=MMC_DATA_WRITE;run();assert(data.error==-EINVAL && !commands);cases++;
 setup(9,false,true);run();assert(data.error==-EINVAL && !commands);cases++;
 setup(8,false,true);sbc.arg|=BIT(31);run();assert(data.error==-EINVAL && !commands);cases++;
 setup(1,false,true);cmd.opcode=51;data.blksz=8;request.sbc=request.stop=NULL;run();
 assert(!cmd.error && !data.error && data.bytes_xfered==8 && commands==1);
 for(unsigned i=0;i<8;i++) assert(user[i]==0x53);
 cases++;
 printf("PASS driver request/IRQ functions: %u cases (SG, SDHC/SDSC, split, partial/error/timeout, SCR)\n",cases);
}
