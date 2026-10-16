#pragma once
#include <charm/expression.hpp>
#include <string.h>
namespace charm::math {
struct Unit { const char* name; unsigned dimension; double scale, offset; };
inline constexpr Unit units[] = {
  {"m",1,1,0},{"cm",1,.01,0},{"mm",1,.001,0},{"km",1,1000,0},{"in",1,.0254,0},{"ft",1,.3048,0},
  {"м",1,1,0},{"см",1,.01,0},{"мм",1,.001,0},{"км",1,1000,0},
  {"kg",2,1,0},{"g",2,.001,0},{"mg",2,.000001,0},{"lb",2,.45359237,0},
  {"кг",2,1,0},{"г",2,.001,0},
  {"s",3,1,0},{"min",3,60,0},{"h",3,3600,0},{"с",3,1,0},{"мин",3,60,0},{"ч",3,3600,0},
  {"K",4,1,0},{"C",4,1,273.15},{"F",4,5.0/9,255.3722222222222}
};
inline const Unit* unit(const char* name) { for (const auto& u : units) if (strcmp(name,u.name)==0) return &u; return nullptr; }
struct Conversion { double value; bool valid; };
inline Conversion convert(double value, const char* from, const char* to) {
  auto* a=unit(from); auto* b=unit(to);
  if (!a || !b || a->dimension != b->dimension) return {0,false};
  double base=value*a->scale+a->offset;
  if (a->dimension==4 && base < -1e-9) return {0,false};
  double out=(base-b->offset)/b->scale;
  return {out, out==out && out>=-1e100 && out<=1e100};
}
inline Conversion convert_query(const char* p) {
  char number[64]{}, from[16]{}, to[16]{};
  auto spaces=[&]() { while (*p==' ') ++p; };
  auto word=[&](char* out,unsigned capacity) {
    spaces(); unsigned n=0; while (*p && *p!=' ') { if (n+1>=capacity) return false; out[n++]=*p++; } out[n]=0; return n!=0;
  };
  if (!word(number,sizeof number) || !word(from,sizeof from)) return {0,false};
  spaces(); if (p[0]=='-' && p[1]=='>') p+=2;
  if (!word(to,sizeof to)) return {0,false};
  spaces(); if (*p) return {0,false};
  auto value=evaluate(number);
  return value.error==Error::None ? convert(value.value,from,to) : Conversion{0,false};
}
}
