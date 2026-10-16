#include <charm/canvas.hpp>
#include <charm/window.hpp>
#include <charm/text_buffer.hpp>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

namespace {
namespace sc=abi::syscall; namespace wm=abi::wm;
constexpr unsigned capacity=16384;
extern "C" const uint8_t wm_ui_font[],wm_ui_font_end[],wm_mono_font[],wm_mono_font_end[];
charm::gui::Canvas canvas;
charm::ui::TextBuffer<capacity> document;
char path[512]="/заметка.md", path_input[512]{}, notice[160]="Новый документ";
char saved[capacity]{}, incoming[capacity]{}, loaded_path[512]{}; unsigned saved_length=0, path_length=0;
unsigned shift=0, control=0, first_line=0, first_column=0;
bool changed=false, autosave=false, russian=false, path_mode=false, open_mode=false, all=false, path_all=false, preview=false, close_armed=false, quit=false;
uint64_t save_at=charm::kForever;
const char* language() {
  const char* ext=strrchr(path,'.'); if(!ext) return "Text";
  if(strcmp(ext,".md")==0) return "Markdown";
  if(strcmp(ext,".tex")==0) return "LaTeX";
  if(strcmp(ext,".py")==0) return "Python";
  if(strcmp(ext,".json")==0) return "JSON";
  if(strcmp(ext,".js")==0) return "JavaScript";
  if(strcmp(ext,".c")==0 || strcmp(ext,".cpp")==0 || strcmp(ext,".h")==0 || strcmp(ext,".hpp")==0) return "C/C++";
  return "Text";
}
void dirty() { changed=true; close_armed=false; save_at=autosave?charm::now_ns()+800000000:charm::kForever; notice[0]=0; }
bool write_file(const char* name,const char* text,unsigned n) {
  int fd=open(name,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0) return false;
  unsigned total=0;
  while(total<n) { ssize_t amount=write(fd,text+total,n-total); if(amount<=0) { close(fd); return false; } total+=static_cast<unsigned>(amount); }
  bool ok=fsync(fd)==0; close(fd); return ok;
}
bool save() {
  int existing=open(path,O_RDONLY);
  if(existing>=0) {
    unsigned n=0; ssize_t amount=0;
    do { amount=read(existing,incoming+n,capacity-n); if(amount>0) n+=static_cast<unsigned>(amount); } while(amount>0 && n<capacity);
    close(existing);
    if(amount<0 || strcmp(path,loaded_path)!=0 || n!=saved_length || memcmp(incoming,saved,n)!=0) {
      snprintf(notice,sizeof notice,"Файл существует или изменён снаружи; выберите новый путь F4"); save_at=charm::kForever; return false;
    }
  } else if(errno!=ENOENT) { snprintf(notice,sizeof notice,"Не удалось проверить файл перед записью"); save_at=charm::kForever; return false; }
  char backup[520]; snprintf(backup,sizeof backup,"%s.charm-backup",path);
  // No rename syscall yet: retain the previous complete contents before replacing.
  if(!write_file(backup,saved,saved_length)) { snprintf(notice,sizeof notice,"Не удалось сохранить резервную копию"); save_at=charm::kForever; return false; }
  if(!write_file(path,document.text,document.length)) { snprintf(notice,sizeof notice,"Ошибка записи; предыдущая версия в .charm-backup"); save_at=charm::kForever; return false; }
  memcpy(saved,document.text,document.length+1); saved_length=document.length; changed=false; save_at=charm::kForever;
  snprintf(loaded_path,sizeof loaded_path,"%s",path);
  snprintf(notice,sizeof notice,"Сохранено"); printf("edit: saved %s (%u bytes)\n",path,document.length); return true;
}
bool load(const char* name) {
  int fd=open(name,O_RDONLY); if(fd<0) { snprintf(notice,sizeof notice,"Не удалось открыть файл"); return false; }
  struct stat st{};
  if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0 || st.st_size>=capacity) { close(fd); snprintf(notice,sizeof notice,"Нужен текстовый файл меньше 16 КиБ"); return false; }
  unsigned n=0;
  while(n<static_cast<unsigned>(st.st_size)) { ssize_t amount=read(fd,incoming+n,static_cast<unsigned>(st.st_size)-n); if(amount<=0) { close(fd); snprintf(notice,sizeof notice,"Ошибка чтения"); return false; } n+=static_cast<unsigned>(amount); }
  close(fd);
  for(unsigned i=0;i<n;++i) if(incoming[i]==0 || (static_cast<unsigned char>(incoming[i])<32 && incoming[i]!='\n' && incoming[i]!='\t' && incoming[i]!='\r')) { snprintf(notice,sizeof notice,"Двоичный файл не открыт"); return false; }
  incoming[n]=0; memcpy(saved,incoming,n+1); saved_length=n; memcpy(document.text,saved,n+1); document.length=n; document.cursor=0; document.undo_available=false;
  snprintf(path,sizeof path,"%s",name); changed=false; all=false; first_line=0; save_at=charm::kForever;
  snprintf(loaded_path,sizeof loaded_path,"%s",path); first_column=0;
  snprintf(notice,sizeof notice,"Открыто"); printf("edit: loaded %s (%u bytes)\n",path,n); return true;
}
void path_field(bool opening) {
  if(opening && changed) { snprintf(notice,sizeof notice,"Сначала сохраните изменения: Ctrl+S"); return; }
  open_mode=opening; path_mode=true; path_all=true; save_at=charm::kForever; snprintf(path_input,sizeof path_input,"A:%s",path); path_length=static_cast<unsigned>(strlen(path_input));
}
unsigned cursor_line() { unsigned n=0; for(unsigned i=0;i<document.cursor;++i) if(document.text[i]=='\n') ++n; return n; }
void ensure_visible() {
  unsigned line=cursor_line(); if(line<first_line) first_line=line; if(line>=first_line+16) first_line=line-15;
  unsigned col=0; for(unsigned p=document.line_start();p<document.cursor;++p) if((static_cast<unsigned char>(document.text[p])&0xc0)!=0x80) ++col;
  if(col<first_column) first_column=col;
  if(col>=first_column+100) first_column=col-99;
}
void move_vertical(bool down) {
  unsigned start=document.line_start(), column=0; for(unsigned i=start;i<document.cursor;++i) if((static_cast<unsigned char>(document.text[i])&0xc0)!=0x80) ++column;
  unsigned next;
  if(down) { next=document.line_end(); if(next==document.length) return; ++next; }
  else { if(!start) return; next=start-1; while(next && document.text[next-1]!='\n') --next; }
  document.cursor=next;
  while(column-- && document.cursor<document.length && document.text[document.cursor]!='\n') document.right();
}
bool keyword(const char* text,unsigned length) {
  const char* words[]={"if","else","for","while","return","class","struct","namespace","const","auto","int","double","true","false","null","def","import","from","let","function"};
  for(const char* w:words) if(strlen(w)==length && memcmp(w,text,length)==0) return true;
  return false;
}
void draw(uint32_t* pixels,bool bgrx) {
  canvas.target(pixels,900,520,bgrx); canvas.fill(0,0,900,520,0x121214);
  canvas.fill(0,0,900,52,0x1b1a18);
  char display[530]; snprintf(display,sizeof display,"%s%s",changed?"● ":"",path_mode?path_input:path);
  canvas.text(display,20,33,0xf3efe7,635,14,true);
  canvas.rounded(658,10,220,32,7,autosave?0x332e25:0x242426);
  canvas.text(autosave?"Автосохранение: вкл · F2":"Автосохранение: выкл · F2",670,32,autosave?0xf5c518:0xb7b1a6,872,11);
  canvas.text(path_mode?(open_mode?"Открыть: Enter · Escape: отмена":"Сохранить как: Enter · Escape: отмена"):"Ctrl+S сохранить · F3 открыть · F4 сохранить как · F5 Markdown",20,76,0x8e887d,880,11);
  canvas.fill(0,90,58,368,0x101012);
  unsigned pos=0,line_number=0;
  while(pos<document.length && line_number<first_line) if(document.text[pos++]=='\n') ++line_number;
  for(int row=0;row<16 && pos<=document.length;++row,++line_number) {
    int y=116+row*21;
    char number[16]; snprintf(number,sizeof number,"%u",line_number+1); canvas.text(number,12,y,0x56534e,54,12,true);
    unsigned end=pos; while(end<document.length && document.text[end]!='\n') ++end;
    if(all && !preview) canvas.fill(64,y-15,820,20,0x332e25);
    int x=68;
    bool comment=false,string=false; char quote=0;
    bool markdown=strcmp(language(),"Markdown")==0, latex=strcmp(language(),"LaTeX")==0;
    if(markdown && document.text[pos]=='#') comment=true;
    if(preview && markdown) { while(pos<end && (document.text[pos]=='#' || document.text[pos]==' ')) ++pos; }
    if(!preview) for(unsigned skip=0;skip<first_column && pos<end;++skip) { const char* p=document.text+pos; lib::font::utf8_next(&p); pos=static_cast<unsigned>(p-document.text); }
    for(unsigned p=pos;p<end && x<875;) {
      uint32_t color=0xd8d2c7;
      bool opened_string=false;
      if(p==document.cursor && !preview && !path_mode) canvas.fill(x,y-15,1,19,0xf5c518);
      if((document.text[p]=='/' && p+1<end && document.text[p+1]=='/') || (latex && document.text[p]=='%') || (strcmp(language(),"Python")==0 && document.text[p]=='#')) comment=true;
      if(comment) color=markdown?0xf5c518:0x8e887d;
      else if(string) color=0xb8cf8a;
      else if(document.text[p]=='"' || document.text[p]=='\'') { quote=document.text[p]; string=true; opened_string=true; color=0xb8cf8a; }
      else if(document.text[p]>='0' && document.text[p]<='9') color=0xffb86c;
      else if(latex && (document.text[p]=='\\' || document.text[p]=='$' || document.text[p]=='{' || document.text[p]=='}')) color=0xf5c518;
      unsigned token=p;
      if(!comment && !string && ((document.text[p]>='a' && document.text[p]<='z') || (document.text[p]>='A' && document.text[p]<='Z'))) {
        unsigned last=p; while(last<end && ((document.text[last]>='a' && document.text[last]<='z') || (document.text[last]>='A' && document.text[last]<='Z') || document.text[last]=='_')) ++last;
        if(keyword(document.text+p,last-p)) color=0xbca1eb;
        if(document.cursor>p && document.cursor<last && !preview && !path_mode) canvas.fill(x+static_cast<int>(document.cursor-p)*8,y-15,1,19,0xf5c518);
        char word[128]; unsigned n=last-p; if(n>=sizeof word) n=sizeof word-1;
        memcpy(word,document.text+p,n); word[n]=0; canvas.text(word,x,y,color,877,13,true); x+=static_cast<int>(n)*8; p=last;
      } else {
        const char* next=document.text+p; lib::font::utf8_next(&next); unsigned n=static_cast<unsigned>(next-(document.text+p));
        char glyph[5]{}; if(n>4) n=1; memcpy(glyph,document.text+p,n);
        if(glyph[0]=='\t') { x+=32; } else if(glyph[0]!='\r') { canvas.text(glyph,x,y,color,877,13,true); x+=8; }
        p+=n;
      }
      if(string && !opened_string && document.text[token]==quote && (token==0 || document.text[token-1]!='\\')) string=false;
    }
    if(document.cursor==end && !preview && !path_mode) canvas.fill(x,y-15,1,19,0xf5c518);
    if(end==document.length) break;
    pos=end+1;
  }
  canvas.fill(0,466,900,1,0x2a2a2e);
  char status[160]; snprintf(status,sizeof status,"%s · UTF-8 · %s · %u байт · строка %u · Ctrl+Z/Y: отмена/повтор",language(),russian?"RU (F8)":"EN (F8)",document.length,cursor_line()+1);
  canvas.text(status,20,490,0x8e887d,885,11);
  canvas.text(notice,20,512,close_armed?0xffb5ac:0xb7b1a6,885,11);
}
bool input(const sc::InputEvent& e) {
  if(e.type==sc::kInputKey && (e.code==0xe1 || e.code==0xe5 || e.code==0xe0 || e.code==0xe4)) {
    unsigned bit=(e.code==0xe1 || e.code==0xe0)?1u:2u; auto& state=(e.code==0xe0 || e.code==0xe4)?control:shift;
    if(e.value) state|=bit; else state&=~bit; return false;
  }
  if(e.type==sc::kInputWheel && e.value) { if(e.value>0) first_line=first_line>3?first_line-3:0; else if(first_line<cursor_line()+16) first_line+=3; return true; }
  if(e.type==sc::kInputButton && e.code==1 && e.value==1 && e.x>=658 && e.y>=10 && e.y<42) { autosave=!autosave; if(changed) dirty(); return true; }
  if(e.type==sc::kInputButton && e.code==1 && e.value==1 && e.x>=58 && e.x<890 && e.y>=90 && e.y<458 && !preview) {
    unsigned line=first_line+static_cast<unsigned>((e.y>96?e.y-96:0)/21), p=0;
    while(p<document.length && line) if(document.text[p++]=='\n') --line;
    document.cursor=p; unsigned column=first_column+static_cast<unsigned>((e.x>68?e.x-68:0)/8);
    while(column-- && document.cursor<document.length && document.text[document.cursor]!='\n') document.right();
    all=false; ensure_visible(); return true;
  }
  if(e.type!=sc::kInputKey || !e.value) return false;
  if(e.code==0x41) { russian=!russian; return true; }
  if(path_mode) {
    if(e.code==0x29) { path_mode=false; if(changed && autosave) dirty(); }
    else if(e.code==0x28) {
      const char* next=(strncmp(path_input,"A:",2)==0 || strncmp(path_input,"a:",2)==0)?path_input+2:path_input;
      if(*next!='/') snprintf(notice,sizeof notice,"Нужен абсолютный путь A:/…");
      else if(open_mode) { if(load(next)) path_mode=false; }
      else { snprintf(path,sizeof path,"%s",next); path_mode=false; save(); }
    } else if(control && e.code==4) path_all=true;
    else {
      if(path_all) { path_input[0]=0; path_length=0; path_all=false; }
      if(e.code==0x2a) charm::ui::backspace(path_input,path_length);
      else charm::ui::append(path_input,sizeof path_input,path_length,charm::ui::key(e.code,shift!=0,russian));
    }
    return true;
  }
  if(control && e.code==0x16) save();
  else if(control && e.code==4) all=true;
  else if(control && (e.code==0x1d || e.code==0x1c)) { if(document.undo()) dirty(); }
  else if(e.code==0x3b) { autosave=!autosave; if(changed) dirty(); }
  else if(e.code==0x3c) path_field(true);
  else if(e.code==0x3d) path_field(false);
  else if(e.code==0x3e) { if(strcmp(language(),"Markdown")==0) preview=!preview; else snprintf(notice,sizeof notice,"Предпросмотр доступен для Markdown; LaTeX — исходный текст"); }
  else if(e.code==0x3f && close_armed) quit=true;
  else if(e.code==0x50) { document.left(); all=false; }
  else if(e.code==0x4f) { document.right(); all=false; }
  else if(e.code==0x52 || e.code==0x51) { move_vertical(e.code==0x51); all=false; }
  else if(e.code==0x4a) { document.cursor=control?0:document.line_start(); all=false; }
  else if(e.code==0x4d) { document.cursor=control?document.length:document.line_end(); all=false; }
  else if(e.code==0x29) { all=false; preview=false; }
  else if(e.code==0x2a) { if(document.erase(all)) dirty(); all=false; }
  else {
    uint32_t cp=e.code==0x28?'\n':e.code==0x2b?'\t':charm::ui::key(e.code,shift!=0,russian);
    if(!cp || control) return false;
    if(document.insert(cp,all)) dirty(); else snprintf(notice,sizeof notice,"Достигнут предел 16 КиБ"); all=false;
  }
  ensure_visible(); return true;
}
}
extern "C" int main(int argc,char** argv) {
  if(!canvas.fonts({wm_ui_font,static_cast<size_t>(wm_ui_font_end-wm_ui_font)},{wm_mono_font,static_cast<size_t>(wm_mono_font_end-wm_mono_font)})) return 1;
  if(argc>1) load(argv[1]);
  charm::gui::Window window; if(window.create("Редактор",900,520)!=charm::Status::Ok) return 1;
  printf("edit: ready\n"); int available=0; bool repaint=true;
  for(;;) {
    if(repaint && available>=0) { draw(window.pixels(static_cast<uint32_t>(available)),window.format()==sc::kDisplayBgrx); if(window.present(static_cast<uint32_t>(available),{0,0,900,520})!=charm::Status::Ok) return 1; available=-1; repaint=false; }
    wm::WindowEvent event{}; auto status=window.wait(&event,save_at);
    if(status==charm::Status::TimedOut) { if(changed && autosave) save(); repaint=true; continue; }
    if(status==charm::Status::Interrupted) continue;
    if(status!=charm::Status::Ok) return 0;
    if(event.type==wm::kCloseRequest) {
      if(changed && (!autosave || !save())) { close_armed=true; snprintf(notice,sizeof notice,"Есть изменения: Ctrl+S сохранить; F6 закрыть без сохранения"); repaint=true; }
      else return 0;
    }
    if(event.type==wm::kFrameDone) available=static_cast<int>(event.value);
    if(event.type==wm::kInput) { sc::InputEvent e{}; while(window.next_input(&e)) if(input(e)) repaint=true; }
    if(quit) return 0;
  }
}
