#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" char* realpath(const char* path,char* resolved) {
  if(!path) { errno=EFAULT; return nullptr; }
  size_t size=strnlen(path,PATH_MAX);
  if(!size) { errno=ENOENT; return nullptr; }
  if(size==PATH_MAX) { errno=ENAMETOOLONG; return nullptr; }
  char source[PATH_MAX],result[PATH_MAX];
  memcpy(source,path,size+1);
  size_t used=1;
  result[0]='/'; result[1]=0;
  if(source[0]!='/') {
    if(!getcwd(result,sizeof result)) return nullptr;
    used=strlen(result);
  }
  for(size_t at=0;at<size;) {
    while(at<size && source[at]=='/') ++at;
    size_t begin=at;
    while(at<size && source[at]!='/') ++at;
    size_t length=at-begin;
    if(!length) break;
    if(length==1 && source[begin]=='.') {
      // A preceding component followed by '/' was checked as a directory.
    } else if(length==2 && source[begin]=='.' && source[begin+1]=='.') {
      while(used>1 && result[used-1]!='/') --used;
      if(used>1) --used;
      result[used]=0;
    } else {
      size_t separator=used>1 ? 1 : 0;
      if(length>sizeof result-used-separator-1) { errno=ENAMETOOLONG; return nullptr; }
      if(separator) result[used++]='/';
      memcpy(result+used,source+begin,length); used+=length; result[used]=0;
    }
    struct stat information{};
    if(stat(result,&information)) return nullptr;
    if(at<size && !S_ISDIR(information.st_mode)) { errno=ENOTDIR; return nullptr; }
  }
  struct stat information{};
  if(stat(result,&information)) return nullptr;
  char* output=resolved ? resolved : static_cast<char*>(malloc(PATH_MAX));
  if(!output) { errno=ENOMEM; return nullptr; }
  memcpy(output,result,used+1);
  return output;
}
