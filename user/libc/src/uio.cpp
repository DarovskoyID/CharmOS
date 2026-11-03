#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

static_assert(sizeof(iovec)==16 && __builtin_offsetof(iovec,iov_len)==8);
namespace {
ssize_t transfer(int fd,const iovec* vectors,int count,bool writing) {
  if (count<0 || count>IOV_MAX) { errno=EINVAL; return -1; }
  if (count && !vectors) { errno=EFAULT; return -1; }
  size_t total=0;
  for (int i=0;i<count;++i) {
    if (vectors[i].iov_len>static_cast<size_t>(INT64_MAX)-total) { errno=EINVAL; return -1; }
    if (vectors[i].iov_len && !vectors[i].iov_base) { errno=EFAULT; return -1; }
    total+=vectors[i].iov_len;
  }
  unsigned char local[4096];
  auto* staging=total<=sizeof local ? local : static_cast<unsigned char*>(malloc(total));
  if (!staging) { errno=ENOMEM; return -1; }
  if (writing) {
    size_t offset=0;
    for (int i=0;i<count;++i) {
      if (vectors[i].iov_len) memcpy(staging+offset,vectors[i].iov_base,vectors[i].iov_len);
      offset+=vectors[i].iov_len;
    }
  }
  ssize_t done=writing ? write(fd,staging,total) : read(fd,staging,total);
  if (!writing && done>0) {
    size_t offset=0;
    for (int i=0;i<count && offset<static_cast<size_t>(done);++i) {
      size_t amount=vectors[i].iov_len;
      if (amount>static_cast<size_t>(done)-offset) amount=static_cast<size_t>(done)-offset;
      if (amount) memcpy(vectors[i].iov_base,staging+offset,amount);
      offset+=amount;
    }
  }
  if (staging!=local) free(staging);
  return done;
}
}
extern "C" ssize_t readv(int fd,const iovec* vectors,int count) { return transfer(fd,vectors,count,false); }
extern "C" ssize_t writev(int fd,const iovec* vectors,int count) { return transfer(fd,vectors,count,true); }
