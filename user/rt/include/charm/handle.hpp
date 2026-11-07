#pragma once
#include <charm/sys.hpp>

namespace charm {
// get() borrows; release() transfers. Ownership never implies extra authority.
class UniqueHandle {
 public:
  UniqueHandle()=default;
  ~UniqueHandle() { reset(); }
  UniqueHandle(const UniqueHandle&)=delete;
  UniqueHandle& operator=(const UniqueHandle&)=delete;
  UniqueHandle(UniqueHandle&& other) noexcept :handle_(other.release()) {}
  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if(this!=&other) reset(other.release());
    return *this;
  }
  static UniqueHandle adopt(Handle handle) { return UniqueHandle(handle); }
  Handle get() const { return handle_; }
  bool is_valid() const { return handle_!=0; }
  explicit operator bool() const { return handle_!=0; }
  Handle release() { Handle result=handle_; handle_=0; return result; }
  void reset(Handle replacement=0) {
    if(handle_ && handle_!=replacement) (void)handle_close(handle_);
    handle_=replacement;
  }
  Status duplicate(uint32_t requested_rights,UniqueHandle* output) const {
    if(!output) return Status::InvalidArgs;
    auto result=handle_duplicate(handle_,requested_rights);
    if(!result.ok()) return result.status;
    output->reset(static_cast<Handle>(result.value));
    return Status::Ok;
  }
  Status duplicate_same(UniqueHandle* output) const {
    if(!output) return Status::InvalidArgs;
    auto information=handle_rights(handle_);
    if(!information.ok()) return information.status;
    return duplicate(static_cast<uint32_t>(information.value),output);
  }
 private:
  explicit UniqueHandle(Handle handle):handle_(handle) {}
  Handle handle_=0;
};
}  // namespace charm
