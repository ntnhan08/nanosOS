#pragma once
#include "../kernel/kernel.h"
typedef struct { uint32_t size; uint64_t created,modified; } nfs_stat_t;
k_err_t nfs_mount(void);
k_err_t nfs_format(void);
int     nfs_open(const char *path,uint8_t flags);
int32_t nfs_read(int fd,void *buf,uint32_t len);
int32_t nfs_write(int fd,const void *buf,uint32_t len);
int     nfs_close(int fd);
int32_t nfs_stat(const char *path,nfs_stat_t *st);
void    nfs_stats_print(void);
#define NFS_O_RDONLY 0x01
#define NFS_O_WRONLY 0x02
#define NFS_O_RDWR   0x03
#define NFS_O_CREAT  0x10
#define NFS_O_TRUNC  0x20
