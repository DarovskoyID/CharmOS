#pragma once
#include <stdint.h>
#include <string.h>
namespace charm::calc {
struct IntegerResult { uint64_t value; bool valid; };
class IntegerParser {
  const char* p; unsigned work=0, depth=0; bool ok=true;
  void spaces() { while(*p==' ') ++p; }
  uint64_t atom() {
    spaces(); if(++work>512 || ++depth>32) { ok=false; --depth; return 0; }
    uint64_t value=0;
    if(*p=='~' || *p=='-' || *p=='+') { char op=*p++; value=atom(); if(op=='~') value=~value; if(op=='-') value=uint64_t{0}-value; }
    else if(*p=='(') { ++p; value=expr(1); spaces(); if(*p==')') ++p; else ok=false; }
    else {
      unsigned base=10, digits=0;
      if(p[0]=='0' && (p[1]=='x' || p[1]=='X')) { base=16; p+=2; }
      else if(p[0]=='0' && (p[1]=='b' || p[1]=='B')) { base=2; p+=2; }
      for(;;) {
        unsigned d=*p>='0' && *p<='9'?static_cast<unsigned>(*p-'0'):*p>='a' && *p<='f'?static_cast<unsigned>(*p-'a'+10):*p>='A' && *p<='F'?static_cast<unsigned>(*p-'A'+10):99u;
        if(d>=base) break;
        if(value>(UINT64_MAX-d)/base) { ok=false; break; }
        value=value*base+d; ++p; ++digits;
      }
      if(!digits) ok=false;
    }
    --depth; return value;
  }
  uint64_t expr(unsigned minimum) {
    uint64_t left=atom();
    while(ok) {
      spaces(); char op=*p; unsigned priority=0, count=1;
      if(op=='|') priority=1; else if(op=='^') priority=2; else if(op=='&') priority=3;
      else if((op=='<' || op=='>') && p[1]==op) { priority=4; count=2; }
      else if(op=='+' || op=='-') priority=5;
      else if(op=='*' || op=='/' || op=='%') priority=6;
      if(priority<minimum) break;
      p+=count; if(++work>512) { ok=false; break; }
      uint64_t right=expr(priority+1); if(!ok) break;
      switch(op) {
        case '|': left|=right; break; case '^': left^=right; break; case '&': left&=right; break;
        case '+': left+=right; break; case '-': left-=right; break; case '*': left*=right; break;
        case '/': if(!right) ok=false; else left/=right; break;
        case '%': if(!right) ok=false; else left%=right; break;
        case '<': if(right>=64) ok=false; else left<<=right; break;
        case '>': if(right>=64) ok=false; else left>>=right; break;
      }
    }
    return left;
  }
public:
  explicit IntegerParser(const char* text):p(text) {}
  IntegerResult run() { if(strlen(p)>191) return {0,false}; auto value=expr(1); spaces(); return {value,ok && !*p}; }
};
inline IntegerResult integer(const char* text) { return IntegerParser(text).run(); }
inline void base_text(uint64_t value,unsigned base,char* out,unsigned capacity) {
  if(!capacity) return;
  if(base<2 || base>16) { out[0]=0; return; }
  char reverse[64]; unsigned n=0; do { reverse[n++]="0123456789ABCDEF"[value%base]; value/=base; } while(value);
  if(n>=capacity) { out[0]=0; return; }
  for(unsigned i=0;i<n;++i) out[i]=reverse[n-i-1]; out[n]=0;
}
struct Element { const char* symbol; double mass; };
// Rounded standard atomic weights, CIAAW 2024; see DESKTOP-APPS.md.
inline constexpr Element elements[]={
  {"H",1.0080},{"He",4.0026},{"Li",6.94},{"Be",9.0122},{"B",10.81},{"C",12.011},
  {"N",14.007},{"O",15.999},{"F",18.998},{"Ne",20.180},{"Na",22.990},{"Mg",24.305},
  {"Al",26.982},{"Si",28.085},{"P",30.974},{"S",32.06},{"Cl",35.45},{"Ar",39.95},
  {"K",39.098},{"Ca",40.078},{"Fe",55.845},{"Cu",63.546},{"Zn",65.38},{"Ag",107.87}
};
struct MassResult { double mass; bool valid; };
class FormulaParser {
  const char* p; bool ok=true; unsigned work=0;
  unsigned count() {
    if(*p<'0' || *p>'9') return 1;
    unsigned n=0; while(*p>='0' && *p<='9') { if(n>999) { ok=false; return 0; } n=n*10+static_cast<unsigned>(*p++-'0'); }
    if(!n) ok=false; return n;
  }
  double group(unsigned depth) {
    if(depth>8) { ok=false; return 0; }
    double total=0; unsigned terms=0;
    while(ok && *p && *p!=')') {
      if(*p==' ') { ++p; continue; }
      if(++work>191) { ok=false; break; }
      double mass=0;
      if(*p=='(') { ++p; mass=group(depth+1); if(*p==')') ++p; else ok=false; }
      else {
        char symbol[3]{};
        if(*p<'A' || *p>'Z') { ok=false; break; }
        symbol[0]=*p++; if(*p>='a' && *p<='z') symbol[1]=*p++;
        for(const auto& e:elements) if(strcmp(symbol,e.symbol)==0) { mass=e.mass; break; }
        if(mass==0) ok=false;
      }
      total+=mass*count(); ++terms; if(total>1e12) ok=false;
    }
    if(!terms) ok=false; return total;
  }
public:
  explicit FormulaParser(const char* text):p(text) {}
  MassResult run() { if(strlen(p)>191) return {0,false}; double mass=group(0); return {mass,ok && !*p}; }
};
inline MassResult molar_mass(const char* text) { return FormulaParser(text).run(); }
struct Circuit { double resistance,current,power,branch1,branch2; bool valid; };
inline bool positive(double n) { return n>0 && n<=1e9; }
inline bool bounded(double n) { return n==n && n>=-1e100 && n<=1e100; }
inline Circuit resistors(double voltage,double r1,double r2,bool parallel) {
  if(!(voltage>=0 && voltage<=1e9) || !positive(r1) || !positive(r2)) return {};
  double resistance=parallel?(r1<r2?r1/(1+r1/r2):r2/(1+r2/r1)):r1+r2;
  double current=voltage/resistance;
  double power=voltage*current, branch1=parallel?voltage/r1:current, branch2=parallel?voltage/r2:current;
  if(!bounded(current) || !bounded(power) || !bounded(branch1) || !bounded(branch2) || resistance<=0) return {};
  return {resistance,current,power,branch1,branch2,true};
}
struct Spring { double displacement,period,energy; bool valid; };
inline Spring spring(double force,double stiffness,double mass) {
  if(!(force>=-1e9 && force<=1e9) || !positive(stiffness) || !positive(mass)) return {};
  double ratio=mass/stiffness;
  if(!bounded(ratio) || ratio<=0) return {};
  double scale=1;
  while(ratio>=4) { ratio*=.25; scale*=2; }
  while(ratio<1) { ratio*=4; scale*=.5; }
  double root=2;
  for(unsigned i=0;i<24;++i) root=.5*(root+ratio/root);
  root*=scale;
  double x=force/stiffness;
  double period=6.2831853071795864769*root, energy=.5*stiffness*x*x;
  if(!bounded(x) || !bounded(period) || !bounded(energy)) return {};
  return {x,period,energy,true};
}
}
