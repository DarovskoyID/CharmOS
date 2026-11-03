#include "base/files/file_util.h"
#include <stdio.h>
#include <string.h>
#include <string>

namespace base {
namespace {
struct ComparisonFiles {
  FILE* left;
  FILE* right;
  ComparisonFiles(const FilePath& first,const FilePath& second)
      :left(fopen(first.value().c_str(),"rb")),right(fopen(second.value().c_str(),"rb")) {}
  ~ComparisonFiles() { if (left) fclose(left); if (right) fclose(right); }
};
bool ReadComparisonLine(FILE* file,std::string* line,bool* at_end) {
  line->clear();
  int byte;
  while ((byte=fgetc(file))!=EOF && byte!='\n') line->push_back(static_cast<char>(byte));
  if (ferror(file)) return false;
  *at_end=byte==EOF;
  while (!line->empty() && (line->back()=='\r' || line->back()=='\n')) line->pop_back();
  return true;
}
}
bool ContentsEqual(const FilePath& first,const FilePath& second) {
  ComparisonFiles files(first,second);
  if (!files.left || !files.right) return false;
  char left[2056],right[2056];
  for (;;) {
    size_t a=fread(left,1,sizeof left,files.left);
    size_t b=fread(right,1,sizeof right,files.right);
    if (ferror(files.left) || ferror(files.right) || a!=b || memcmp(left,right,a)) return false;
    if (!a) return true;
  }
}
bool TextContentsEqual(const FilePath& first,const FilePath& second) {
  ComparisonFiles files(first,second);
  if (!files.left || !files.right) return false;
  for (;;) {
    std::string a,b;
    bool eof_a=false,eof_b=false;
    if (!ReadComparisonLine(files.left,&a,&eof_a) || !ReadComparisonLine(files.right,&b,&eof_b) ||
        eof_a!=eof_b || a!=b) return false;
    if (eof_a) return true;
  }
}
}  // namespace base
