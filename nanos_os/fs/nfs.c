/* NanosOS fs/nfs.c - NanosFS: wear-leveling journaling filesystem */
#include "../kernel/kernel.h"
#include "nfs.h"
#define NFS_FLASH_BASE  0x00200000UL
#define NFS_FLASH_SZ    (8*1024*1024)
#define NFS_BLK_SZ      4096U
#define NFS_BLKS        (NFS_FLASH_SZ/NFS_BLK_SZ)
#define NFS_MAGIC       0x4E414E4FU
#define NFS_MAX_INODES  512U
#define NFS_NAME_MAX    60U
#define NFS_DIRECT_BLKS 12U
#define NFS_JRNL_OFFSET (1*NFS_BLK_SZ)
#define NFS_JRNL_BLKS   16
#define NFS_INODE_OFF   ((1+NFS_JRNL_BLKS)*NFS_BLK_SZ)
#define NFS_INODE_BLKS  64
#define NFS_DATA_OFF    ((1+NFS_JRNL_BLKS+NFS_INODE_BLKS)*NFS_BLK_SZ)
#define NFS_DATA_BLKS   (NFS_BLKS-1-NFS_JRNL_BLKS-NFS_INODE_BLKS)
#define NFS_MAX_FDS     16
#define CACHE_N         8
extern k_err_t flash_read(uint32_t a,void*,uint32_t);
extern k_err_t flash_write(uint32_t a,const void*,uint32_t);
extern k_err_t flash_erase_sector(uint32_t a);
typedef struct __attribute__((packed)){uint32_t magic,inum,mode,sz;uint64_t created,modified;
    uint32_t db[NFS_DIRECT_BLKS],ib,refc,crc;char name[NFS_NAME_MAX];} inode_t;
typedef struct __attribute__((packed)){uint32_t magic;uint8_t op;uint32_t inum,blk,off,len,crc;uint8_t data[NFS_BLK_SZ];} jentry_t;
typedef struct __attribute__((packed)){uint32_t magic,ver,blksz,tblks,tinodes,fblks,finodes,joff,jsz,ioff,doff;uint64_t mtime,wtime;uint32_t mnt,crc;
    uint8_t blkbmp[(NFS_DATA_BLKS+7)/8]; uint8_t inobmp[(NFS_MAX_INODES+7)/8];} sb_t;
typedef struct {bool open; inode_t ino; uint32_t pos; uint8_t flags;} fd_t;
typedef struct {uint32_t blk; bool dirty,valid; uint64_t lu; uint8_t *buf;} ce_t;
static sb_t  s_sb; static bool s_mounted;
static fd_t  s_fds[NFS_MAX_FDS];
static ce_t  s_cache[CACHE_N]; static uint8_t *s_cbufs;
static uint16_t *s_erase_cnt;
static k_mutex_t s_lk;
static uint32_t  s_reads,s_writes,s_erases,s_hits,s_miss;
static uint32_t crc32f(const uint8_t *d,uint32_t n){uint32_t c=0;while(n--){c^=*d++;for(int i=0;i<8;i++)c=(c>>1)^(0xEDB88320&-(c&1));}return c;}
static uint32_t b2a(uint32_t b){return NFS_FLASH_BASE+b*NFS_BLK_SZ;}
static ce_t *cfind(uint32_t b){for(int i=0;i<CACHE_N;i++)if(s_cache[i].valid&&s_cache[i].blk==b)return &s_cache[i];return NULL;}
static ce_t *cevict(void){int best=0;uint64_t ot=UINT64_MAX;
    for(int i=0;i<CACHE_N;i++){if(!s_cache[i].valid)return &s_cache[i];
        if(s_cache[i].lu<ot){ot=s_cache[i].lu;best=i;}}
    ce_t *e=&s_cache[best];
    if(e->dirty){flash_erase_sector(b2a(e->blk));flash_write(b2a(e->blk),e->buf,NFS_BLK_SZ);e->dirty=false;s_erases++;}
    return e;}
