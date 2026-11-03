#pragma once
#include <charm/handle.hpp>

namespace charm {
// Borrowed capability. A PID is diagnostic metadata, never permission to open.
class ProcessView {
 public:
  explicit ProcessView(Handle handle):handle_(handle) {}
  Handle handle() const { return handle_; }
  Status query(abi::process::ProcessInfo* information) const {
    return information ? process_info(handle_,information) : Status::InvalidArgs;
  }
  Status terminate() const { return process_kill(handle_); }
  Status wait(uint64_t deadline,int64_t* exit_code=nullptr) const {
    abi::process::ProcessInfo information{};
    auto status=query(&information);
    if(status!=Status::Ok) return status;
    if(information.state==2) {
      if(exit_code) *exit_code=information.exit_code;
      return Status::Ok;
    }
    if(deadline!=kForever && now_ns()>=deadline) return Status::TimedOut;
    auto created=port_create();
    if(!created.ok()) return created.status;
    auto port=UniqueHandle::adopt(static_cast<Handle>(created.value));
    status=object_bind(port.get(),handle_,1);
    if(status!=Status::Ok) return status;
    struct Binding {
      Handle port,object;
      ~Binding() { (void)object_unbind(port,object,1); }
    } binding{port.get(),handle_};
    PortPacket packet{};
    status=port_wait(port.get(),&packet,deadline);
    if(status!=Status::Ok) return status;
    if(packet.key!=1 || packet.type!=abi::syscall::kPacketProcessExited) return Status::InvalidArgs;
    status=query(&information);
    if(status!=Status::Ok) return status;
    if(information.state!=2) return Status::InvalidArgs;
    if(exit_code) *exit_code=information.exit_code;
    return Status::Ok;
  }
 private:
  Handle handle_;
};
}  // namespace charm
