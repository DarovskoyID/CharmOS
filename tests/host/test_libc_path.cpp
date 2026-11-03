#include <fnmatch.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
extern "C" int charm_fnmatch(const char*,const char*,int);
extern "C" char* charm_basename(char*);
extern "C" char* charm_dirname(char*);

int main() {
  struct PathCase { const char* input; const char* base; const char* dir; };
  for(const auto& item:{PathCase{"",".","."},PathCase{"/","/","/"},
      PathCase{"////","/","/"},PathCase{"a","a","."},PathCase{"a/","a","."},
      PathCase{"/a/b//","b","/a"},PathCase{"a//b","b","a"},PathCase{"///a","a","/"}}) {
    char base[64],dir[64]; std::strcpy(base,item.input); std::strcpy(dir,item.input);
    if(std::strcmp(charm_basename(base),item.base) || std::strcmp(charm_dirname(dir),item.dir)) return 1;
  }
  if(std::strcmp(charm_basename(nullptr),".") || std::strcmp(charm_dirname(nullptr),".")) return 2;
  uint32_t state=12345;
  auto random=[&] { state=state*1664525+1013904223; return state; };
  const char patterns[]="ab./?*[]!^-\\";
  const char strings[]="ab./-";
  unsigned comparisons=0;
  for(unsigned test=0;test<100000;++test) {
    char pattern[12]{},string[12]{};
    unsigned pattern_size=random()%11,string_size=random()%11;
    for(unsigned i=0;i<pattern_size;++i) pattern[i]=patterns[(random()>>8)%(sizeof patterns-1)];
    for(unsigned i=0;i<string_size;++i) string[i]=strings[(random()>>8)%(sizeof strings-1)];
    for(int flags=0;flags<8;++flags) {
      // glibc's star fast path treats an escaped slash differently from its
      // ordinary literal path. Exercise that native case explicitly below.
      if((flags&FNM_PATHNAME) && !(flags&FNM_NOESCAPE) && std::strstr(pattern,"\\/") && std::strchr(pattern,'*')) continue;
      ++comparisons;
      bool expected=fnmatch(pattern,string,flags)==0;
      bool actual=charm_fnmatch(pattern,string,flags)==0;
      if(actual!=expected) {
        std::printf("fnmatch mismatch pattern=%s string=%s flags=%d actual=%d expected=%d\n",pattern,string,flags,actual,expected);
        return 3;
      }
    }
  }
  for(const char* pattern:{"[[:alpha:]]*","[![:digit:]]?","[]a]","[a-c]","[[:blank:]]",
      "[![:bad:]]","[b[.]","[[.a.]]","[[=a=]]","[![:]]"})
    for(const char* string:{"a","b","z9", "a.","]"," ","\t","9"})
      for(int flags=0;flags<8;++flags)
        if((fnmatch(pattern,string,flags)==0)!=(charm_fnmatch(pattern,string,flags)==0)) return 4;
  if(charm_fnmatch("*\\/","a/",FNM_PATHNAME)!=0) return 5;
  std::printf("native lexical paths and %u C-locale fnmatch differential cases ok\n",comparisons);
}
