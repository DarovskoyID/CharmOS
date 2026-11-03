#pragma once
#include <charm/handle.hpp>

namespace charm {
inline constexpr uint32_t kApplicationSdkVersion=0x00010000;
enum class MemoryAccess { ReadOnly, ReadWrite };

class Mapping {
 public:
  Mapping()=default;
  ~Mapping() { reset(); }
  Mapping(const Mapping&)=delete;
  Mapping& operator=(const Mapping&)=delete;
  Mapping(Mapping&& other) noexcept :address_(other.address_),size_(other.size_) {
    other.address_=nullptr; other.size_=0;
  }
  Mapping& operator=(Mapping&& other) noexcept {
    if(this!=&other) {
      reset(); address_=other.address_; size_=other.size_;
      other.address_=nullptr; other.size_=0;
    }
    return *this;
  }
  void* data() const { return address_; }
  size_t size() const { return size_; }
  explicit operator bool() const { return address_!=nullptr; }
  void reset() {
    if(address_) (void)vmo_unmap(reinterpret_cast<uint64_t>(address_));
    address_=nullptr; size_=0;
  }
  void* release() { void* result=address_; address_=nullptr; size_=0; return result; }
 private:
  friend class SharedMemory;
  Mapping(void* address,size_t size):address_(address),size_(size) {}
  void* address_=nullptr;
  size_t size_=0;
};

class SharedMemory {
 public:
  SharedMemory()=default;
  SharedMemory(const SharedMemory&)=delete;
  SharedMemory& operator=(const SharedMemory&)=delete;
  SharedMemory(SharedMemory&& other) noexcept
      :handle_(static_cast<UniqueHandle&&>(other.handle_)),size_(other.size_) { other.size_=0; }
  SharedMemory& operator=(SharedMemory&& other) noexcept {
    if(this!=&other) {
      handle_=static_cast<UniqueHandle&&>(other.handle_);
      size_=other.size_; other.size_=0;
    }
    return *this;
  }
  static Status create(size_t bytes,SharedMemory* output) {
    if(!output || !bytes || bytes>abi::syscall::kVmoMaxSize) return Status::InvalidArgs;
    size_t rounded=(bytes+4095)&~size_t{4095};
    auto object=vmo_create(rounded);
    if(!object.ok()) return object.status;
    auto owner=UniqueHandle::adopt(static_cast<Handle>(object.value));
    return adopt(static_cast<UniqueHandle&&>(owner),output);
  }
  // Failed adoption preserves both the incoming owner and output object.
  static Status adopt(UniqueHandle&& incoming,SharedMemory* output) {
    if(!output) return Status::InvalidArgs;
    auto information=vmo_size(incoming.get());
    if(!information.ok()) return information.status;
    output->handle_=static_cast<UniqueHandle&&>(incoming);
    output->size_=static_cast<size_t>(information.value);
    return Status::Ok;
  }
  Status map(MemoryAccess access,Mapping* output) const {
    if(!output || (access!=MemoryAccess::ReadOnly && access!=MemoryAccess::ReadWrite)) return Status::InvalidArgs;
    uint32_t requested=rights::kRead;
    if(access==MemoryAccess::ReadWrite) requested|=rights::kWrite;
    auto result=vmo_map(handle_.get(),requested);
    if(!result.ok()) return result.status;
    *output=Mapping(reinterpret_cast<void*>(result.value),size_);
    return Status::Ok;
  }
  Status share(uint32_t requested_rights,UniqueHandle* output) const {
    return handle_.duplicate(requested_rights,output);
  }
  Handle handle() const { return handle_.get(); }
  size_t size() const { return size_; }
  explicit operator bool() const { return static_cast<bool>(handle_); }
  void reset() { handle_.reset(); size_=0; }
 private:
  UniqueHandle handle_;
  size_t size_=0;
};
}  // namespace charm
