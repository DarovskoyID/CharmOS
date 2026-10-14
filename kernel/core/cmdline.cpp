#include "cmdline.hpp"

namespace kernel {

using lib::core::StringView;

StringView Cmdline::get(StringView key, bool* found) const {
  size_t i = 0;
  if (found != nullptr) *found = false;
  while (i < text_.size()) {
    while (i < text_.size() && text_[i] == ' ') ++i;
    size_t start = i;
    while (i < text_.size() && text_[i] != ' ') ++i;
    StringView token = text_.substr(start, i - start);
    if (token.empty()) continue;
    size_t eq = 0;
    while (eq < token.size() && token[eq] != '=') ++eq;
    if (token.substr(0, eq) == key) {
      if (found != nullptr) *found = true;
      return eq < token.size() ? token.substr(eq + 1) : StringView();
    }
  }
  return StringView();
}

bool Cmdline::has(StringView key) const {
  bool found = false;
  get(key, &found);
  return found;
}

}  // namespace kernel
