#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <libgen.h>
#include <string.h>

namespace {
char dot_path[]=".";
int character_class(const char* name,size_t size,unsigned char value) {
#define CLASS(n,f) if(size==sizeof(n)-1 && !memcmp(name,n,size)) return f(value)!=0
  CLASS("alnum",isalnum); CLASS("alpha",isalpha); CLASS("blank",isblank);
  CLASS("cntrl",iscntrl); CLASS("digit",isdigit); CLASS("graph",isgraph);
  CLASS("lower",islower); CLASS("print",isprint); CLASS("punct",ispunct);
  CLASS("space",isspace); CLASS("upper",isupper); CLASS("xdigit",isxdigit);
#undef CLASS
  return -1;
}
unsigned char bracket_byte(const char*& text,int flags) {
  if(*text=='\\' && !(flags&FNM_NOESCAPE) && text[1]) ++text;
  return static_cast<unsigned char>(*text++);
}
// A malformed bracket is a literal '['. No recursion or pattern allocation.
bool token(const char* pattern,unsigned char value,int flags,const char** after) {
  if(*pattern=='?') { *after=pattern+1; return true; }
  if(*pattern!='[') {
    if(*pattern=='\\' && !(flags&FNM_NOESCAPE) && !pattern[1]) { *after=pattern+1; return false; }
    if(*pattern=='\\' && !(flags&FNM_NOESCAPE) && pattern[1]) ++pattern;
    *after=pattern+1;
    return static_cast<unsigned char>(*pattern)==value;
  }
  const char* cursor=pattern+1;
  bool invert=*cursor=='!' || *cursor=='^';
  if(invert) ++cursor;
  bool found=false,first=true;
  while(*cursor && (*cursor!=']' || first)) {
    first=false;
    if(cursor[0]=='[' && cursor[1]==':') {
      const char* end=strstr(cursor+2,":]");
      if(end) {
        int matched=character_class(cursor+2,static_cast<size_t>(end-cursor-2),value);
        if(matched<0) { *after=cursor; return false; }
        found|=matched!=0;
        cursor=end+2;
        continue;
      }
      *after=cursor; return false;
    }
    if(cursor[0]=='[' && (cursor[1]=='.' || cursor[1]=='=')) {
      char delimiter=cursor[1];
      // The C locale has only single-byte collating/equivalence elements.
      if(!cursor[2] || cursor[3]!=delimiter || cursor[4]!=']') {
        *after=cursor; return false;
      }
      found|=static_cast<unsigned char>(cursor[2])==value;
      cursor+=5;
      continue;
    }
    unsigned char low=bracket_byte(cursor,flags);
    if(*cursor=='-' && cursor[1] && cursor[1]!=']') {
      ++cursor;
      unsigned char high=bracket_byte(cursor,flags);
      found|=low<=value && value<=high;
    } else found|=low==value;
  }
  if(*cursor!=']') { *after=pattern+1; return value=='['; }
  *after=cursor+1;
  return found!=invert;
}
}
extern "C" char* basename(char* path) {
  if(!path || !*path) return dot_path;
  size_t size=strlen(path);
  while(size>1 && path[size-1]=='/') path[--size]=0;
  if(size==1 && path[0]=='/') return path;
  char* start=path+size;
  while(start>path && start[-1]!='/') --start;
  return start;
}
extern "C" char* dirname(char* path) {
  if(!path || !*path) return dot_path;
  size_t size=strlen(path);
  while(size>1 && path[size-1]=='/') --size;
  while(size && path[size-1]!='/') --size;
  if(!size) return dot_path;
  while(size>1 && path[size-1]=='/') --size;
  path[size]=0;
  return path;
}
extern "C" int fnmatch(const char* pattern,const char* string,int flags) {
  if(!pattern || !string || (flags&~(FNM_PATHNAME|FNM_NOESCAPE|FNM_PERIOD))) {
    errno=EINVAL; return FNM_NOMATCH;
  }
  const char* start=string;
  const char* star_pattern=nullptr;
  const char* star_string=nullptr;
  auto protected_period=[&](const char* at) {
    return (flags&FNM_PERIOD) && *at=='.' &&
           (at==start || ((flags&FNM_PATHNAME) && at[-1]=='/'));
  };
  while(*string) {
    bool period=protected_period(string);
    bool explicit_dot=*pattern=='.' ||
        (*pattern=='\\' && !(flags&FNM_NOESCAPE) && pattern[1]=='.');
    if(*pattern=='*' && !period) {
      do { ++pattern; } while(*pattern=='*');
      star_pattern=pattern; star_string=string;
      continue;
    }
    const char* after=pattern;
    bool separator=(flags&FNM_PATHNAME) && *string=='/';
    if(*pattern && (!period || explicit_dot) &&
       (!separator || *pattern=='/' ||
         (*pattern=='\\' && !(flags&FNM_NOESCAPE) && pattern[1]=='/')) &&
       token(pattern,static_cast<unsigned char>(*string),flags,&after)) {
      pattern=after; ++string; continue;
    }
    if(star_pattern && *star_string && !protected_period(star_string) &&
       (!(flags&FNM_PATHNAME) || *star_string!='/')) {
      string=++star_string; pattern=star_pattern; continue;
    }
    return FNM_NOMATCH;
  }
  while(*pattern=='*') ++pattern;
  return *pattern ? FNM_NOMATCH : 0;
}
