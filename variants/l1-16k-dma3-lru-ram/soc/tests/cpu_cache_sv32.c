// Reuse the complete storage/IRQ firmware after exercising translated aliases.
// Production RTL and the existing physical-address regression are unchanged.
#define main storage_main
#include "cpu_storage.c"
#undef main

#define ROOT 0x80040000u
#define LEAF 0x80041000u
#define ALIAS0 0x40000000u
#define ALIAS1 0x40001000u
#define PAGE0 0x80030000u
#define PAGE1 0x80031000u
#define COPY 0x80033000u
static u32 pte(u32 physical,u32 flags){return (physical>>12<<10)|flags;}
static void flush_tlb(void){__asm__ volatile("sfence.vma zero,zero":::"memory");}
static void set_status(u32 value){__asm__ volatile("csrw mstatus,%0"::"r"(value):"memory");}
static void dma_wait(u32 reg,u32 code){
 for(u32 n=0;n<10000;n++)if(R(reg+20)&2){check((R(reg+20)&6)==2,code);R(reg+20)=2;fence();return;}
 fail(code+1);
}
int main(void){
 u32 original;__asm__ volatile("csrr %0,mstatus":"=r"(original));
 for(u32 n=0;n<1024;n++){R(ROOT+4*n)=0;R(LEAF+4*n)=0;}
 // Identity superpages keep firmware, stack, PLIC and MMIO usable in S mode.
 R(ROOT+4*(0x80000000u>>22))=pte(0x80000000u,0xcf);
 R(ROOT+4*(0x10000000u>>22))=pte(0x10000000u,0xcf);
 R(ROOT+4*(0x0c000000u>>22))=pte(0x0c000000u,0xcf);
 R(ROOT+4*(0xbfc00000u>>22))=pte(0xbfc00000u,0xcf);
 R(ROOT+4*(ALIAS0>>22))=pte(LEAF,1);
 R(LEAF)=pte(PAGE0,0xc7);R(LEAF+4)=pte(PAGE0,0xc7);
 for(u32 n=0;n<8;n++){R(PAGE0+4*n)=0x12340000+n;R(PAGE1+4*n)=0x56780000+n;R(COPY+4*n)=0;}
 fence();__asm__ volatile("csrw satp,%0"::"r"(0x80000000u|(ROOT>>12)):"memory");flush_tlb();
 u32 translated=(original&~0x1800u)|0x20800u; // MPRV=1, MPP=S.
 set_status(translated);
 for(u32 n=0;n<8;n++)check(R(ALIAS0+4*n)==R(ALIAS1+4*n),40);
 R(ALIAS1+12)=0xa1b2c3d4;fence();check(R(ALIAS0+12)==0xa1b2c3d4,41);
 set_status(original);
 // Change a cached page-table entry, then invalidate the original TLB mapping.
 R(LEAF+4)=pte(PAGE1,0xc7);fence();flush_tlb();set_status(translated);
 check(R(ALIAS1)==0x56780000 && R(ALIAS0)==0x12340000,42);
 set_status(original);
 // DMA1 overwrites a line already read through two translated virtual aliases.
 u32 reg=DMA+0x1000;R(reg+4)=PAGE0;R(reg+8)=32;R(reg+12)=0x89abcdef;fence();R(reg+16)=2;
 dma_wait(reg,43);set_status(translated);
 for(u32 n=0;n<8;n++)check(R(ALIAS0+4*n)==0x89abcdef,45);
 check(R(ALIAS1)==0x56780000,46);
 R(ALIAS0)=0x10293847;fence();set_status(original);
 // CPU's translated store must be visible to DMA2's physical read.
 for(u32 n=0;n<8;n++)check(R(COPY+4*n)==0,47);
 reg=DMA+0x2000;R(reg)=PAGE0;R(reg+4)=COPY;R(reg+8)=32;fence();R(reg+16)=1;
 dma_wait(reg,48);check(R(COPY)==0x10293847,50);
 for(u32 n=1;n<8;n++)check(R(COPY+4*n)==0x89abcdef,51);
 R(0x8007ff04)=0x600d1632;fence();
 // satp stays enabled. The inherited supervisor IRQ tests now execute under Sv32.
 return storage_main();
}
