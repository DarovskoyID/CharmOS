#ifndef _DIRENT_H
#define _DIRENT_H
#include <stdint.h>
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
struct dirent {
  uint8_t d_type;
  char d_name[256];
};
typedef struct __dir DIR;
#ifdef __cplusplus
extern "C" {
#endif
DIR* opendir(const char* path);
DIR* fdopendir(int fd);
int dirfd(DIR* directory);
struct dirent* readdir(DIR* d);
int closedir(DIR* d);
#ifdef __cplusplus
}
#endif
#endif
