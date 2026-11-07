#ifndef CHARMOS_INPUT_TRANSLATOR_H_
#define CHARMOS_INPUT_TRANSLATOR_H_

#include <charm/window.hpp>
#include <memory>
#include <vector>
#include "ui/events/event.h"

namespace ui {

class InputTranslatorCharmOS {
 public:
  InputTranslatorCharmOS() = default;
  void set_russian(bool russian) { russian_ = russian; }
  bool russian() const { return russian_; }
  std::vector<std::unique_ptr<Event>> Translate(const abi::syscall::InputEvent& input);
  void ResetPressedState();

 private:
  void TranslateKey(const abi::syscall::InputEvent& input,std::vector<std::unique_ptr<Event>>& events);
  void TranslateMouse(const abi::syscall::InputEvent& input,std::vector<std::unique_ptr<Event>>& events);
  uint8_t modifiers_ = 0;
  int flags_ = 0;
  int mouse_flags_ = 0;
  bool russian_ = false;
};
}  // namespace ui
#endif