static k_err_t rblk(uint32_t b,uint8_t *buf){
    k_mutex_lock(&s_lk,K_FOREVER);
    ce_t *e=cfind(b);
    if(e){s_hits++;memcpy(buf,e->buf,NFS_BLK_SZ);e->lu=k_time_ms();k_mutex_unlock(&s_lk);return K_OK;}
    s_miss++; e=cevict();
    k_err_t r=flash_read(b2a(b),e->buf,NFS_BLK_SZ);
    if(r==K_OK){e->blk=b;e->valid=true;e->dirty=false;e->lu=k_time_ms();memcpy(buf,e->buf,NFS_BLK_SZ);}
    k_mutex_unlock(&s_lk); s_reads++; return r;}
static k_err_t wblk(uint32_t b,const uint8_t *buf){
    k_mutex_lock(&s_lk,K_FOREVER);
    ce_t *e=cfind(b); if(!e){e=cevict();e->blk=b;e->valid=true;}
    memcpy(e->buf,buf,NFS_BLK_SZ); e->dirty=true; e->lu=k_time_ms();
    s_writes++; k_mutex_unlock(&s_lk); return K_OK;}
static int32_t alloc_blk(void){
    for(uint32_t i=0;i<NFS_DATA_BLKS;i++){uint32_t by=i/8,bi=i%8;
        if(!(s_sb.blkbmp[by]&BIT(bi))){
            s_sb.blkbmp[by]|=BIT(bi);s_sb.fblks--;if(s_erase_cnt)s_erase_cnt[i]++;
            return (int32_t)(NFS_DATA_OFF/NFS_BLK_SZ+i);}}return -1;}
k_err_t nfs_mount(void){
    k_mutex_init(&s_lk,"nfs");
    s_cbufs=(uint8_t*)k_psram_alloc(CACHE_N*NFS_BLK_SZ);
    for(int i=0;i<CACHE_N;i++){s_cache[i].buf=s_cbufs+i*NFS_BLK_SZ;s_cache[i].valid=false;}
    s_erase_cnt=(uint16_t*)k_psram_alloc(NFS_DATA_BLKS*2);
    k_err_t r=flash_read(NFS_FLASH_BASE,&s_sb,sizeof(s_sb));
    if(r!=K_OK||s_sb.magic!=NFS_MAGIC)return nfs_format();
    s_mounted=true; s_sb.mnt++;
    KLOGI("NFS","Mounted: %u blks %u free",s_sb.tblks,s_sb.fblks); return K_OK;}
k_err_t nfs_format(void){
    KLOGI("NFS","Formatting...");
    for(uint32_t i=0;i<NFS_BLKS;i++){flash_erase_sector(NFS_FLASH_BASE+i*NFS_BLK_SZ);if(i%64==0)KLOGI("NFS","%u%%",i*100/NFS_BLKS);}
    memset(&s_sb,0,sizeof(s_sb));
    s_sb.magic=NFS_MAGIC; s_sb.ver=1; s_sb.blksz=NFS_BLK_SZ;
    s_sb.tblks=NFS_DATA_BLKS; s_sb.tinodes=NFS_MAX_INODES;
    s_sb.fblks=NFS_DATA_BLKS; s_sb.finodes=NFS_MAX_INODES;
    s_sb.joff=NFS_JRNL_OFFSET; s_sb.jsz=NFS_JRNL_BLKS*NFS_BLK_SZ;
    s_sb.ioff=NFS_INODE_OFF; s_sb.doff=NFS_DATA_OFF;
    s_sb.mtime=k_time_ms();
    flash_write(NFS_FLASH_BASE,&s_sb,sizeof(s_sb));
    s_mounted=true; KLOGI("NFS","Format done"); return K_OK;}
