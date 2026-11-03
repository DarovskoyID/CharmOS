#include "base/files/file_util.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

namespace base {
bool AllocateFileRegion(File* file,int64_t offset,size_t size) {
  if(!file || offset<0 || size>static_cast<uint64_t>(INT64_MAX-offset)) { errno=EINVAL; return false; }
  int64_t original=file->GetLength();
  if(original<0) return false;
  int64_t end=offset+static_cast<int64_t>(size);
  if(end>original && !file->SetLength(end)) return false;
  char bytes[4096];
  size_t completed=0;
  while(completed<size) {
    size_t amount=size-completed;
    if(amount>sizeof bytes) amount=sizeof bytes;
    size_t received=0;
    while(received<amount) {
      ssize_t count=pread(file->GetPlatformFile(),bytes+received,amount-received,
                           offset+static_cast<int64_t>(completed+received));
      if(count<0 && errno==EINTR) continue;
      if(count<=0) { if(!count) errno=EIO; return false; }
      received+=static_cast<size_t>(count);
    }
    size_t written=0;
    while(written<amount) {
      ssize_t count=pwrite(file->GetPlatformFile(),bytes+written,amount-written,
                            offset+static_cast<int64_t>(completed+written));
      if(count<0 && errno==EINTR) continue;
      if(count<=0) { if(!count) errno=EIO; return false; }
      written+=static_cast<size_t>(count);
    }
    completed+=amount;
  }
  return true;
}
}  // namespace base
