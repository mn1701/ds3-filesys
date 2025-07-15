#include "ds3.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void mark(ds3_fs *fs,unsigned block,int used) {
    unsigned char *byte=&fs->bytes[DS3_BLOCK_SIZE+block/8];
    if (used) *byte|=(unsigned char)(1u<<(block%8)); else *byte&=(unsigned char)~(1u<<(block%8));
}
static int used(ds3_fs *fs,unsigned b) { return !!(fs->bytes[DS3_BLOCK_SIZE+b/8]&(1u<<(b%8))); }
static void release(ds3_fs *fs,ds3_inode *node) {
    for (unsigned n=0;n<DS3_DIRECT;n++) if (node->blocks[n]) {
        unsigned b=node->blocks[n]-1; mark(fs,b,0);
        memset(fs->bytes+DS3_META+(size_t)b*DS3_BLOCK_SIZE,0,DS3_BLOCK_SIZE); node->blocks[n]=0;
    }
    node->size=0;
}
int ds3_write(ds3_fs *fs,unsigned id,const unsigned char *data,size_t size) {
    if (id>=DS3_INODES || fs->nodes[id].kind!=1 || size>DS3_DIRECT*DS3_BLOCK_SIZE) return ds3_error("invalid file or file exceeds 128 KiB");
    unsigned need=(unsigned)((size+DS3_BLOCK_SIZE-1)/DS3_BLOCK_SIZE), available=0;
    ds3_inode *node=&fs->nodes[id];
    for (unsigned b=0;b<fs->count;b++) if (!used(fs,b)) available++;
    for (unsigned b=0;b<DS3_DIRECT;b++) if (node->blocks[b]) available++;
    if (available<need) return ds3_error("not enough free blocks");
    release(fs,node);
    size_t offset=0; unsigned entry=0;
    for (unsigned b=0;entry<need && b<fs->count;b++) if (!used(fs,b)) {
        mark(fs,b,1); node->blocks[entry++]=b+1;
        size_t count=size-offset; if (count>DS3_BLOCK_SIZE) count=DS3_BLOCK_SIZE;
        memcpy(fs->bytes+DS3_META+(size_t)b*DS3_BLOCK_SIZE,data+offset,count); offset+=count;
    }
    node->size=(uint32_t)size; node->modified=(uint64_t)time(NULL); return 0;
}
int ds3_read(ds3_fs *fs,unsigned id,unsigned char **data,size_t *size) {
    if (id>=DS3_INODES || fs->nodes[id].kind!=1) return ds3_error("path is not a regular file");
    ds3_inode *node=&fs->nodes[id]; *size=node->size; *data=malloc(*size?*size:1);
    if (!*data) return ds3_error("allocation failed");
    size_t offset=0;
    for (unsigned n=0;offset<*size;n++) {
        size_t count=*size-offset; if (count>DS3_BLOCK_SIZE) count=DS3_BLOCK_SIZE;
        memcpy(*data+offset,fs->bytes+DS3_META+(size_t)(node->blocks[n]-1)*DS3_BLOCK_SIZE,count); offset+=count;
    }
    return 0;
}
int ds3_remove(ds3_fs *fs,unsigned id) {
    if (!id || id>=DS3_INODES || !fs->nodes[id].kind) return ds3_error("cannot remove root or missing inode");
    for (unsigned n=1;n<DS3_INODES;n++) if (fs->nodes[n].kind && fs->nodes[n].parent==id) return ds3_error("directory is not empty");
    release(fs,&fs->nodes[id]); memset(&fs->nodes[id],0,sizeof(ds3_inode)); return 0;
}
