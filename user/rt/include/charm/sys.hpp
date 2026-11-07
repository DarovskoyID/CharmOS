#pragma once
// Обёртки системных вызовов (docs/abi/syscalls.md). Соглашение: номер в RAX,
// аргументы в RDI, RSI, RDX, R10, R8, R9; статус в RAX, значение в RDX;
// RCX и R11 портит сама инструкция SYSCALL, остальное ядро сохраняет.

#include <stddef.h>
#include <stdint.h>

#include <abi/fs.hpp>
#include <abi/process.hpp>
#include <abi/syscall.hpp>

namespace charm {

using abi::syscall::Number;
using abi::syscall::Status;

struct SysResult {
  Status status;
  uint64_t value;
  bool ok() const { return status == Status::Ok; }
};

inline SysResult syscall0(Number n) {
  uint64_t rax = static_cast<uint64_t>(n), rdx;
  asm volatile("syscall" : "+a"(rax), "=d"(rdx) : : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall1(Number n, uint64_t a1) {
  uint64_t rax = static_cast<uint64_t>(n), rdx;
  asm volatile("syscall" : "+a"(rax), "=d"(rdx) : "D"(a1) : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall2(Number n, uint64_t a1, uint64_t a2) {
  uint64_t rax = static_cast<uint64_t>(n), rdx;
  asm volatile("syscall" : "+a"(rax), "=d"(rdx) : "D"(a1), "S"(a2) : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall3(Number n, uint64_t a1, uint64_t a2, uint64_t a3) {
  uint64_t rax = static_cast<uint64_t>(n), rdx = a3;
  asm volatile("syscall" : "+a"(rax), "+d"(rdx) : "D"(a1), "S"(a2) : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall5(Number n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                          uint64_t a5) {
  uint64_t rax = static_cast<uint64_t>(n), rdx = a3;
  register uint64_t r10 asm("r10") = a4;
  register uint64_t r8 asm("r8") = a5;
  asm volatile("syscall"
               : "+a"(rax), "+d"(rdx)
               : "D"(a1), "S"(a2), "r"(r10), "r"(r8)
               : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall6(Number n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                          uint64_t a5, uint64_t a6) {
  uint64_t rax = static_cast<uint64_t>(n), rdx = a3;
  register uint64_t r10 asm("r10") = a4;
  register uint64_t r8 asm("r8") = a5;
  register uint64_t r9 asm("r9") = a6;
  asm volatile("syscall"
               : "+a"(rax), "+d"(rdx)
               : "D"(a1), "S"(a2), "r"(r10), "r"(r8), "r"(r9)
               : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

// Номер, которого нет в ABI (для проверки NotSupported).
inline SysResult raw_syscall(uint64_t number) { return syscall0(static_cast<Number>(number)); }

inline SysResult abi_version() { return syscall0(Number::AbiVersion); }
inline SysResult realtime() { return syscall0(Number::ClockRealtime); }
inline SysResult runtime_query(uint64_t selector) { return syscall1(Number::RuntimeQuery, selector); }
inline SysResult random_read(void* buf, size_t size, uint64_t flags = 0) {
  return syscall3(Number::RandomRead, reinterpret_cast<uint64_t>(buf), size, flags);
}

inline SysResult debug_write(const void* buf, uint64_t len) {
  return syscall2(Number::DebugWrite, reinterpret_cast<uint64_t>(buf), len);
}

inline size_t length(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

inline void print(const char* s) { debug_write(s, length(s)); }

[[noreturn]] inline void exit(int64_t code) {
  syscall1(Number::ProcessExit, static_cast<uint64_t>(code));
  __builtin_unreachable();
}

inline void yield() { syscall0(Number::ThreadYield); }
inline uint64_t now_ns() { return syscall0(Number::ClockMonotonic).value; }
inline void sleep_until(uint64_t deadline_ns) { syscall1(Number::Nanosleep, deadline_ns); }

// --- v1.1: хендлы, каналы, VMO (docs/abi/ipc.md) ---------------------------------

namespace rights = abi::syscall::rights;
using Handle = uint32_t;

inline Status handle_close(Handle h) { return syscall1(Number::HandleClose, h).status; }
inline SysResult handle_rights(Handle h) { return syscall1(Number::HandleRights,h); }
inline SysResult handle_duplicate(Handle h, uint32_t r) {
  return syscall2(Number::HandleDuplicate, h, r);
}
inline SysResult handle_revoke(Handle h) { return syscall1(Number::HandleRevoke, h); }

inline Status channel_create(Handle pair[2]) {
  return syscall1(Number::ChannelCreate, reinterpret_cast<uint64_t>(pair)).status;
}
inline Status channel_write(Handle h, const void* bytes, uint64_t len, const Handle* handles,
                            uint64_t count) {
  return syscall5(Number::ChannelWrite, h, reinterpret_cast<uint64_t>(bytes), len,
                  reinterpret_cast<uint64_t>(handles), count)
      .status;
}
// value: (байт << 32) | хендлов.
inline SysResult channel_read(Handle h, void* bytes, uint64_t cap, Handle* handles,
                              uint64_t hcap, uint64_t deadline) {
  return syscall6(Number::ChannelRead, h, reinterpret_cast<uint64_t>(bytes), cap,
                  reinterpret_cast<uint64_t>(handles), hcap, deadline);
}
inline constexpr uint64_t kForever = abi::syscall::kInfiniteDeadline;
inline Status channel_query(Handle endpoint,abi::syscall::ChannelState* output) {
  return syscall2(Number::ChannelQuery,endpoint,reinterpret_cast<uint64_t>(output)).status;
}
inline Status channel_shutdown(Handle endpoint,uint32_t directions=3) {
  return syscall2(Number::ChannelShutdown,endpoint,directions).status;
}

inline SysResult vmo_create(uint64_t size) { return syscall1(Number::VmoCreate, size); }
inline SysResult vmo_reserve(uint64_t size) { return syscall1(Number::VmoReserve, size); }
inline SysResult vmo_size(Handle h) { return syscall1(Number::VmoSize,h); }
inline SysResult thread_priority(uint32_t priority=0) { return syscall1(Number::ThreadPriority,priority); }
inline SysResult vmo_map(Handle h, uint32_t r) { return syscall2(Number::VmoMap, h, r); }
inline Status vmo_unmap(uint64_t addr) { return syscall1(Number::VmoUnmap, addr).status; }
inline Status vmo_unmap_range(uint64_t addr, uint64_t size) {
  return syscall2(Number::VmoUnmapRange, addr, size).status;
}
inline Status vmo_protect(uint64_t addr, uint64_t size, uint32_t r) {
  return syscall3(Number::VmoProtect, addr, size, r).status;
}

// --- v1.2: порты (docs/abi/driver.md §4) -----------------------------------------
using abi::syscall::PortPacket;
inline SysResult port_create() { return syscall0(Number::PortCreate); }
inline Status port_wait(Handle port, PortPacket* out, uint64_t deadline) {
  return syscall3(Number::PortWait, port, reinterpret_cast<uint64_t>(out), deadline).status;
}
inline Status object_bind(Handle port, Handle h, uint64_t key) {
  return syscall3(Number::ObjectBind, port, h, key).status;
}
inline Status object_unbind(Handle port, Handle h, uint64_t key) {
  return syscall3(Number::ObjectUnbind, port, h, key).status;
}
inline Status object_watch(Handle port, Handle h, uint64_t key, uint32_t mask) {
  return syscall5(Number::ObjectWatch, port, h, key, mask, 0).status;
}
inline SysResult fs_identity(Handle h) { return syscall1(Number::FsIdentity,h); }

// --- v1.3: файлы и каталоги (docs/abi/fs.md) -----------------------------------------
inline SysResult fs_open(Handle dir, const char* path, uint64_t len, uint64_t flags) {
  return syscall5(Number::FsOpen, dir, reinterpret_cast<uint64_t>(path), len, flags, 0);
}
inline SysResult fs_open(Handle dir, const char* path, uint64_t flags) {
  return fs_open(dir, path, length(path), flags);
}
inline SysResult file_read(Handle f, void* buf, uint64_t len, uint64_t offset) {
  return syscall5(Number::FileRead, f, reinterpret_cast<uint64_t>(buf), len, offset, 0);
}
inline SysResult file_write(Handle f, const void* buf, uint64_t len, uint64_t offset) {
  return syscall5(Number::FileWrite, f, reinterpret_cast<uint64_t>(buf), len, offset, 0);
}
inline Status file_sync(Handle f) { return syscall1(Number::FileSync, f).status; }
inline Status fs_stat(Handle h, abi::fs::Stat* out) {
  return syscall2(Number::FsStat, h, reinterpret_cast<uint64_t>(out)).status;
}
inline SysResult dir_read(Handle dir, void* buf, uint64_t cap, uint64_t cookie) {
  return syscall5(Number::DirRead, dir, reinterpret_cast<uint64_t>(buf), cap, cookie, 0);
}
inline Status fs_unlink(Handle dir, const char* path) {
  return syscall3(Number::FsUnlink, dir, reinterpret_cast<uint64_t>(path), length(path)).status;
}
inline Status fs_unlink_typed(Handle dir,const char* path,bool directory) {
  return syscall5(Number::FsUnlinkTyped,dir,reinterpret_cast<uint64_t>(path),length(path),directory ? 1u : 0u,0).status;
}
inline Status fs_rename(Handle source_dir,const char* source,Handle target_dir,const char* target) {
  return syscall6(Number::FsRename,source_dir,reinterpret_cast<uint64_t>(source),length(source),
                   target_dir,reinterpret_cast<uint64_t>(target),length(target)).status;
}
inline Status file_resize(Handle f, uint64_t size) { return syscall2(Number::FileResize, f, size).status; }

// --- v1.4: сеть (docs/abi/socket.md) -----------------------------------------------------
struct SockAddr {
  uint32_t ipv4;  // порядок сети
  uint16_t port;  // порядок хоста
  uint16_t reserved;
};
inline constexpr uint64_t kSockTcp = 1;
inline SysResult socket_open(Handle net, uint64_t type) { return syscall2(Number::SocketOpen, net, type); }
inline Status socket_bind(Handle s, const SockAddr* a) {
  return syscall2(Number::SocketBind, s, reinterpret_cast<uint64_t>(a)).status;
}
inline Status socket_listen(Handle s, uint64_t backlog) { return syscall2(Number::SocketListen, s, backlog).status; }
inline SysResult socket_accept(Handle s, SockAddr* peer, uint64_t deadline) {
  return syscall3(Number::SocketAccept, s, reinterpret_cast<uint64_t>(peer), deadline);
}
inline Status socket_connect(Handle s, const SockAddr* a, uint64_t deadline) {
  return syscall3(Number::SocketConnect, s, reinterpret_cast<uint64_t>(a), deadline).status;
}
inline SysResult socket_send(Handle s, const void* buf, uint64_t len) {
  return syscall5(Number::SocketSend, s, reinterpret_cast<uint64_t>(buf), len, 0, 0);
}
inline SysResult socket_recv(Handle s, void* buf, uint64_t cap, uint64_t deadline) {
  return syscall5(Number::SocketRecv, s, reinterpret_cast<uint64_t>(buf), cap, 0, deadline);
}
inline Status socket_shutdown(Handle s, uint64_t how) { return syscall2(Number::SocketShutdown, s, how).status; }

// DMA-буфер устройства (docs/abi/driver.md §5): VMO и адрес для устройства.
inline SysResult dma_alloc(Handle dev, uint64_t size, uint64_t flags, uint64_t* device_addr) {
  return syscall5(Number::DmaAlloc, dev, size, flags, reinterpret_cast<uint64_t>(device_addr), 0);
}


// --- v1.5: потоки (docs/abi/process.md §4) ---------------------------------------
// entry стартует с RSP = stack_top (выровнен на 16), RDI = arg.
inline SysResult thread_create(void (*entry)(uint64_t), uint64_t stack_top, uint64_t arg) {
  return syscall3(Number::ThreadCreate, reinterpret_cast<uint64_t>(entry), stack_top, arg);
}
[[noreturn]] inline void thread_exit(int64_t code) {
  syscall1(Number::ThreadExit, static_cast<uint64_t>(code));
  __builtin_unreachable();
}
[[noreturn]] inline void process_exit(int64_t code) {
  syscall1(Number::ProcessExit, static_cast<uint64_t>(code));
  __builtin_unreachable();
}
inline Status thread_info(Handle t, abi::syscall::ThreadInfo* out) {
  return syscall2(Number::ThreadInfo, t, reinterpret_cast<uint64_t>(out)).status;
}
inline Status futex_wait(const volatile uint32_t* addr, uint32_t expected, uint64_t deadline) {
  return syscall3(Number::FutexWait, reinterpret_cast<uint64_t>(addr), expected, deadline).status;
}
inline SysResult futex_wake(const volatile uint32_t* addr, uint64_t count) {
  return syscall2(Number::FutexWake, reinterpret_cast<uint64_t>(addr), count);
}
inline Status thread_set_fs(uint64_t base) { return syscall1(Number::ThreadSetFs, base).status; }


// --- v1.5: процессы и Job (docs/abi/process.md §3, §5) --------------------------
inline SysResult process_spawn(const abi::process::SpawnArgs* a) {
  return syscall1(Number::ProcessSpawn, reinterpret_cast<uint64_t>(a));
}
inline Status process_kill(Handle p) { return syscall1(Number::ProcessKill, p).status; }
inline Status process_info(Handle p, abi::process::ProcessInfo* out) {
  return syscall2(Number::ProcessInfo, p, reinterpret_cast<uint64_t>(out)).status;
}
inline SysResult job_create(Handle parent) { return syscall1(Number::JobCreate, parent); }
inline Status job_kill(Handle j) { return syscall1(Number::JobKill, j).status; }
inline Status job_info(Handle j, abi::process::JobInfo* out) {
  return syscall2(Number::JobInfo, j, reinterpret_cast<uint64_t>(out)).status;
}
inline Status console_set_foreground(Handle con, Handle job) {
  return syscall2(Number::ConsoleSetForeground, con, job).status;
}
inline SysResult console_read(Handle con, void* buf, uint64_t cap, uint64_t deadline) {
  return syscall5(Number::ConsoleRead, con, reinterpret_cast<uint64_t>(buf), cap, deadline, 0);
}
inline SysResult console_write(Handle con, const void* buf, uint64_t len) {
  return syscall3(Number::ConsoleWrite, con, reinterpret_cast<uint64_t>(buf), len);
}

}  // namespace charm
