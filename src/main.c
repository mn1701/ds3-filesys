#define _POSIX_C_SOURCE 200809L
#include "ds3.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int usage(const char *program) {
    fprintf(stderr,"Usage: %s format IMAGE BLOCKS\n",program);
    fprintf(stderr,"       %s info|check IMAGE\n",program);
    fprintf(stderr,"       %s ls|stat|cat|mkdir|touch|rm IMAGE /PATH\n",program);
    fprintf(stderr,"       %s put IMAGE /PATH LOCAL_FILE\n",program);
    fprintf(stderr,"       %s get IMAGE /PATH LOCAL_FILE\n",program);
    fprintf(stderr,"       %s mv|cp IMAGE /SOURCE /DESTINATION\n",program); return 2;
}
static int import_file(ds3_fs *fs,const char *path,const char *local) {
    FILE *f=fopen(local,"rb"); if (!f) return ds3_error("cannot open input file");
    size_t cap=DS3_DIRECT*DS3_BLOCK_SIZE; unsigned char *data=malloc(cap+1);
    if (!data) { fclose(f); return ds3_error("allocation failed"); }
    size_t size=fread(data,1,cap+1,f); int error=ferror(f); fclose(f);
    if (error || size>cap) { free(data); return ds3_error("input read failed or exceeds 128 KiB"); }
    int id=ds3_find(fs,path); if (id<0) id=ds3_create(fs,path,1);
    int result=id<0?-1:ds3_write(fs,(unsigned)id,data,size); free(data); return result;
}
int main(int argc,char **argv) {
    if (argc<3) return usage(argv[0]);
    const char *cmd=argv[1];
    if (!strcmp(cmd,"format") && argc==4) {
        char *end=NULL; errno=0; unsigned long count=strtoul(argv[3],&end,10);
        if (errno || !*argv[3] || *end || count>32768) return ds3_error("invalid block count")<0;
        return ds3_format(argv[2],(uint32_t)count)<0;
    }
    int arity=(!strcmp(cmd,"info") || !strcmp(cmd,"check"))?3:
        (!strcmp(cmd,"put") || !strcmp(cmd,"get") || !strcmp(cmd,"mv") || !strcmp(cmd,"cp"))?5:4;
    if (argc!=arity) return usage(argv[0]);
    ds3_fs fs; if (ds3_open(&fs,argv[2])<0) return 1;
    int result=0, mutate=0, id=argc>3?ds3_find(&fs,argv[3]):0;
    if (!strcmp(cmd,"info")) printf("DS3 v2: %u data blocks, %u inodes, generation %u\n",fs.count,DS3_INODES,fs.generation);
    else if (!strcmp(cmd,"check")) puts("Image checksum, directory graph, and block allocation are consistent.");
    else if (!strcmp(cmd,"mkdir") || !strcmp(cmd,"touch")) { result=ds3_create(&fs,argv[3],!strcmp(cmd,"mkdir")?2:1); mutate=1; }
    else if (!strcmp(cmd,"put")) { result=import_file(&fs,argv[3],argv[4]); mutate=1; }
    else if (id<0) result=ds3_error("path does not exist");
    else if (!strcmp(cmd,"ls")) {
        if (fs.nodes[id].kind!=2) result=ds3_error("path is not a directory");
        else for (unsigned n=1;n<DS3_INODES;n++) if (fs.nodes[n].kind && fs.nodes[n].parent==(unsigned)id) printf("%c\t%u\t%s\n",fs.nodes[n].kind==2?'d':'f',fs.nodes[n].size,fs.nodes[n].name);
    } else if (!strcmp(cmd,"stat")) printf("inode=%d kind=%u size=%u modified=%llu\n",id,fs.nodes[id].kind,fs.nodes[id].size,(unsigned long long)fs.nodes[id].modified);
    else if (!strcmp(cmd,"rm")) { result=ds3_remove(&fs,(unsigned)id); mutate=1; }
    else if (!strcmp(cmd,"mv")) { result=ds3_move(&fs,(unsigned)id,argv[4]); mutate=1; }
    else if (!strcmp(cmd,"cat") || !strcmp(cmd,"get") || !strcmp(cmd,"cp")) {
        unsigned char *data=NULL; size_t size=0; result=ds3_read(&fs,(unsigned)id,&data,&size);
        if (result==0 && !strcmp(cmd,"cp")) { int dest=ds3_create(&fs,argv[4],1); result=dest<0?-1:ds3_write(&fs,(unsigned)dest,data,size); mutate=1; }
        else if (result==0) {
            FILE *out=!strcmp(cmd,"cat")?stdout:fopen(argv[4],"wb");
            if (!out) result=ds3_error("cannot open output file");
            else { if (fwrite(data,1,size,out)!=size) result=ds3_error("output write failed"); if (out!=stdout && fclose(out)!=0) result=-1; }
        }
        free(data);
    } else { usage(argv[0]); result=-1; }
    if (result>=0 && mutate) result=ds3_commit(&fs);
    ds3_close(&fs); return result<0;
}