static uint32_t iaddr(uint32_t inum){return NFS_FLASH_BASE+NFS_INODE_OFF+inum*sizeof(inode_t);}
int nfs_open(const char *path,uint8_t flags){
    if(!s_mounted||!path)return -1;
    k_mutex_lock(&s_lk,K_FOREVER);
    int fd=-1; for(int i=0;i<NFS_MAX_FDS;i++)if(!s_fds[i].open){fd=i;break;}
    if(fd<0){k_mutex_unlock(&s_lk);return -1;}
    for(uint32_t i=0;i<NFS_MAX_INODES;i++){
        if(!(s_sb.inobmp[i/8]&BIT(i%8)))continue;
        inode_t ino; flash_read(iaddr(i),&ino,sizeof(ino));
        if(strncmp(ino.name,path,NFS_NAME_MAX)==0){
            s_fds[fd].ino=ino; s_fds[fd].pos=0; s_fds[fd].flags=flags; s_fds[fd].open=true;
            k_mutex_unlock(&s_lk); return fd;}}
    if(!(flags&NFS_O_CREAT)){k_mutex_unlock(&s_lk);return -1;}
    int32_t inum=-1;
    for(uint32_t i=0;i<NFS_MAX_INODES;i++)if(!(s_sb.inobmp[i/8]&BIT(i%8))){
        s_sb.inobmp[i/8]|=BIT(i%8);s_sb.finodes--;inum=(int32_t)i;break;}
    if(inum<0){k_mutex_unlock(&s_lk);return -1;}
    inode_t *ino=&s_fds[fd].ino; memset(ino,0,sizeof(*ino));
    ino->magic=0x4E464900; ino->inum=(uint32_t)inum; ino->mode=0x8000;
    ino->created=ino->modified=k_time_ms(); strncpy(ino->name,path,NFS_NAME_MAX-1);
    flash_write(iaddr((uint32_t)inum),ino,sizeof(*ino));
    s_fds[fd].pos=0; s_fds[fd].flags=flags; s_fds[fd].open=true;
    k_mutex_unlock(&s_lk); return fd;}
int32_t nfs_read(int fd,void *buf,uint32_t len){
    if(fd<0||fd>=NFS_MAX_FDS||!s_fds[fd].open)return -1;
    fd_t *f=&s_fds[fd]; uint32_t rem=f->ino.sz-f->pos; if(len>rem)len=rem; if(!len)return 0;
    uint32_t rd=0; uint8_t blkbuf[NFS_BLK_SZ];
    while(rd<len){uint32_t bi=(f->pos+rd)/NFS_BLK_SZ,bo=(f->pos+rd)%NFS_BLK_SZ;
        uint32_t ch=MIN(len-rd,NFS_BLK_SZ-bo);
        if(bi>=NFS_DIRECT_BLKS||!f->ino.db[bi])break;
        rblk(f->ino.db[bi],blkbuf); memcpy((uint8_t*)buf+rd,blkbuf+bo,ch); rd+=ch;}
    f->pos+=rd; return (int32_t)rd;}
int32_t nfs_write(int fd,const void *buf,uint32_t len){
    if(fd<0||fd>=NFS_MAX_FDS||!s_fds[fd].open)return -1;
    fd_t *f=&s_fds[fd]; uint32_t wr=0; uint8_t blkbuf[NFS_BLK_SZ];
    while(wr<len){uint32_t bi=(f->pos+wr)/NFS_BLK_SZ,bo=(f->pos+wr)%NFS_BLK_SZ;
        uint32_t ch=MIN(len-wr,NFS_BLK_SZ-bo);
        if(bi>=NFS_DIRECT_BLKS)break;
        if(!f->ino.db[bi]){int32_t nb=alloc_blk();if(nb<0)break;f->ino.db[bi]=(uint32_t)nb;memset(blkbuf,0xFF,NFS_BLK_SZ);}
        else rblk(f->ino.db[bi],blkbuf);
        memcpy(blkbuf+bo,(const uint8_t*)buf+wr,ch); wblk(f->ino.db[bi],blkbuf); wr+=ch;}
    f->pos+=wr; if(f->pos>f->ino.sz)f->ino.sz=f->pos;
    f->ino.modified=k_time_ms(); flash_write(iaddr(f->ino.inum),&f->ino,sizeof(f->ino));
    return (int32_t)wr;}
int nfs_close(int fd){if(fd<0||fd>=NFS_MAX_FDS)return -1;s_fds[fd].open=false;return 0;}
int32_t nfs_stat(const char *p,nfs_stat_t *st){
    for(uint32_t i=0;i<NFS_MAX_INODES;i++){if(!(s_sb.inobmp[i/8]&BIT(i%8)))continue;
        inode_t ino; flash_read(iaddr(i),&ino,sizeof(ino));
        if(strncmp(ino.name,p,NFS_NAME_MAX)==0){st->size=ino.sz;st->created=ino.created;st->modified=ino.modified;return 0;}}
    return -1;}
void nfs_stats_print(void){
    KLOGI("NFS","reads=%u writes=%u erases=%u hits=%u miss=%u fblks=%u",
          s_reads,s_writes,s_erases,s_hits,s_miss,s_sb.fblks);}
