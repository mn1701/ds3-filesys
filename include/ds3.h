#ifndef DS3_H
#define DS3_H
#include <stdint.h>
#include <stddef.h>
#define DS3_BLOCK_SIZE 4096u
#define DS3_INODES 128u
#define DS3_DIRECT 32u
#define DS3_NAME 96u
#define DS3_INODE_BYTES 256u
#define DS3_META (2 * DS3_BLOCK_SIZE + DS3_INODES * DS3_INODE_BYTES)
typedef struct {
    uint32_t kind, parent, size;
    uint64_t modified;
    char name[DS3_NAME];
    uint32_t blocks[DS3_DIRECT];
} ds3_inode;
typedef struct {
    unsigned char *bytes;
    size_t length;
    uint32_t count, generation;
    ds3_inode nodes[DS3_INODES];
    const char *path;
    int lock;
} ds3_fs;
uint32_t ds3_u32(const unsigned char *p);
void ds3_put32(unsigned char *p, uint32_t value);
uint32_t ds3_checksum(const unsigned char *p, size_t length);
int ds3_open(ds3_fs *fs, const char *path);
void ds3_close(ds3_fs *fs);
int ds3_commit(ds3_fs *fs);
int ds3_format(const char *path, uint32_t blocks);
int ds3_check(ds3_fs *fs);
int ds3_find(ds3_fs *fs, const char *path);
int ds3_create(ds3_fs *fs, const char *path, uint32_t kind);
int ds3_write(ds3_fs *fs, unsigned id, const unsigned char *data, size_t size);
int ds3_read(ds3_fs *fs, unsigned id, unsigned char **data, size_t *size);
int ds3_remove(ds3_fs *fs, unsigned id);
int ds3_move(ds3_fs *fs, unsigned id, const char *destination);
int ds3_error(const char *message);
#endif
