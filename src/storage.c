#define _POSIX_C_SOURCE 200809L
#include "ds3.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static void decode(ds3_fs *fs) {
    for (unsigned i=0;i<DS3_INODES;i++) {
        const unsigned char *p=fs->bytes+2*DS3_BLOCK_SIZE+i*DS3_INODE_BYTES;
        ds3_inode *n=&fs->nodes[i];
        n->kind=ds3_u32(p); n->parent=ds3_u32(p+4); n->size=ds3_u32(p+8);
        n->modified=ds3_u32(p+12)|((uint64_t)ds3_u32(p+16)<<32);
        memcpy(n->name,p+20,DS3_NAME);
        for (unsigned b=0;b<DS3_DIRECT;b++) n->blocks[b]=ds3_u32(p+116+b*4);
    }
}
static void encode(ds3_fs *fs) {
    for (unsigned i=0;i<DS3_INODES;i++) {
        unsigned char *p=fs->bytes+2*DS3_BLOCK_SIZE+i*DS3_INODE_BYTES;
        ds3_inode *n=&fs->nodes[i];
        memset(p,0,DS3_INODE_BYTES);
        ds3_put32(p,n->kind); ds3_put32(p+4,n->parent); ds3_put32(p+8,n->size);
        ds3_put32(p+12,(uint32_t)n->modified); ds3_put32(p+16,(uint32_t)(n->modified>>32));
        memcpy(p+20,n->name,DS3_NAME);
        for (unsigned b=0;b<DS3_DIRECT;b++) ds3_put32(p+116+b*4,n->blocks[b]);
    }
}
static int acquire(ds3_fs *fs, const char *path) {
    memset(fs,0,sizeof(*fs)); fs->lock=-1; fs->path=path;
    size_t len=strlen(path); char *lock=malloc(len+6);
    if (!lock) return ds3_error("allocation failed");
    snprintf(lock,len+6,"%s.lock",path);
    fs->lock=open(lock,O_CREAT|O_RDWR,0600); free(lock);
    if (fs->lock<0 || flock(fs->lock,LOCK_EX)<0) { ds3_close(fs); return ds3_error("cannot lock image"); }
    return 0;
}
void ds3_close(ds3_fs *fs) {
    free(fs->bytes); fs->bytes=NULL;
    if (fs->lock>=0) { flock(fs->lock,LOCK_UN); close(fs->lock); fs->lock=-1; }
}
int ds3_open(ds3_fs *fs,const char *path) {
    if (acquire(fs,path)<0) return -1;
    FILE *f=fopen(path,"rb"); struct stat st;
    if (!f) { ds3_close(fs); return ds3_error("cannot open image"); }
    if (fstat(fileno(f),&st)<0 || st.st_size<(off_t)(DS3_META+8*DS3_BLOCK_SIZE) || st.st_size>(off_t)(DS3_META+32768u*DS3_BLOCK_SIZE)) goto invalid;
    fs->length=(size_t)st.st_size; fs->bytes=malloc(fs->length);
    if (!fs->bytes || fread(fs->bytes,1,fs->length,f)!=fs->length) goto invalid;
    if (memcmp(fs->bytes,"DS3IMG02",8) || ds3_u32(fs->bytes+8)!=2 || ds3_u32(fs->bytes+12)!=DS3_BLOCK_SIZE) goto invalid;
    fs->count=ds3_u32(fs->bytes+16); fs->generation=ds3_u32(fs->bytes+20);
    if (fs->count<8 || fs->count>32768 || fs->length!=DS3_META+(size_t)fs->count*DS3_BLOCK_SIZE) goto invalid;
    uint32_t crc=ds3_u32(fs->bytes+24); ds3_put32(fs->bytes+24,0);
    if (crc!=ds3_checksum(fs->bytes,fs->length)) goto invalid;
    decode(fs); fclose(f);
    if (ds3_check(fs)<0) { ds3_close(fs); return -1; }
    return 0;
invalid:
    fclose(f); ds3_close(fs); return ds3_error("invalid, corrupt, or unsupported image");
}
int ds3_commit(ds3_fs *fs) {
    if (ds3_check(fs)<0) return -1;
    encode(fs); ds3_put32(fs->bytes+20,fs->generation+1); ds3_put32(fs->bytes+24,0);
    ds3_put32(fs->bytes+24,ds3_checksum(fs->bytes,fs->length));
    size_t len=strlen(fs->path); char *tmp=malloc(len+12);
    if (!tmp) return ds3_error("allocation failed");
    snprintf(tmp,len+12,"%s.XXXXXX",fs->path);
    int fd=mkstemp(tmp), ok=fd>=0; size_t written=0;
    while (ok && written<fs->length) {
        ssize_t n=write(fd,fs->bytes+written,fs->length-written);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) ok=0; else written+=(size_t)n;
    }
    if (fd>=0) { if (fsync(fd)<0) ok=0; if (close(fd)<0) ok=0; }
    if (ok && rename(tmp,fs->path)<0) ok=0;
    if (!ok) unlink(tmp);
    free(tmp);
    if (!ok) return ds3_error("image transaction could not be committed");
    fs->generation++;
    /* Directory fsync strengthens rename durability where the host supports it. */
    char *parent=strdup(fs->path);
    if (parent) {
        char *slash=strrchr(parent,'/');
        if (slash) { if (slash==parent) slash[1]='\0'; else *slash='\0'; }
        int directory=open(slash?parent:".",O_RDONLY);
        if (directory>=0) { if (fsync(directory)<0) fprintf(stderr,"ds3: directory sync unsupported or failed\n"); close(directory); }
        free(parent);
    }
    return 0;
}
int ds3_format(const char *path,uint32_t blocks) {
    if (blocks<8 || blocks>32768) return ds3_error("block count must be 8..32768");
    ds3_fs fs; if (acquire(&fs,path)<0) return -1;
    struct stat st;
    if (lstat(path,&st)==0 || errno!=ENOENT) { ds3_close(&fs); return ds3_error("image path must not already exist"); }
    fs.count=blocks; fs.length=DS3_META+(size_t)blocks*DS3_BLOCK_SIZE;
    fs.bytes=calloc(1,fs.length);
    if (!fs.bytes) { ds3_close(&fs); return ds3_error("allocation failed"); }
    memcpy(fs.bytes,"DS3IMG02",8); ds3_put32(fs.bytes+8,2); ds3_put32(fs.bytes+12,DS3_BLOCK_SIZE); ds3_put32(fs.bytes+16,blocks);
    fs.nodes[0].kind=2; strcpy(fs.nodes[0].name,"/");
    int result=ds3_commit(&fs); ds3_close(&fs); return result;
}
