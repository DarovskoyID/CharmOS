#include <charm/canvas.hpp>
#include <charm/window.hpp>
#include <charm/expression.hpp>
#include <charm/text_input.hpp>
#include <charm/calculator_profiles.hpp>
#include <stdio.h>
#include <string.h>

namespace {
namespace sc = abi::syscall;
namespace wm = abi::wm;
constexpr int width = 860, height = 480;
extern "C" const uint8_t wm_ui_font[], wm_ui_font_end[], wm_mono_font[], wm_mono_font_end[];
charm::gui::Canvas canvas;
char expression[192] = "2+2";
unsigned length = 3, shift = 0;
bool graph = false, replace = true;
double parameter = 1;
unsigned profile=0, selected=0, physics_preset=0;
double variable=2, circuit_values[3]={12,100,200}, spring_values[3]={10,100,1};
bool parallel=false, mechanical=false, value_edit=false, value_error=false;
char value_input[48]{}; unsigned value_length=0;
const char* physics_formulas[]={"a*x^2/2","a*x","a*x","a*9.80665*x"};
const char* physics_labels[]={"Кинетическая энергия: a = m (кг), x = v (м/с), результат Дж", "Закон Ома: a = R (Ом), x = I (А), результат В", "Закон Гука: a = k (Н/м), x = смещение (м), результат Н", "Потенциальная энергия: a = m (кг), x = h (м), результат Дж"};
void edit(uint32_t cp) {
  if (replace) { expression[0] = 0; length = 0; replace = false; }
  charm::ui::append(expression, sizeof expression, length, cp);
}
void format(char* out, size_t capacity, double value) {
  int exponent = 0;
  while (value > 1e9 || value < -1e9) { value /= 10; ++exponent; }
  while (value != 0 && value < 0.000001 && value > -0.000001 && exponent > -100) { value *= 10; --exponent; }
  if (exponent) snprintf(out, capacity, "%.6f ×10^%d", value, exponent);
  else snprintf(out, capacity, "%.6f", value);
}
void mode(bool plot) {
  graph = plot; profile=plot?1u:0u; value_edit=false;
  snprintf(expression, sizeof expression, "%s", graph ? "a*sin(x)" : "2+2");
  length = static_cast<unsigned>(strlen(expression)); replace = true;
}
void set_profile(unsigned next) {
  if(next<2) { mode(next==1); return; }
  profile=next; graph=false; selected=0; value_edit=false; value_error=false; replace=true;
  if(next==3 && parameter<0) parameter=0;
  snprintf(expression,sizeof expression,"%s",next==2?"0xFF & 0b1010":next==3?"Ca(OH)2":next==4?physics_formulas[physics_preset]:"Два резистора / пружина");
  length=static_cast<unsigned>(strlen(expression));
}
void parameter_cards(const char* const* names,const double* values,unsigned count) {
  for(unsigned i=0;i<count;++i) {
    int x=24+static_cast<int>(i)*270;
    canvas.rounded(x,374,256,62,9,i==selected?0x332e25:0x1b1a18);
    char text[96]; snprintf(text,sizeof text,"%s: %.3f",names[i],values[i]);
    if(i==selected && value_edit) snprintf(text,sizeof text,"%s: %s_",names[i],value_input);
    canvas.text(text,x+12,410,i==selected?0xf5c518:0xd8d2c7,x+248,13,true);
  }
}
void line(int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    canvas.fill(x0, y0, 2, 2, 0xf5c518);
    if (x0 == x1 && y0 == y1) break;
    int twice = error * 2;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}
void draw(uint32_t* pixels, bool bgrx) {
  canvas.target(pixels, width, height, bgrx); canvas.fill(0, 0, width, height, 0x121214);
  const char* titles[]={"Инженерный F1","График F2","Программист F3","Химия F4","Физика F5","Модели F6"};
  for(unsigned i=0;i<6;++i) { int x=20+static_cast<int>(i)*137; canvas.rounded(x,14,130,38,9,profile==i?0x332e25:0x1b1a18); canvas.text(titles[i],x+8,39,profile==i?0xf5c518:0xb7b1a6,x+126,11); }
  canvas.rounded(20, 72, 820, 64, 12, 0x332e25);
  canvas.rounded(21, 73, 818, 62, 11, 0x0b0b0c);
  canvas.text(expression, 36, 114, 0xf3efe7, 824, 20, true);
  if (graph) {
    char label[96]; snprintf(label, sizeof label, "x: −10…10   y: −10…10   a = %.2f   ↑↓: параметр", parameter);
    canvas.text(label, 26, 164, 0xb7b1a6, 835, 12);
    canvas.fill(30, 184, 800, 240, 0x0b0b0c);
    for (int i = 0; i <= 20; ++i) {
      canvas.fill(30 + i * 40, 184, 1, 240, i == 10 ? 0x8e887d : 0x242426);
      canvas.fill(30, 184 + i * 12, 800, 1, i == 10 ? 0x8e887d : 0x242426);
    }
    bool previous = false; int px = 0, py = 0;
    for (int i = 0; i < 400; ++i) {
      double x = -10 + i * (20.0 / 399);
      auto r = charm::math::evaluate(expression, x, parameter);
      if (r.error != charm::math::Error::None || r.value < -10 || r.value > 10) { previous = false; continue; }
      int xx = 30 + i * 2, yy = 304 - static_cast<int>(r.value * 12);
      if (previous && yy - py < 96 && py - yy < 96) line(px, py, xx, yy);
      px = xx; py = yy; previous = true;
    }
  } else if(profile==2) {
    auto result=charm::calc::integer(expression); char number[70];
    if(result.valid) {
      const unsigned bases[]={10,16,2}; const char* labels[]={"DEC","HEX","BIN"};
      for(unsigned i=0;i<3;++i) { charm::calc::base_text(result.value,bases[i],number,sizeof number); int y=190+static_cast<int>(i)*62; canvas.text(labels[i],26,y,0x8e887d,90,12,true); canvas.text(number,90,y,0xf5c518,840,i==2?12:19,true); }
    } else canvas.text("Ошибка выражения, деление на ноль или сдвиг вне 0…63",24,190,0xffb5ac,835,15);
    canvas.text("64 бита без знака · переполнение по модулю 2^64",24,372,0xb7b1a6,835,13);
    canvas.text("0x HEX · 0b BIN · + - * / % · & | ^ ~ · << >> · ( )",24,410,0x8e887d,835,13,true);
  } else if(profile==3) {
    auto result=charm::calc::molar_mass(expression); char text[160];
    if(result.valid) {
      snprintf(text,sizeof text,"M = %.4f г/моль",result.mass); canvas.text(text,24,194,0xf5c518,835,26,true);
      snprintf(text,sizeof text,"m = %.2f г    n = %.6f моль    ↑↓: масса",parameter,parameter/result.mass); canvas.text(text,24,247,0xd8d2c7,835,16,true);
    } else canvas.text("Неизвестный элемент или ошибка формулы",24,194,0xffb5ac,835,18);
    canvas.text("Скобки и индексы: Ca(OH)2, Al2(SO4)3",24,300,0xb7b1a6,835,14);
    canvas.text("H He Li Be B C N O F Ne Na Mg Al Si P S Cl Ar K Ca",24,345,0x8e887d,835,12,true);
    canvas.text("Fe Cu Zn Ag · округлённые веса CIAAW 2024",24,378,0x8e887d,835,12,true);
    canvas.text("Не поддерживаются изотопы, заряды, гидраты и реакции",24,414,0x8e887d,835,12);
  } else if(profile==4) {
    auto result=charm::math::evaluate(expression,variable,parameter); char text[160];
    if(result.error==charm::math::Error::None) format(text,sizeof text,result.value); else snprintf(text,sizeof text,"%s",charm::math::error_text(result.error));
    canvas.text(text,24,194,result.error==charm::math::Error::None?0xf5c518:0xffb5ac,835,26,true);
    canvas.text(physics_labels[physics_preset],24,247,0xb7b1a6,835,13);
    canvas.text("Формулу можно менять; единицы результата зависят от неё",24,294,0x8e887d,835,12);
    canvas.text("F7: следующая формула · Tab: параметр · ↑↓: значение",24,334,0x8e887d,835,12);
    const char* names[]={"a","x"}; double values[]={parameter,variable}; parameter_cards(names,values,2);
  } else if(profile==5) {
    char text[192]; double* values=mechanical?spring_values:circuit_values;
    if(mechanical) {
      auto result=charm::calc::spring(values[0],values[1],values[2]);
      canvas.text("Груз на пружине · F8: электрическая цепь",24,164,0xb7b1a6,835,13);
      if(result.valid) snprintf(text,sizeof text,"x = %.4f м   T = %.4f с   E = %.4f Дж",result.displacement,result.period,result.energy);
      else snprintf(text,sizeof text,"Нужны k > 0, m > 0; конечные параметры до 10^9");
      canvas.text(text,24,216,result.valid?0xf5c518:0xffb5ac,835,16,true);
      canvas.fill(90,260,10,70,0x8e887d); int end=490;
      if(result.valid) { double x=result.displacement; if(x>2) x=2; if(x< -2) x=-2; end+=static_cast<int>(x*60); }
      int step=(end-120)/12, last=120; line(100,295,120,295);
      for(int i=0;i<12;++i) { int next=120+(i+1)*step; line(last,i==0?295:(i%2?280:310),next,i==11?295:(i%2?310:280)); last=next; }
      line(last,295,end,295); canvas.rounded(end,264,100,62,8,0x332e25); canvas.text("m",end+42,303,0xf5c518,end+95,18,true);
      const char* names[]={"F, Н","k, Н/м","m, кг"}; parameter_cards(names,values,3);
    } else {
      auto result=charm::calc::resistors(values[0],values[1],values[2],parallel);
      canvas.text(parallel?"Параллельно · F7: последовательно · F8: механика":"Последовательно · F7: параллельно · F8: механика",24,164,0xb7b1a6,835,13);
      if(result.valid) snprintf(text,sizeof text,"R = %.4f Ом   I = %.6f А   P = %.4f Вт",result.resistance,result.current,result.power);
      else snprintf(text,sizeof text,"Нужны U ≥ 0, R1 > 0, R2 > 0; параметры до 10^9");
      canvas.text(text,24,216,result.valid?0xf5c518:0xffb5ac,835,16,true);
      if(parallel) { line(90,295,190,295); line(190,260,190,330); line(190,260,600,260); line(190,330,600,330); line(600,260,600,330); line(600,295,740,295); }
      else line(90,295,740,295);
      int y1=parallel?244:279, y2=parallel?314:279, x2=parallel?340:510;
      canvas.rounded(340,y1,96,32,3,0x332e25); canvas.text("R1",370,y1+23,0xf5c518,430,13,true);
      canvas.rounded(x2,y2,96,32,3,0x332e25); canvas.text("R2",x2+30,y2+23,0xf5c518,x2+90,13,true);
      canvas.text("+ U −",35,300,0xb7b1a6,150,13,true);
      const char* names[]={"U, В","R1, Ом","R2, Ом"}; parameter_cards(names,values,3);
    }
  } else {
    auto r = charm::math::evaluate(expression, 0, parameter); char result[128];
    if (r.error == charm::math::Error::None) format(result, sizeof result, r.value);
    else snprintf(result, sizeof result, "%s", charm::math::error_text(r.error));
    canvas.text(result, 24, 190, r.error == charm::math::Error::None ? 0xf5c518 : 0xffb5ac, 835, 26, true);
    const char* keys = "789/(456*)123-^0.=+C";
    for (int i = 0; i < 20; ++i) {
      int x = 20 + i % 5 * 78, y = 220 + i / 5 * 48;
      canvas.rounded(x, y, 70, 40, 8, 0x1b1a18);
      char label[2] = {keys[i], 0}; canvas.text(label, x + 27, y + 28, 0xf3efe7, x + 65, 18, true);
    }
    canvas.text("Функции", 442, 250, 0xf5c518, 830, 16);
    canvas.text("sqrt(x)   abs(x)", 442, 286, 0xd8d2c7, 830, 16, true);
    canvas.text("sin(x)  cos(x)  tan(x)", 442, 322, 0xd8d2c7, 830, 16, true);
    canvas.text("ln(x)  log(x)  exp(x)", 442, 358, 0xd8d2c7, 830, 16, true);
    canvas.text("pi · e · 1e-6 · радианы · ^ целое", 442, 396, 0x8e887d, 830, 11);
  }
  if(profile==5 && value_error) canvas.text("Некорректное число; параметр не изменён",24,361,0xffb5ac,835,11);
  canvas.text(profile==5?"Tab/клик: параметр · число + Enter: задать · ↑↓: ±1 (Shift: ±10) · F7/F8: схема":"Enter: вычислить · Backspace: удалить · F1…F6: режим · Esc: очистить",24,460,0x8e887d,840,11);
}
bool input(const sc::InputEvent& e) {
  if (e.type == sc::kInputKey && (e.code == 0xe1 || e.code == 0xe5)) {
    unsigned bit = e.code == 0xe1 ? 1u : 2u; if (e.value) shift |= bit; else shift &= ~bit; return false;
  }
  if (e.type == sc::kInputButton && e.code == 1 && e.value == 1) {
    if (e.y >= 14 && e.y < 52 && e.x>=20 && e.x<842) set_profile(static_cast<unsigned>((e.x-20)/137));
    else if((profile==4 || profile==5) && e.y>=374 && e.y<436 && e.x>=24 && e.x<834) { selected=static_cast<unsigned>((e.x-24)/270); if(profile==4 && selected>1) selected=1; value_edit=false; }
    else if (profile==0 && e.x >= 20 && e.x < 410 && e.y >= 220 && e.y < 412) {
      int column = (e.x - 20) / 78, row = (e.y - 220) / 48;
      if ((e.x - 20) % 78 >= 70 || (e.y - 220) % 48 >= 40) return false;
      char c = "789/(456*)123-^0.=+C"[row * 5 + column];
      if (c == 'C') { expression[0] = 0; length = 0; replace = false; }
      else if (c == '=') replace = true;
      else edit(static_cast<unsigned char>(c));
    } else return false;
    return true;
  }
  if (e.type != sc::kInputKey || !e.value) return false;
  if(e.code>=0x3a && e.code<=0x3f) set_profile(e.code-0x3a);
  else if(profile==5) {
    value_error=false;
    double* values=mechanical?spring_values:circuit_values;
    if(e.code==0x40) { parallel=!parallel; value_edit=false; }
    else if(e.code==0x41) { mechanical=!mechanical; value_edit=false; }
    else if(e.code==0x2b) { selected=(selected+1)%3; value_edit=false; }
    else if(e.code==0x51 || e.code==0x52) { values[selected]+=(e.code==0x52?1:-1)*(shift?10:1); value_edit=false; }
    else if(e.code==0x29) value_edit=false;
    else if(e.code==0x2a && value_edit) charm::ui::backspace(value_input,value_length);
    else if(e.code==0x28) {
      if(value_edit) { auto r=charm::math::evaluate(value_input); if(r.error==charm::math::Error::None && r.value>=-1e9 && r.value<=1e9) { values[selected]=r.value; value_edit=false; } else { value_error=true; printf("calc: model input error\n"); return true; } }
      printf("calc: model %s %.3f %.3f %.3f\n",mechanical?"spring":parallel?"parallel":"series",values[0],values[1],values[2]);
      if(mechanical) { auto r=charm::calc::spring(values[0],values[1],values[2]); printf("calc: spring %s x=%.4f T=%.4f\n",r.valid?"ok":"error",r.displacement,r.period); }
      else { auto r=charm::calc::resistors(values[0],values[1],values[2],parallel); printf("calc: circuit %s R=%.4f I=%.6f\n",r.valid?"ok":"error",r.resistance,r.current); }
    }
    else { auto cp=charm::ui::key(e.code,shift!=0,false); if((cp>='0' && cp<='9') || cp=='.' || cp=='-') { if(!value_edit) { value_input[0]=0; value_length=0; value_edit=true; } charm::ui::append(value_input,sizeof value_input,value_length,cp); } else return false; }
  }
  else if(profile==4 && e.code==0x40) { physics_preset=(physics_preset+1)%4; snprintf(expression,sizeof expression,"%s",physics_formulas[physics_preset]); length=static_cast<unsigned>(strlen(expression)); replace=true; }
  else if(profile==4 && e.code==0x2b) selected=(selected+1)%2;
  else if((profile==3 || profile==4) && (e.code==0x51 || e.code==0x52)) { double& v=profile==4 && selected==1?variable:parameter; v+=e.code==0x52?.5:-.5; if(profile==3 && v<0) v=0; }
  else if (e.code == 0x29) { expression[0] = 0; length = 0; replace = false; }
  else if (e.code == 0x2a) { charm::ui::backspace(expression, length); replace = false; }
  else if (graph && (e.code == 0x51 || e.code == 0x52)) {
    parameter += e.code == 0x52 ? .5 : -.5;
    printf("calc: parameter %.2f\n", parameter);
  } else if (e.code == 0x28) {
    if(profile==2) { auto r=charm::calc::integer(expression); char text[70]; charm::calc::base_text(r.value,10,text,sizeof text); printf("calc: integer %s\n",r.valid?text:"error"); replace=true; return true; }
    if(profile==3) { auto r=charm::calc::molar_mass(expression); printf("calc: molar %.4f %s\n",r.mass,r.valid?"ok":"error"); replace=true; return true; }
    auto r = charm::math::evaluate(expression, profile==4?variable:0, parameter);
    char text[96]; format(text, sizeof text, r.value);
    printf("calc: %s %s\n", r.error == charm::math::Error::None ? "result" : "error", r.error == charm::math::Error::None ? text : charm::math::error_text(r.error));
    replace = true;
  } else { uint32_t cp = charm::ui::key(e.code, shift != 0, false); if (!cp) return false; edit(cp); }
  return true;
}
}
extern "C" int main() {
  if (!canvas.fonts({wm_ui_font, static_cast<size_t>(wm_ui_font_end - wm_ui_font)}, {wm_mono_font, static_cast<size_t>(wm_mono_font_end - wm_mono_font)})) return 1;
  charm::gui::Window window;
  if (window.create("Калькулятор", width, height) != charm::Status::Ok) return 1;
  printf("calc: ready\n");
  int available = 0; bool dirty = true;
  for (;;) {
    if (dirty && available >= 0) {
      draw(window.pixels(static_cast<uint32_t>(available)), window.format() == sc::kDisplayBgrx);
      if (window.present(static_cast<uint32_t>(available), {0, 0, width, height}) != charm::Status::Ok) return 1;
      available = -1; dirty = false;
    }
    wm::WindowEvent e{}; auto s = window.wait(&e);
    if (s == charm::Status::Interrupted) continue;
    if (s != charm::Status::Ok || e.type == wm::kCloseRequest) return 0;
    if (e.type == wm::kFrameDone) available = static_cast<int>(e.value);
    if (e.type == wm::kInput) { sc::InputEvent event{}; while (window.next_input(&event)) if (input(event)) dirty = true; }
  }
}
