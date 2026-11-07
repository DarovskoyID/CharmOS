#include "build/charmos/input_translator_charmos.h"

#include <algorithm>
#include <limits>
#include "base/time/time.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"

namespace ui {
namespace {
base::TimeTicks Timestamp(const abi::syscall::InputEvent& input) {
  return base::TimeTicks() + base::Nanoseconds(
      std::min(input.time_ns,static_cast<uint64_t>(INT64_MAX)));
}
uint32_t RussianCharacter(uint32_t code,bool upper) {
  constexpr uint32_t letters[] = {
      0x444,0x438,0x441,0x432,0x443,0x430,0x43f,0x440,0x448,
      0x43e,0x43b,0x434,0x44c,0x442,0x449,0x437,0x439,0x43a,
      0x44b,0x435,0x433,0x43c,0x446,0x447,0x43d,0x44f};
  uint32_t character=code>=4 && code<=29 ? letters[code-4] :
      code==0x2f ? 0x445 : code==0x30 ? 0x44a : code==0x33 ? 0x436 :
      code==0x34 ? 0x44d : code==0x35 ? 0x451 : code==0x36 ? 0x431 :
      code==0x37 ? 0x44e : 0;
  return upper && character ? (character==0x451 ? 0x401 : character-0x20) : character;
}
int MouseFlag(uint32_t code) {
  switch(code) {
    case 1: return EF_LEFT_MOUSE_BUTTON;
    case 2: return EF_RIGHT_MOUSE_BUTTON;
    case 3: return EF_MIDDLE_MOUSE_BUTTON;
    default: return 0;
  }
}
}
void InputTranslatorCharmOS::ResetPressedState() {
  modifiers_=0;
  flags_ &= EF_CAPS_LOCK_ON;
  mouse_flags_=0;
}
std::vector<std::unique_ptr<Event>> InputTranslatorCharmOS::Translate(
    const abi::syscall::InputEvent& input) {
  std::vector<std::unique_ptr<Event>> events;
  if(input.type==abi::syscall::kInputKey) TranslateKey(input,events);
  else if(input.type==abi::syscall::kInputMotion || input.type==abi::syscall::kInputButton ||
          input.type==abi::syscall::kInputWheel) TranslateMouse(input,events);
  return events;
}
void InputTranslatorCharmOS::TranslateKey(const abi::syscall::InputEvent& input,
                                          std::vector<std::unique_ptr<Event>>& events) {
  if(!input.code || input.code>0xe7 || input.value<0 || input.value>2) return;
  const bool pressed=input.value!=0;
  // DomCode contains both the HID page and usage; a bare usage is never a DomCode.
  const DomCode code=KeycodeConverter::UsbKeycodeToDomCode(0x070000u|input.code);
  if(code==DomCode::NONE) return;
  if(input.code>=0xe0) {
    const uint8_t bit=static_cast<uint8_t>(1u<<(input.code-0xe0));
    if(pressed) modifiers_|=bit; else modifiers_&=static_cast<uint8_t>(~bit);
  }
  flags_ &= EF_CAPS_LOCK_ON;
  if(modifiers_&0x11) flags_|=EF_CONTROL_DOWN;
  if(modifiers_&0x22) flags_|=EF_SHIFT_DOWN;
  if(modifiers_&0x04) flags_|=EF_ALT_DOWN;
  if(modifiers_&0x40) flags_|=EF_ALTGR_DOWN;
  if(modifiers_&0x88) flags_|=EF_COMMAND_DOWN;
  if(input.code==0x39 && input.value==1) flags_^=EF_CAPS_LOCK_ON;
  KeyboardCode key_code=DomCodeToUsLayoutKeyboardCode(code);
  DomKey key=DomKey::NONE;
  if(!DomCodeToUsLayoutDomKey(code,flags_,&key,&key_code)) return;
  const bool upper=((flags_&EF_SHIFT_DOWN)!=0)!=((flags_&EF_CAPS_LOCK_ON)!=0);
  if(russian_) {
    if(uint32_t character=RussianCharacter(input.code,upper))
      key=DomKey::FromCharacter(character);
  }
  int flags=flags_|mouse_flags_;
  if(input.value==2) flags|=EF_IS_REPEAT;
  auto raw=std::make_unique<KeyEvent>(pressed ? EventType::kKeyPressed : EventType::kKeyReleased,
      key_code,code,flags,key,Timestamp(input));
  raw->set_scan_code(input.code);
  events.push_back(std::move(raw));
  if(pressed && key.IsCharacter() &&
     !(flags_&(EF_CONTROL_DOWN|EF_ALT_DOWN|EF_ALTGR_DOWN|EF_COMMAND_DOWN))) {
    events.push_back(std::make_unique<KeyEvent>(KeyEvent::FromCharacter(
        static_cast<char16_t>(key.ToCharacter()),key_code,code,flags,Timestamp(input))));
  }
}
void InputTranslatorCharmOS::TranslateMouse(const abi::syscall::InputEvent& input,
                                            std::vector<std::unique_ptr<Event>>& events) {
  const gfx::Point location(input.x,input.y);
  if(input.type==abi::syscall::kInputWheel) {
    if(!input.value) return;
    const int offset=static_cast<int>(std::clamp<int64_t>(
        int64_t{input.value}*MouseWheelEvent::kWheelDelta,INT32_MIN,INT32_MAX));
    events.push_back(std::make_unique<MouseWheelEvent>(gfx::Vector2d(0,offset),
        location,location,Timestamp(input),flags_|mouse_flags_,0));
    return;
  }
  EventType type=mouse_flags_ ? EventType::kMouseDragged : EventType::kMouseMoved;
  int changed=0;
  if(input.type==abi::syscall::kInputButton) {
    changed=MouseFlag(input.code);
    if(!changed || input.value<0 || input.value>1) return;
    type=input.value ? EventType::kMousePressed : EventType::kMouseReleased;
    if(input.value) mouse_flags_|=changed; else mouse_flags_&=~changed;
  }
  events.push_back(std::make_unique<MouseEvent>(type,location,location,Timestamp(input),
                                              flags_|mouse_flags_,changed));
}
}  // namespace ui
