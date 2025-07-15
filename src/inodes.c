#define _POSIX_C_SOURCE 200809L
#include "ds3.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int valid_name(const char *name) {
    return *name && strlen(name)<DS3_NAME && !strchr(name,'/') && strcmp(name,".") && strcmp(name,"..");
}
static int child(ds3_fs *fs,unsigned parent,const char *name) {
    for (unsigned i=1;i<DS3_INODES;i++) if (fs->nodes[i].kind && fs->nodes[i].parent==parent && !strcmp(fs->nodes[i].name,name)) return (int)i;
    return -1;
}
int ds3_find(ds3_fs *fs,const char *path) {
    if (!path || *path!='/' || strlen(path)>4096) return ds3_error("use an absolute image path up to 4096 bytes");
    char *copy=strdup(path); if (!copy) return ds3_error("allocation failed");
    unsigned id=0; char *save=NULL; char *part=strtok_r(copy,"/",&save);
    while (part) {
        if (!valid_name(part) || fs->nodes[id].kind!=2) { free(copy); return -1; }
        int next=child(fs,id,part); if (next<0) { free(copy); return -1; }
        id=(unsigned)next; part=strtok_r(NULL,"/",&save);
    }
    free(copy); return (int)id;
}
static int destination(ds3_fs *fs,const char *path,char name[DS3_NAME]) {
    if (!path || strlen(path)>4096 || *path!='/') return ds3_error("invalid destination path");
    char *copy=strdup(path); if (!copy) return ds3_error("allocation failed");
    char *slash=strrchr(copy,'/');
    if (!slash || !valid_name(slash+1)) { free(copy); return ds3_error("invalid file name"); }
    strcpy(name,slash+1); *slash='\0';
    int parent=ds3_find(fs,*copy?copy:"/"); free(copy);
    if (parent<0 || fs->nodes[parent].kind!=2) return ds3_error("parent directory does not exist");
    if (child(fs,(unsigned)parent,name)>=0) return ds3_error("destination already exists");
    return parent;
}
int ds3_create(ds3_fs *fs,const char *path,uint32_t kind) {
    char name[DS3_NAME]; int parent=destination(fs,path,name);
    if (parent<0) return -1;
    for (unsigned i=1;i<DS3_INODES;i++) if (!fs->nodes[i].kind) {
        ds3_inode *n=&fs->nodes[i]; memset(n,0,sizeof(*n)); n->kind=kind;
        n->parent=(unsigned)parent; strcpy(n->name,name); n->modified=(uint64_t)time(NULL); return (int)i;
    }
    return ds3_error("inode table is full");
}
int ds3_move(ds3_fs *fs,unsigned id,const char *path) {
    if (!id || id>=DS3_INODES || !fs->nodes[id].kind) return ds3_error("cannot move root or missing inode");
    char name[DS3_NAME]; int parent=destination(fs,path,name); if (parent<0) return -1;
    unsigned ancestor=(unsigned)parent;
    for (unsigned steps=0;steps<DS3_INODES;steps++) {
        if (ancestor==id) return ds3_error("cannot move a directory into itself");
        if (!ancestor) break;
        ancestor=fs->nodes[ancestor].parent;
    }
    fs->nodes[id].parent=(unsigned)parent; strcpy(fs->nodes[id].name,name);
    fs->nodes[id].modified=(uint64_t)time(NULL); return 0;
}
int ds3_check(ds3_fs *fs) {
    unsigned char *seen=calloc(fs->count,1); if (!seen) return ds3_error("allocation failed");
    int ok=fs->nodes[0].kind==2 && fs->nodes[0].parent==0 && !memcmp(fs->nodes[0].name,"/",2);
    for (unsigned i=0;ok && i<DS3_INODES;i++) {
        ds3_inode *n=&fs->nodes[i]; if (!n->kind) continue;
        if (n->kind>2 || !memchr(n->name,0,DS3_NAME) || n->parent>=DS3_INODES || fs->nodes[n->parent].kind!=2 || (i && !valid_name(n->name))) { ok=0; break; }
        if (n->size>DS3_DIRECT*DS3_BLOCK_SIZE || (n->kind==2 && n->size)) { ok=0; break; }
        for (unsigned j=i+1;j<DS3_INODES;j++) if (fs->nodes[j].kind && n->parent==fs->nodes[j].parent && !strncmp(n->name,fs->nodes[j].name,DS3_NAME)) ok=0;
        unsigned parent=i;
        for (unsigned depth=0;parent;depth++) {
            if (depth>=DS3_INODES || parent>=DS3_INODES || !fs->nodes[parent].kind) { ok=0; break; }
            parent=fs->nodes[parent].parent;
        }
        unsigned needed=(n->size+DS3_BLOCK_SIZE-1)/DS3_BLOCK_SIZE;
        for (unsigned b=0;b<DS3_DIRECT;b++) {
            unsigned ref=n->blocks[b];
            if (b<needed) {
                if (!ref || ref>fs->count || seen[ref-1]) { ok=0; break; }
                seen[ref-1]=1;
            } else if (ref) ok=0;
        }
    }
    for (unsigned b=0;ok && b<fs->count;b++) if (!!(fs->bytes[DS3_BLOCK_SIZE+b/8]&(1u<<(b%8)))!=seen[b]) ok=0;
    free(seen); return ok?0:ds3_error("inode, directory, or allocation consistency failure");
}
