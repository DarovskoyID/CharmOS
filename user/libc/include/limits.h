#ifndef _CHARMOS_LIMITS_H
#define _CHARMOS_LIMITS_H
// Compiler arithmetic limits plus actual native filesystem/pipe boundaries.
#include_next <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#ifndef SSIZE_MAX
#define SSIZE_MAX __INT64_MAX__
#endif
#ifndef PIPE_BUF
#define PIPE_BUF 4096
#endif
#endif
