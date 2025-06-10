#include "ds3.h"
#include <stdio.h>
uint32_t ds3_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
void ds3_put32(unsigned char *p, uint32_t value) {
    for (unsigned n=0;n<4;n++) p[n]=(unsigned char)(value>>(n*8));
}
uint32_t ds3_checksum(const unsigned char *p, size_t length) {
    uint32_t crc=0xffffffffu;
    for (size_t n=0;n<length;n++) {
        crc^=p[n];
        for (unsigned bit=0;bit<8;bit++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));
    }
    return ~crc;
}
int ds3_error(const char *message) { fprintf(stderr,"ds3: %s\n",message); return -1; }
