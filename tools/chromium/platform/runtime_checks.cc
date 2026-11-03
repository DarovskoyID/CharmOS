#include "build/build_config.h"
#include "third_party/perfetto/include/perfetto/base/thread_utils.h"
#include "third_party/perfetto/include/perfetto/ext/base/paged_memory.h"
#include <charm/libc.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <locale>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <errno.h>
#include <fenv.h>
#include <langinfo.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <unistd.h>
#include <charm/sys.hpp>
#include <charm/memory.hpp>
#include <charm/process.hpp>
#include <charm/stream.hpp>
#include <memory>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <libgen.h>
#include <fnmatch.h>
#include <dirent.h>

static unsigned failures;
#define CHECK(c) do { if(!(c)) { printf("cxx runtime: FAILED line %d\n",__LINE__); ++failures; } } while(0)
#define REQUIRE(c) do { if(!(c)) { printf("cxx runtime: FAILED required line %d\n",__LINE__); return 1; } } while(0)
#define REPORT(message) do { printf("cxx runtime: " message " %s\n", failures==group_start ? "ok" : "FAILED"); group_start=failures; } while(0)
static unsigned handler_calls;
static volatile int alarm_seen;
static void note_alarm(int) { alarm_seen=1; }
static void out_of_memory() { ++handler_calls; std::set_new_handler(nullptr); }
int TestNativeCompilerTls();
int RunNativeCompilerTlsExitChild();
extern "C" int TestNativeNonlocalJumps();
int TestNativeSemaphores();
extern "C" int main(int argc, char** argv) {
  if(argc==2 && !strcmp(argv[1],"compiler-tls-exit")) return RunNativeCompilerTlsExitChild();
  if(argc==2 && !strcmp(argv[1],"startup-memory")) {
    std::atomic<unsigned> successful_takes{0};
    std::atomic<uint32_t> taken_handle{0};
    std::thread takers[4];
    for(auto& taker:takers) taker=std::thread([&] {
      uint32_t taken=charm_take_start_handle(0x40000001u);
      if(taken) { taken_handle.store(taken); successful_takes.fetch_add(1); }
    });
    for(auto& taker:takers) taker.join();
    auto taken_owner=charm::UniqueHandle::adopt(taken_handle.load());
    if(successful_takes.load()!=1 || charm_take_start_handle(0x40000001u) ||
       charm_extra_handle(0) || charm_take_start_handle(abi::process::kRoleRoot)) return 80;
    auto taken_rights=charm::handle_rights(taken_owner.get());
    if(!taken_rights.ok() || (taken_rights.value&charm::rights::kWrite)) return 81;
    charm::SharedMemory taken_memory;
    if(charm::SharedMemory::adopt(std::move(taken_owner),&taken_memory)!=charm::Status::Ok) return 82;
    charm::Mapping taken_mapping;
    if(taken_memory.map(charm::MemoryAccess::ReadOnly,&taken_mapping)!=charm::Status::Ok ||
       static_cast<const char*>(taken_mapping.data())[0]!=73) return 83;
    return 40;
  }
  if(argc==2 && !strcmp(argv[1],"process-delay")) { usleep(300000); return 37; }
  if(argc==2 && !strcmp(argv[1],"env-limit")) {
    unsigned count=0;
    while(environ && environ[count]) ++count;
    return count==abi::process::kMaxEnv ? 38 : 39;
  }
  if (argc==2 && (!strcmp(argv[1],"map-readonly") || !strcmp(argv[1],"map-file-readonly"))) {
    int source_fd=-1;
    int map_flags=MAP_PRIVATE|MAP_ANONYMOUS;
    size_t map_size=4096;
    if (!strcmp(argv[1],"map-file-readonly")) {
      source_fd=open("/etc/hello.txt",O_RDONLY);
      if (source_fd<0) return 95;
      map_flags=MAP_PRIVATE;
      map_size=5;
    }
    void* readonly=mmap(nullptr,map_size,PROT_READ,map_flags,source_fd,0);
    if (source_fd>=0) (void)close(source_fd);
    if (readonly==MAP_FAILED) return 96;
    static_cast<volatile char*>(readonly)[0]=1;
    return 97;
  }
  if (argc==2 && (!strcmp(argv[1],"guard-before") || !strcmp(argv[1],"guard-after"))) {
    auto guarded=perfetto::base::PagedMemory::Allocate(8193);
    auto* boundary=static_cast<volatile char*>(guarded.Get());
    if (!strcmp(argv[1],"guard-before")) boundary[-1]=1;
    else boundary[12288]=1;
    return 97; // A successful write means the guard failed.
  }
  unsigned group_start=0;
  {
    double (*volatile hyperbolic[])(double)={::sinh,::cosh,::tanh,::asinh,::acosh,::atanh};
    const double inputs[]={1,1,1,1,2,0.5};
    const double expected[]={1.1752011936438014,1.5430806348152437,0.7615941559557649,
                             0.8813735870195430,1.3169578969248167,0.5493061443340548};
    for(unsigned i=0;i<6;++i) CHECK(std::fabs(hyperbolic[i](inputs[i])-expected[i])<2e-15);
    CHECK(std::signbit(hyperbolic[0](-0.0)) && std::signbit(hyperbolic[2](-0.0)) &&
          std::signbit(hyperbolic[3](-0.0)) && std::signbit(hyperbolic[5](-0.0)));
    CHECK(hyperbolic[2](INFINITY)==1 && hyperbolic[2](-INFINITY)==-1);
    errno=0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(std::isnan(hyperbolic[4](0.5)) && errno==EDOM && (fetestexcept(FE_INVALID)&FE_INVALID));
    errno=0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(std::isinf(hyperbolic[5](1)) && errno==ERANGE && (fetestexcept(FE_DIVBYZERO)&FE_DIVBYZERO));
    errno=0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(std::isinf(hyperbolic[0](1000)) && errno==ERANGE && (fetestexcept(FE_OVERFLOW)&FE_OVERFLOW));
    feclearexcept(FE_ALL_EXCEPT);
    REPORT("fdlibm double hyperbolic functions, signed zero, domains and overflow");
  }
  {
    CHECK(malloc_usable_size(nullptr)==0);
    void* small=malloc(73);
    REQUIRE(small);
    size_t capacity=malloc_usable_size(small);
    CHECK(capacity==112);
    memset(small,0x6b,capacity);
    void* grown=realloc(small,capacity+1);
    REQUIRE(grown);
    CHECK(malloc_usable_size(grown)>=capacity+1);
    for(size_t i=0;i<capacity;++i) CHECK(static_cast<unsigned char*>(grown)[i]==0x6b);
    free(grown);
    void* aligned=nullptr;
    REQUIRE(posix_memalign(&aligned,256,513)==0);
    CHECK(malloc_usable_size(aligned)==768);
    memset(aligned,0x4d,malloc_usable_size(aligned));
    free(aligned);
    void* large=malloc(70000);
    REQUIRE(large);
    CHECK(malloc_usable_size(large)==73712);
    memset(large,0x37,malloc_usable_size(large));
    free(large);
    REPORT("native allocator payload capacities and realloc preservation");
  }
  {
    CHECK(TestNativeNonlocalJumps()==0);
    REPORT("nonlocal jumps, nested stacks, thread isolation and floating environment");
  }
  {
    int semaphore_status=TestNativeSemaphores();
    if(semaphore_status) printf("cxx runtime: semaphore failure %d\n",semaphore_status);
    CHECK(semaphore_status==0);
    REPORT("native counting semaphores, signal posting, deadlines and token handoff");
  }
  {
    int tls_status=TestNativeCompilerTls();
    if(tls_status) printf("cxx runtime: compiler TLS failure %d\n",tls_status);
    CHECK(tls_status==0);
    REPORT("compiler TLS isolation, alignment, cross-unit access and destructor ordering");
  }
  {
    size_t before_heap=charm_heap_allocated_bytes();
    void* small_allocation=malloc(73);
    REQUIRE(small_allocation);
    static_cast<volatile char*>(small_allocation)[0]=1;
    CHECK(charm_heap_allocated_bytes()==before_heap+128);
    void* large_allocation=malloc(70000);
    REQUIRE(large_allocation);
    static_cast<volatile char*>(large_allocation)[0]=2;
    CHECK(charm_heap_allocated_bytes()==before_heap+128+73728);
    free(large_allocation);
    CHECK(charm_heap_allocated_bytes()==before_heap+128);
    free(small_allocation);
    CHECK(charm_heap_allocated_bytes()==before_heap);
    void* reused_allocation=malloc(73);
    REQUIRE(reused_allocation);
    static_cast<volatile char*>(reused_allocation)[0]=3;
    CHECK(charm_heap_allocated_bytes()==before_heap+128);
    free(reused_allocation);
    void* aligned_allocation=aligned_alloc(4096,8192);
    REQUIRE(aligned_allocation);
    static_cast<volatile char*>(aligned_allocation)[0]=4;
    CHECK(charm_heap_allocated_bytes()>before_heap+8192);
    free(aligned_allocation);
    CHECK(charm_heap_allocated_bytes()==before_heap);
    int original_descriptors=charm_open_fd_count();
    int counted_descriptor=dup(0);
    REQUIRE(counted_descriptor>=0);
    CHECK(charm_open_fd_count()==original_descriptors+1);
    CHECK(close(counted_descriptor)==0 && charm_open_fd_count()==original_descriptors);
    REPORT("native live allocator capacities and descriptor accounting");
  }
  {
    charm_spawn_opts limits_options{};
    char* oversized_args[abi::process::kMaxArgs+2];
    for(unsigned i=0;i<abi::process::kMaxArgs+1;++i) oversized_args[i]=const_cast<char*>("x");
    oversized_args[abi::process::kMaxArgs+1]=nullptr;
    errno=0;
    CHECK(charm_spawn("/bin/runtime_cpp",oversized_args,nullptr,&limits_options)==-1 && errno==E2BIG);
    char* env_args[]={const_cast<char*>("runtime_cpp"),const_cast<char*>("env-limit"),nullptr};
    char* oversized_environment[abi::process::kMaxEnv+2];
    for(unsigned i=0;i<abi::process::kMaxEnv+1;++i) oversized_environment[i]=const_cast<char*>("A=1");
    oversized_environment[abi::process::kMaxEnv+1]=nullptr;
    errno=0;
    CHECK(charm_spawn("/bin/runtime_cpp",env_args,oversized_environment,&limits_options)==-1 && errno==E2BIG);
    oversized_environment[abi::process::kMaxEnv]=nullptr;
    int64_t env_child=charm_spawn("/bin/runtime_cpp",env_args,oversized_environment,&limits_options);
    REQUIRE(env_child>0);
    auto env_owner=charm::UniqueHandle::adopt(static_cast<charm::Handle>(env_child));
    int64_t env_code=0;
    CHECK(charm::ProcessView(env_owner.get()).wait(charm::kForever,&env_code)==charm::Status::Ok && env_code==38);
    uint32_t excessive_handles[abi::process::kMaxStartHandlesV2];
    for(auto& handle:excessive_handles) handle=charm_root();
    limits_options.give=CHARM_GIVE_ROOT;
    limits_options.extra_count=abi::process::kMaxStartHandlesV2;
    limits_options.extra=excessive_handles;
    errno=0;
    CHECK(charm_spawn("/bin/runtime_cpp",env_args,nullptr,&limits_options)==-1 && errno==E2BIG);
    CHECK(charm::handle_rights(charm_root()).ok());
    REPORT("spawn argument/environment limits and excess-handle failure cleanup");
  }
  {
    char* process_args[]={const_cast<char*>("runtime_cpp"),const_cast<char*>("process-delay"),nullptr};
    charm_spawn_opts process_options{};
    int64_t process_handle=charm_spawn("/bin/runtime_cpp",process_args,nullptr,&process_options);
    REQUIRE(process_handle>0);
    auto process_owner=charm::UniqueHandle::adopt(static_cast<charm::Handle>(process_handle));
    charm::UniqueHandle read_only_process;
    REQUIRE(process_owner.duplicate(charm::rights::kRead|charm::rights::kDuplicate,&read_only_process)==charm::Status::Ok);
    auto process_rights=charm::handle_rights(read_only_process.get());
    CHECK(process_rights.ok() && process_rights.value==(charm::rights::kRead|charm::rights::kDuplicate));
    charm::UniqueHandle copied_process;
    CHECK(read_only_process.duplicate_same(&copied_process)==charm::Status::Ok);
    charm::ProcessView process(read_only_process.get());
    CHECK(process.terminate()==charm::Status::AccessDenied);
    abi::process::ProcessInfo process_information{};
    CHECK(process.query(&process_information)==charm::Status::Ok && process_information.pid!=static_cast<uint64_t>(getpid()));
    unsigned cancelled_process_waits=0;
    for(unsigned attempt=0;attempt<16;++attempt) {
      int64_t unchanged_exit=12345;
      auto wait_status=process.wait(charm::now_ns()+1000000,&unchanged_exit);
      if(wait_status==charm::Status::Ok) break;
      CHECK(wait_status==charm::Status::TimedOut && unchanged_exit==12345);
      if(wait_status==charm::Status::TimedOut) ++cancelled_process_waits;
    }
    CHECK(cancelled_process_waits>0);
    int64_t exit_code=0;
    CHECK(process.wait(charm::kForever,&exit_code)==charm::Status::Ok && exit_code==37);
    CHECK(process.wait(0,&exit_code)==charm::Status::Ok && exit_code==37);
    CHECK(process.query(&process_information)==charm::Status::Ok && process_information.state==2);
    CHECK(charm::handle_rights(0).status==charm::Status::BadHandle);
    REPORT("shared process API, real capability rights and cancelled deadline waits");
  }
  {
    using charm::Status;
    charm::SharedMemory memory;
    REQUIRE(charm::SharedMemory::create(4097,&memory)==Status::Ok && memory.size()==8192);
    charm::Mapping writable;
    REQUIRE(memory.map(charm::MemoryAccess::ReadWrite,&writable)==Status::Ok);
    auto* bytes=static_cast<unsigned char*>(writable.data());
    CHECK(writable.size()==8192 && bytes[0]==0 && bytes[8191]==0);
    bytes[0]=73; bytes[8191]=99;
    charm::UniqueHandle restricted;
    REQUIRE(memory.share(charm::rights::kRead|charm::rights::kMap|charm::rights::kTransfer,&restricted)==Status::Ok);
    charm::SharedMemory readonly;
    REQUIRE(charm::SharedMemory::adopt(std::move(restricted),&readonly)==Status::Ok && !restricted && readonly.size()==8192);
    void* original_address=writable.data();
    CHECK(readonly.map(charm::MemoryAccess::ReadWrite,&writable)==Status::AccessDenied && writable.data()==original_address);
    charm::Mapping readable;
    REQUIRE(readonly.map(charm::MemoryAccess::ReadOnly,&readable)==Status::Ok);
    CHECK(static_cast<const unsigned char*>(readable.data())[0]==73);
    auto raw_port=charm::port_create(); REQUIRE(raw_port.ok());
    auto wrong=charm::UniqueHandle::adopt(static_cast<charm::Handle>(raw_port.value));
    auto old_handle=memory.handle();
    CHECK(charm::SharedMemory::adopt(std::move(wrong),&memory)==Status::WrongType && wrong && memory.handle()==old_handle);
    CHECK(charm::SharedMemory::create(0,&memory)==Status::InvalidArgs && memory.handle()==old_handle);
    auto moved_mapping=std::move(writable);
    CHECK(!writable && moved_mapping.data()==bytes);
    auto moved_memory=std::move(memory);
    CHECK(!memory && memory.size()==0 && moved_memory.handle()==old_handle);
    moved_memory.reset(); readonly.reset();
    CHECK(bytes[0]==73 && static_cast<const unsigned char*>(readable.data())[8191]==99);
    readable.reset();
    void* released=moved_mapping.release();
    CHECK(!moved_mapping && charm::vmo_unmap(reinterpret_cast<uint64_t>(released))==Status::Ok);
    auto bad_size=charm::vmo_size(0);
    CHECK(bad_size.status==Status::BadHandle && bad_size.value==0);
    REPORT("shared application SDK ownership, immutable sizes and restricted memory rights");
  }
  {
    auto saved_priority=charm::thread_priority(),priority_ceiling=charm::runtime_query(8);
    REQUIRE(saved_priority.ok() && priority_ceiling.ok() && saved_priority.value>=1 && saved_priority.value<=31);
    auto lowered=charm::thread_priority(4);
    REQUIRE(lowered.ok());
    CHECK(lowered.value==std::min<uint64_t>(4,priority_ceiling.value));
    uint64_t inherited_priority=0,worker_priority=0;
    std::thread priority_worker([&] {
      inherited_priority=charm::thread_priority().value;
      worker_priority=charm::thread_priority(24).value;
    });
    priority_worker.join();
    CHECK(inherited_priority==lowered.value && worker_priority==std::min<uint64_t>(24,priority_ceiling.value));
    CHECK(charm::thread_priority().value==lowered.value);
    auto restored=charm::thread_priority(static_cast<uint32_t>(saved_priority.value));
    CHECK(restored.ok() && restored.value==saved_priority.value);
    CHECK(charm::thread_priority(32).status==charm::Status::InvalidArgs && charm::thread_priority().value==saved_priority.value);
    REPORT("native current-thread priorities, inheritance and independent worker updates");
  }
  static_assert(BUILDFLAG(IS_CHARMOS) && !BUILDFLAG(IS_LINUX));
  static_assert(!_LIBCPP_HAS_FILESYSTEM && _LIBCPP_HAS_LOCALIZATION && _LIBCPP_HAS_WIDE_CHARACTERS);
  static_assert(sizeof(fenv_t)==32 && sizeof(fexcept_t)==2);
  {
    auto memory=perfetto::base::PagedMemory::Allocate(8193);
    REQUIRE(memory.IsValid() && memory.size()==8193);
    auto* data=static_cast<unsigned char*>(memory.Get());
    CHECK((reinterpret_cast<uintptr_t>(data)&4095)==0);
    bool zero=true;
    for (size_t i=0;i<8193;++i) if (data[i]) zero=false;
    CHECK(zero);
    data[0]=11; data[8192]=29;
    auto moved=std::move(memory);
    CHECK(!memory.IsValid() && moved.IsValid() && moved.Get()==data);
    auto assigned=perfetto::base::PagedMemory::Allocate(4096);
    assigned=std::move(moved);
    CHECK(!moved.IsValid() && assigned.Get()==data && data[0]==11 && data[8192]==29);
    CHECK(!assigned.AdviseDontNeed(data,4096));
    CHECK(!perfetto::base::PagedMemory::Allocate(SIZE_MAX,perfetto::base::PagedMemory::kMayFail).IsValid());
    for (const char* side:{"guard-before","guard-after"}) {
      char* child_argv[]={const_cast<char*>("runtime_cpp"),const_cast<char*>(side),nullptr};
      charm_spawn_opts options{};
      int64_t child=charm_spawn("/bin/runtime_cpp",child_argv,nullptr,&options);
      REQUIRE(child>0);
      int64_t child_code=0;
      CHECK(charm_wait(static_cast<uint32_t>(child),&child_code)==0 && child_code==-1000);
      CHECK(charm::handle_close(static_cast<uint32_t>(child))==charm::Status::Ok);
    }
  }
  REPORT("native Perfetto VMO memory, move ownership and guard-page faults");
  {
    void* anonymous_mapping=mmap(nullptr,8193,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    REQUIRE(anonymous_mapping!=MAP_FAILED);
    auto* mapped_bytes=static_cast<unsigned char*>(anonymous_mapping);
    bool fresh_zero=true;
    for (size_t i=0;i<8193;++i) if (mapped_bytes[i]) fresh_zero=false;
    CHECK(fresh_zero);
    mapped_bytes[0]=17; mapped_bytes[8192]=93;
    errno=0; CHECK(munmap(anonymous_mapping,4096)==-1 && errno==ENOTSUP && mapped_bytes[8192]==93);
    CHECK(mprotect(anonymous_mapping,8193,PROT_READ|PROT_WRITE)==0);
    errno=0; CHECK(mprotect(anonymous_mapping,8193,PROT_NONE)==-1 && errno==ENOTSUP);
    CHECK(madvise(anonymous_mapping,8193,MADV_NORMAL)==0);
    errno=0; CHECK(madvise(anonymous_mapping,8193,MADV_DONTNEED)==-1 && errno==ENOTSUP);
    CHECK(munmap(anonymous_mapping,8193)==0);
    errno=0; CHECK(munmap(anonymous_mapping,8193)==-1 && errno==EINVAL);
    errno=0; CHECK(mmap(nullptr,0,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED && errno==EINVAL);
    errno=0; CHECK(mmap(nullptr,SIZE_MAX,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED && errno==ENOMEM);
    errno=0; CHECK(mmap(nullptr,4096,PROT_READ,MAP_SHARED|MAP_ANONYMOUS,-1,0)==MAP_FAILED && errno==ENOTSUP);
    errno=0; CHECK(mmap(nullptr,4096,PROT_READ|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED && errno==ENOTSUP);
    char map_name[]="/runtime-map-XXXXXX";
    int map_fd=mkstemp(map_name); REQUIRE(map_fd>=0);
    std::vector<unsigned char> original_bytes(8192);
    for (size_t i=0;i<original_bytes.size();++i) original_bytes[i]=static_cast<unsigned char>(i%251);
    CHECK(write(map_fd,original_bytes.data(),original_bytes.size())==static_cast<ssize_t>(original_bytes.size()));
    CHECK(lseek(map_fd,7,SEEK_SET)==7);
    void* read_snapshot=mmap(nullptr,5,PROT_READ,MAP_PRIVATE,map_fd,4096);
    REQUIRE(read_snapshot!=MAP_FAILED);
    CHECK(!memcmp(read_snapshot,original_bytes.data()+4096,5) && lseek(map_fd,0,SEEK_CUR)==7);
    void* private_snapshot=mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE,map_fd,0);
    REQUIRE(private_snapshot!=MAP_FAILED);
    static_cast<unsigned char*>(private_snapshot)[0]=201;
    CHECK(msync(private_snapshot,8192,MS_SYNC)==0);
    unsigned char unchanged_byte=255;
    CHECK(pread(map_fd,&unchanged_byte,1,0)==1 && unchanged_byte==original_bytes[0]);
    errno=0; CHECK(mmap(nullptr,5,PROT_READ,MAP_PRIVATE,map_fd,1)==MAP_FAILED && errno==EINVAL);
    errno=0; CHECK(mmap(nullptr,8193,PROT_READ,MAP_PRIVATE,map_fd,0)==MAP_FAILED && errno==EINVAL);
    CHECK(close(map_fd)==0 && unlink(map_name)==0);
    CHECK(!memcmp(read_snapshot,original_bytes.data()+4096,5));
    CHECK(munmap(read_snapshot,5)==0 && munmap(private_snapshot,8192)==0);
    for (const char* map_mode:{"map-readonly","map-file-readonly"}) {
      char* map_argv[]={const_cast<char*>("runtime_cpp"),const_cast<char*>(map_mode),nullptr};
      charm_spawn_opts map_options{}; map_options.give=CHARM_GIVE_ROOT;
      int64_t map_child=charm_spawn("/bin/runtime_cpp",map_argv,nullptr,&map_options);
      REQUIRE(map_child>0);
      int64_t map_code=0;
      CHECK(charm_wait(static_cast<uint32_t>(map_child),&map_code)==0 && map_code==-1000);
      CHECK(charm::handle_close(static_cast<uint32_t>(map_child))==charm::Status::Ok);
    }
  }
  REPORT("native anonymous/private-file mappings, unchanged offsets and read-only faults");
  {
    charm::Handle channel[2];
    REQUIRE(charm::channel_create(channel)==charm::Status::Ok);
    auto watch_port=charm::port_create(); REQUIRE(watch_port.ok());
    auto port_handle=static_cast<charm::Handle>(watch_port.value);
    auto readonly_port=charm::handle_duplicate(port_handle,charm::rights::kRead);
    REQUIRE(readonly_port.ok());
    CHECK(charm::object_watch(static_cast<charm::Handle>(readonly_port.value),channel[0],1,1)==charm::Status::AccessDenied);
    CHECK(charm::handle_close(static_cast<charm::Handle>(readonly_port.value))==charm::Status::Ok);
    for (unsigned i=0;i<100;++i) {
      CHECK(charm::object_watch(port_handle,channel[0],i+1,1)==charm::Status::Ok);
      CHECK(charm::object_unbind(port_handle,channel[0],i+2)==charm::Status::NotFound);
      CHECK(charm::object_unbind(port_handle,channel[0],i+1)==charm::Status::Ok);
    }
    CHECK(charm::object_watch(port_handle,channel[0],1,0)==charm::Status::InvalidArgs);
    CHECK(charm::object_watch(port_handle,channel[0],1,4)==charm::Status::InvalidArgs);
    CHECK(charm::channel_write(channel[1],"ready",5,nullptr,0)==charm::Status::Ok);
    CHECK(charm::object_watch(port_handle,channel[0],42,1)==charm::Status::Ok);
    charm::PortPacket event{};
    CHECK(charm::port_wait(port_handle,&event,0)==charm::Status::Ok && event.key==42 &&
          event.type==abi::syscall::kPacketChannelReadable);
    CHECK(charm::object_unbind(port_handle,channel[0],42)==charm::Status::NotFound);
    CHECK(charm::handle_close(channel[0])==charm::Status::Ok &&
          charm::handle_close(channel[1])==charm::Status::Ok &&
          charm::handle_close(port_handle)==charm::Status::Ok);
    int endpoints[2]; REQUIRE(pipe2(endpoints,O_NONBLOCK|O_CLOEXEC)==0);
    CHECK(fcntl(endpoints[0],F_GETFD)==FD_CLOEXEC && fcntl(endpoints[1],F_GETFD)==FD_CLOEXEC);
    char small[8]{};
    errno=0; CHECK(read(endpoints[0],small,1)==-1 && errno==EAGAIN);
    struct stat pipe_info{};
    CHECK(fstat(endpoints[0],&pipe_info)==0 && S_ISFIFO(pipe_info.st_mode));
    pollfd read_event{endpoints[0],POLLIN,0};
    for (unsigned i=0;i<20;++i) CHECK(poll(&read_event,1,1)==0 && !read_event.revents);
    auto previous_alarm=signal(SIGALRM,&note_alarm); REQUIRE(previous_alarm!=SIG_ERR);
    alarm_seen=0; alarm(1);
    errno=0; CHECK(poll(&read_event,1,2000)==-1 && errno==EINTR && alarm_seen==1);
    alarm(0); CHECK(signal(SIGALRM,previous_alarm)!=SIG_ERR);
    CHECK(poll(&read_event,1,0)==0);
    pollfd write_event{endpoints[1],POLLOUT,0};
    CHECK(poll(&write_event,1,0)==1 && (write_event.revents&POLLOUT));
    for (unsigned i=0;i<64;++i) CHECK(write(endpoints[1],"a",1)==1);
    errno=0; CHECK(write(endpoints[1],"b",1)==-1 && errno==EAGAIN);
    CHECK(poll(&write_event,1,0)==0);
    CHECK(poll(&read_event,1,0)==1 && (read_event.revents&POLLIN));
    CHECK(read(endpoints[0],small,1)==1 && small[0]=='a');
    CHECK(poll(&write_event,1,0)==1 && (write_event.revents&POLLOUT));
    CHECK(write(endpoints[1],"b",1)==1);
    CHECK(fcntl(endpoints[1],F_SETFL,0)==0);
    std::atomic<int> blocked_write{0};
    std::thread blocked_writer([&] { blocked_write.store(static_cast<int>(write(endpoints[1],"wake",4))); });
    usleep(2000);
    CHECK(blocked_write.load()==0);
    CHECK(read(endpoints[0],small,1)==1);
    blocked_writer.join(); CHECK(blocked_write.load()==4);
    CHECK(close(endpoints[1])==0);
    size_t drained=0;
    for (;;) { ssize_t amount=read(endpoints[0],small,sizeof small); REQUIRE(amount>=0); if (!amount) break; drained+=static_cast<size_t>(amount); }
    CHECK(drained==67);
    CHECK(poll(&read_event,1,0)==1 && (read_event.revents&POLLHUP));
    CHECK(close(endpoints[0])==0);
    REQUIRE(pipe(endpoints)==0);
    int alias=dup(endpoints[0]); REQUIRE(alias>=0);
    CHECK(write(endpoints[1],"abcdef",6)==6 && read(endpoints[0],small,2)==2);
    pollfd aliases[]={{endpoints[0],POLLIN,0},{alias,POLLIN,0},{-1,POLLIN,0},{255,POLLIN,0}};
    CHECK(poll(aliases,4,0)==3 && (aliases[0].revents&POLLIN) &&
          (aliases[1].revents&POLLIN) && !aliases[2].revents && (aliases[3].revents&POLLNVAL));
    CHECK(close(endpoints[0])==0 && read(alias,small,4)==4 && !memcmp(small,"cdef",4));
    std::thread delayed_writer([&] { usleep(2000); (void)write(endpoints[1],"later",5); });
    read_event={alias,POLLIN,0};
    CHECK(poll(&read_event,1,1000)==1 && (read_event.revents&POLLIN));
    delayed_writer.join();
    CHECK(read(alias,small,5)==5 && !memcmp(small,"later",5));
    CHECK(close(alias)==0);
    write_event={endpoints[1],POLLOUT,0};
    CHECK(poll(&write_event,1,0)==1 && (write_event.revents&POLLERR));
    errno=0; CHECK(write(endpoints[1],"x",1)==-1 && errno==EPIPE);
    CHECK(close(endpoints[1])==0);
    int unchanged[2]={123,456};
    errno=0; CHECK(pipe2(unchanged,O_APPEND)==-1 && errno==EINVAL && unchanged[0]==123 && unchanged[1]==456);
    errno=0; CHECK(poll(nullptr,65,0)==-1 && errno==EINVAL);
    CHECK(poll(nullptr,0,1)==0);
    REQUIRE(pipe(endpoints)==0);
    std::thread producers[2];
    for (unsigned writer=0;writer<2;++writer) producers[writer]=std::thread([&,writer] {
      char record[1024]; memset(record,static_cast<int>('A'+writer),sizeof record);
      iovec pieces[]={{record,17},{nullptr,0},{record+17,sizeof record-17}};
      for (unsigned batch=0;batch<32;++batch)
        if (writev(endpoints[1],pieces,3)!=sizeof record) return;
    });
    std::vector<char> records(65536);
    size_t consumed=0;
    while (consumed<records.size()) {
      size_t amount=records.size()-consumed;
      if (amount>37) amount=37;
      ssize_t got=read(endpoints[0],records.data()+consumed,amount);
      REQUIRE(got>0); consumed+=static_cast<size_t>(got);
    }
    for (auto& producer:producers) producer.join();
    unsigned a_records=0,b_records=0;
    bool atomic_records=true;
    for (size_t offset=0;offset<records.size();offset+=1024) {
      char expected=records[offset];
      if (expected=='A') ++a_records; else if (expected=='B') ++b_records; else atomic_records=false;
      for (size_t byte=1;byte<1024;++byte) if (records[offset+byte]!=expected) atomic_records=false;
    }
    CHECK(atomic_records && a_records==32 && b_records==32);
    CHECK(close(endpoints[0])==0 && close(endpoints[1])==0);
  }
  REPORT("cancellable watches, pipe backpressure, shared reads, poll timeouts and EOF");
  {
    static_assert(sizeof(pthread_cond_t)==8 && sizeof(pthread_attr_t)==16);
    pthread_attr_t attributes;
    REQUIRE(pthread_attr_init(&attributes)==0);
    size_t requested_stack=0;
    CHECK(pthread_attr_getstacksize(&attributes,&requested_stack)==0 && requested_stack==256*1024);
    CHECK(pthread_attr_setstacksize(&attributes,PTHREAD_STACK_MIN-1)==EINVAL);
    CHECK(pthread_attr_setstacksize(&attributes,512*1024+1)==0);
    pthread_t custom_thread;
    REQUIRE(pthread_create(&custom_thread,&attributes,+[](void*)->void* {
      volatile unsigned char large_stack[320*1024];
      for (size_t position=0;position<sizeof large_stack;position+=4096) large_stack[position]=37;
      unsigned sum=0;
      for (size_t position=0;position<sizeof large_stack;position+=4096) sum+=large_stack[position];
      return reinterpret_cast<void*>(static_cast<uintptr_t>(sum));
    },nullptr)==0);
    void* custom_result=nullptr;
    CHECK(pthread_join(custom_thread,&custom_result)==0 && reinterpret_cast<uintptr_t>(custom_result)==37*80);
    CHECK(pthread_attr_destroy(&attributes)==0);
    CHECK(pthread_join(pthread_self(),nullptr)==EDEADLK);
    long free_before_detached=sysconf(_SC_AVPHYS_PAGES);
    std::atomic<unsigned> finished_detached{0};
    for (unsigned batch=0;batch<16;++batch) {
      finished_detached.store(0);
      for (unsigned i=0;i<8;++i) {
        std::thread detached_worker([&] { finished_detached.fetch_add(1); });
        detached_worker.detach();
      }
      uint64_t limit=charm::now_ns()+2000000000;
      while (finished_detached.load()!=8 && charm::now_ns()<limit) usleep(1000);
      REQUIRE(finished_detached.load()==8);
      usleep(1000);
    }
    REQUIRE(pthread_attr_init(&attributes)==0 && pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED)==0);
    pthread_t detached_attribute_thread;
    REQUIRE(pthread_create(&detached_attribute_thread,&attributes,+[](void* argument)->void* {
      static_cast<std::atomic<unsigned>*>(argument)->fetch_add(1);
      return nullptr;
    },&finished_detached)==0);
    uint64_t detach_limit=charm::now_ns()+2000000000;
    while (finished_detached.load()!=9 && charm::now_ns()<detach_limit) usleep(1000);
    REQUIRE(finished_detached.load()==9);
    CHECK(pthread_attr_destroy(&attributes)==0);
    usleep(20000);
    // A bounded reaper/allocator footprint is permitted; 129 leaked 256 KiB
    // stacks would exceed this bound by more than an order of magnitude.
    CHECK(sysconf(_SC_AVPHYS_PAGES)+512>=free_before_detached);
    pthread_condattr_t condition_attributes;
    REQUIRE(pthread_condattr_init(&condition_attributes)==0);
    CHECK(pthread_condattr_setclock(&condition_attributes,CLOCK_MONOTONIC)==0);
    CHECK(pthread_condattr_setclock(&condition_attributes,CLOCK_MONOTONIC_RAW)==EINVAL);
    clockid_t selected_clock=-1;
    CHECK(pthread_condattr_getclock(&condition_attributes,&selected_clock)==0 && selected_clock==CLOCK_MONOTONIC);
    pthread_cond_t monotonic_condition;
    pthread_mutex_t monotonic_mutex=PTHREAD_MUTEX_INITIALIZER;
    REQUIRE(pthread_cond_init(&monotonic_condition,&condition_attributes)==0);
    timespec monotonic_deadline{};
    CHECK(clock_gettime(CLOCK_MONOTONIC,&monotonic_deadline)==0);
    monotonic_deadline.tv_nsec+=5000000;
    if (monotonic_deadline.tv_nsec>=1000000000) { ++monotonic_deadline.tv_sec; monotonic_deadline.tv_nsec-=1000000000; }
    CHECK(pthread_mutex_lock(&monotonic_mutex)==0);
    uint64_t wait_start=charm::now_ns();
    int wait_result;
    do { wait_result=pthread_cond_timedwait(&monotonic_condition,&monotonic_mutex,&monotonic_deadline); } while (!wait_result);
    CHECK(wait_result==ETIMEDOUT && charm::now_ns()-wait_start>=3000000);
    CHECK(pthread_mutex_trylock(&monotonic_mutex)==EBUSY && pthread_mutex_unlock(&monotonic_mutex)==0);
    CHECK(pthread_cond_destroy(&monotonic_condition)==0 && pthread_condattr_destroy(&condition_attributes)==0);
  }
  REPORT("custom pthread stacks, detached reclamation and monotonic conditions");
  fenv_t original;
  CHECK(fegetenv(&original)==0 && fesetround(FE_TONEAREST)==0);
  char* end=nullptr;
  CHECK(strtod("0x1.8p+1tail",&end)==3.0 && !strcmp(end,"tail"));
  CHECK(strtof("-0",nullptr)==0.0f && std::signbit(strtof("-0",nullptr)));
  CHECK(strtold("1.0000000000000000001084202172485504434",nullptr)>1.0L);
  CHECK(std::isinf(strtod("infinity",nullptr)) && std::isnan(strtod("NaN",nullptr)));
  errno=0; CHECK(std::isinf(strtod("1e9999",nullptr)) && errno==ERANGE);
  errno=0; CHECK(strtod("1e-9999",nullptr)==0 && errno==ERANGE);
  CHECK(fesetround(FE_TONEAREST)==0 && strtod("0x1.00000000000008p0",nullptr)==1.0);
  CHECK(fesetround(FE_UPWARD)==0 && strtod("0x1.00000000000008p0",nullptr)==0x1.0000000000001p0);
  CHECK(fesetround(FE_DOWNWARD)==0 && strtod("0x1.00000000000008p0",nullptr)==1.0);
  CHECK(fesetround(FE_TONEAREST)==0 && feclearexcept(FE_ALL_EXCEPT)==0);
  CHECK(feraiseexcept(FE_DIVBYZERO|FE_INEXACT)==0);
  CHECK((fetestexcept(FE_ALL_EXCEPT)&(FE_DIVBYZERO|FE_INEXACT))==(FE_DIVBYZERO|FE_INEXACT));
  CHECK(feclearexcept(FE_DIVBYZERO)==0 && !(fetestexcept(FE_ALL_EXCEPT)&FE_DIVBYZERO));
  fexcept_t flags;
  CHECK(fegetexceptflag(&flags,FE_ALL_EXCEPT)==0 && feclearexcept(FE_ALL_EXCEPT)==0);
  CHECK(fesetexceptflag(&flags,FE_INEXACT)==0 && (fetestexcept(FE_ALL_EXCEPT)&FE_INEXACT));
  CHECK(fesetenv(&original)==0);
  volatile double input=0.5;
  CHECK(std::fabs(std::sin(input)-0.479425538604203)<1e-15);
  CHECK(std::fabs(std::cos(input)-0.8775825618903728)<1e-15);
  CHECK(std::fabs(std::log(std::exp(input))-input)<1e-14);
  CHECK(std::sqrt(81.0)==9.0 && std::pow(3.0,4.0)==81.0);
  errno=0; volatile double negative=-1;
  double (*volatile root)(double)=&::sqrt;
  CHECK(std::isnan(root(negative)) && errno==EDOM);
  REPORT("parsing, range, rounding, fenv and numeric functions");

  std::string text="Привет";
  for(unsigned i=0;i<100;++i) text+=" / Chromium";
  CHECK(text.find("Chromium")!=std::string::npos && std::stoi("123")==123 && std::stod("2.5")==2.5);
  CHECK(std::to_wstring(123)==L"123");
  auto wide=std::to_wstring(1.25);
  CHECK(wcstod(wide.c_str(),nullptr)==1.25);
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output<<std::fixed<<std::setprecision(2)<<1234.125;
  CHECK(output.str()=="1234.12");
  std::istringstream incoming("17 2.5");
  int integer=0; double real=0; incoming>>integer>>real;
  CHECK(integer==17 && real==2.5 && !incoming.fail());
  char bytes[80];
  CHECK(snprintf(bytes,sizeof bytes,"%.17g / %La",1.25,1.5L)>0 && !strncmp(bytes,"1.25 / ",7));
  CHECK(strtold(bytes+7,nullptr)==1.5L);
  long double scanned=0;
  CHECK(sscanf("0x1.8p+1","%Lf",&scanned)==1 && scanned==3.0L);
  char* allocated=nullptr;
  CHECK(asprintf(&allocated,"%ls / %.2f",L"Привет",1.25)>0 && !strcmp(allocated,"Привет / 1.25"));
  free(allocated);
  CHECK(std::set_new_handler(&out_of_memory)==nullptr);
  volatile size_t impossible=static_cast<size_t>(-1);
  CHECK(::operator new(impossible,std::nothrow)==nullptr && handler_calls==1);
  CHECK(std::get_new_handler()==nullptr);
  REPORT("strings, locale, streams, formatting and new-handler");

  CHECK(uselocale(nullptr)==LC_GLOBAL_LOCALE);
  locale_t selected=newlocale(LC_ALL_MASK,"C.UTF-8",nullptr);
  CHECK(selected && uselocale(selected)==LC_GLOBAL_LOCALE);
  CHECK(!strcmp(nl_langinfo_l(CODESET,selected),"UTF-8"));
  errno=0; CHECK(!newlocale(LC_ALL_MASK,"ru_RU.UTF-8",nullptr) && errno==ENOENT);
  CHECK(uselocale(nullptr)==selected);
  CHECK(fesetround(FE_DOWNWARD)==0);
  std::atomic<unsigned> status{0};
  std::thread worker([&] {
    if(uselocale(nullptr)!=LC_GLOBAL_LOCALE || fegetround()!=FE_DOWNWARD) { status=1; return; }
    if(fesetround(FE_UPWARD)) { status=2; return; }
    for(unsigned i=0;i<100;++i) { std::this_thread::yield(); if(fegetround()!=FE_UPWARD) { status=3; return; } }
    status=4;
  });
  bool stable=true;
  for(unsigned i=0;i<100;++i) { std::this_thread::yield(); stable&=fegetround()==FE_DOWNWARD; }
  CHECK(stable);
  worker.join(); CHECK(status==4);
  CHECK(fesetenv(&original)==0 && uselocale(LC_GLOBAL_LOCALE)==selected);
  freelocale(selected);
  std::mutex mutex; std::condition_variable condition; bool ready=false;
  std::thread signal([&] { std::lock_guard<std::mutex> lock(mutex); ready=true; condition.notify_one(); });
  { std::unique_lock<std::mutex> lock(mutex); CHECK(condition.wait_for(lock,std::chrono::seconds(2),[&]{return ready;})); }
  signal.join();
  auto before=std::chrono::steady_clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CHECK(std::chrono::steady_clock::now()>before);
  std::random_device entropy;
  unsigned random=entropy(); bool different=false;
  for(unsigned i=0;i<8;++i) different|=entropy()!=random;
  CHECK(different);
  REPORT("C++ threads, fenv inheritance, locale isolation, clocks and entropy");

  FILE* file=fopen("/runtime-wide.txt","w+"); REQUIRE(file); CHECK(fwide(file,0)==0);
  CHECK(fputwc(L'Я',file)==L'Я' && fputwc(L'\U0001f600',file)==L'\U0001f600' && fputwc(L'!',file)==L'!');
  CHECK(fflush(file)==0 && fseeko(file,0,SEEK_SET)==0);
  CHECK(fgetwc(file)==L'Я'); fpos_t position; REQUIRE(fgetpos(file,&position)==0);
  CHECK(fgetwc(file)==L'\U0001f600' && ungetwc(L'\U0001f600',file)==L'\U0001f600');
  CHECK(fgetwc(file)==L'\U0001f600' && fgetwc(file)==L'!' && fgetwc(file)==WEOF && feof(file));
  CHECK(fsetpos(file,&position)==0 && !feof(file) && fgetwc(file)==L'\U0001f600');
  CHECK(fclose(file)==0);
  file=fopen("/runtime-wide.txt","r"); REQUIRE(file);
  char raw[16]{}; CHECK(fread(raw,1,sizeof raw,file)==7 && !strcmp(raw,"Я😀!"));
  CHECK(fclose(file)==0 && remove("/runtime-wide.txt")==0);
  time_t epoch=1609459200; tm date; REQUIRE(gmtime_r(&epoch,&date));
  CHECK(strftime(bytes,sizeof bytes,"%F %T %G-W%V-%u %Z",&date)>0 && !strcmp(bytes,"2021-01-01 00:00:00 2020-W53-5 UTC"));
  REPORT("wide file I/O, positions, UTF-8 bytes and UTC formatting");
  {
    char directory[]="/runtime-temp-XXXXXX";
    REQUIRE(mkdtemp(directory)==directory);
    CHECK(access(directory,F_OK)==0 && access(directory,R_OK)==0);
    char names[4][96]{};
    int temporary_fds[4]{-1,-1,-1,-1};
    std::thread creators[4];
    for (unsigned i=0;i<4;++i) creators[i]=std::thread([&,i] {
      snprintf(names[i],sizeof names[i],"%s/shared-XXXXXX",directory);
      temporary_fds[i]=mkostemp(names[i],O_CLOEXEC);
    });
    for (auto& creator:creators) creator.join();
    for (unsigned i=0;i<4;++i) {
      REQUIRE(temporary_fds[i]>=0);
      CHECK(fcntl(temporary_fds[i],F_GETFD)==FD_CLOEXEC);
      CHECK(access(names[i],R_OK|W_OK)==0);
      CHECK(write(temporary_fds[i],"persist",7)==7);
      errno=0;
      CHECK(open(names[i],O_CREAT|O_EXCL|O_RDWR,0600)==-1 && errno==EEXIST);
      CHECK(lseek(temporary_fds[i],0,SEEK_SET)==0);
      char content[8]{};
      CHECK(read(temporary_fds[i],content,7)==7 && !strcmp(content,"persist"));
      for (unsigned j=0;j<i;++j) CHECK(strcmp(names[i],names[j])!=0);
      CHECK(close(temporary_fds[i])==0 && unlink(names[i])==0);
    }
    char plain[96]; snprintf(plain,sizeof plain,"%s/plain-XXXXXX",directory);
    int plain_fd=mkstemp(plain); REQUIRE(plain_fd>=0);
    CHECK(fcntl(plain_fd,F_GETFD)==0 && close(plain_fd)==0 && unlink(plain)==0);
    char invalid[]="/invalid-XXXXX";
    errno=0; CHECK(mkstemp(invalid)==-1 && errno==EINVAL);
    char missing[]="/absent-temp-parent/file-XXXXXX";
    errno=0; CHECK(mkstemp(missing)==-1 && errno==ENOENT && !strcmp(missing,"/absent-temp-parent/file-XXXXXX"));
    errno=0; CHECK(access(directory,8)==-1 && errno==EINVAL);
    errno=0; CHECK(access(directory,X_OK)==-1 && errno==ENOTSUP);
    CHECK(rmdir(directory)==0);
    errno=0; CHECK(access(directory,F_OK)==-1 && errno==ENOENT);
  }
  REPORT("exclusive random temporary files and directories, access and concurrent creators");
  for (time_t sample : {time_t{-62135596800LL}, time_t{-1}, time_t{0},
                        time_t{951782400}, time_t{253402300799LL}}) {
    tm civil; REQUIRE(localtime_r(&sample, &civil));
    CHECK(civil.tm_isdst == 0 && mktime(&civil) == sample);
    civil.tm_isdst = 1; // UTC conversion ignores the caller's DST hint.
    CHECK(timegm(&civil) == sample && civil.tm_isdst == 0);
  }
  errno=0; CHECK(timegm(nullptr)==-1 && errno==EINVAL);
  tm invalid_utc{}; invalid_utc.tm_year=8100; invalid_utc.tm_mday=1;
  errno=0; CHECK(timegm(&invalid_utc)==-1 && errno==EOVERFLOW && invalid_utc.tm_year==8100);
  tm rollover{}; rollover.tm_year=120; rollover.tm_mon=12; rollover.tm_mday=0;
  CHECK(mktime(&rollover)==1609372800 && rollover.tm_year==120 && rollover.tm_mon==11 && rollover.tm_mday==31);
  CHECK(difftime(INT64_MAX,INT64_MAX-1)==1.0 && difftime(INT64_MIN,INT64_MIN+1)==-1.0);
  CHECK(timezone==0 && daylight==0 && !strcmp(tzname[0],"UTC"));
  {
    utsname identity{};
    CHECK(uname(&identity)==0 && !strcmp(identity.sysname,"CharmOS") &&
          !strcmp(identity.machine,"x86_64") && !strncmp(identity.release,"abi-",4));
    CHECK(getpagesize()==4096 && sysconf(_SC_PAGE_SIZE)==4096);
    CHECK(sysconf(_SC_NPROCESSORS_ONLN)==static_cast<long>(charm::runtime_query(7).value) &&
          sysconf(_SC_NPROCESSORS_ONLN)>0 && sysconf(_SC_NPROCESSORS_ONLN)<=256);
    CHECK(std::thread::hardware_concurrency()==static_cast<unsigned>(sysconf(_SC_NPROCESSORS_ONLN)));
    long total_pages=sysconf(_SC_PHYS_PAGES), free_pages=sysconf(_SC_AVPHYS_PAGES);
    CHECK(total_pages>0 && free_pages>0 && free_pages<=total_pages);
    auto frame_object=charm::vmo_create(4096*16); REQUIRE(frame_object.ok());
    CHECK(sysconf(_SC_AVPHYS_PAGES)<=total_pages-16);
    CHECK(charm::handle_close(static_cast<charm::Handle>(frame_object.value))==charm::Status::Ok);
    errno=0; CHECK(uname(nullptr)==-1 && errno==EFAULT);
    errno=0; CHECK(sysconf(-1)==-1 && errno==EINVAL);
  }
  {
    tm parsed{};
    const char* date_text="2024-02-29 23:17:04 +0000 tail";
    char* rest=strptime(date_text,"%F %T %z",&parsed);
    CHECK(rest && !strcmp(rest," tail") && parsed.tm_year==124 && parsed.tm_mon==1 &&
          parsed.tm_mday==29 && parsed.tm_yday==59 && parsed.tm_wday==4 &&
          parsed.tm_hour==23 && parsed.tm_min==17 && parsed.tm_sec==4);
    CHECK(strptime("Thu Feb 29 11:17:04 2024","%c",&parsed) && parsed.tm_hour==11);
    CHECK(strptime("12:01:02 AM","%r",&parsed) && parsed.tm_hour==0);
    CHECK(strptime("12:01:02 PM","%r",&parsed) && parsed.tm_hour==12);
    CHECK(strptime("11:01:02 PM","%r",&parsed) && parsed.tm_hour==23);
    CHECK(strptime("68","%y",&parsed) && parsed.tm_year==168);
    CHECK(strptime("69","%y",&parsed) && parsed.tm_year==69);
    CHECK(strptime("19 70","%C %y",&parsed) && parsed.tm_year==70);
    tm untouched=parsed;
    CHECK(!strptime("2023-02-29","%F",&parsed) && !memcmp(&parsed,&untouched,sizeof parsed));
    CHECK(!strptime("2024-13-01","%F",&parsed));
    CHECK(!strptime("24:00:00","%T",&parsed));
    CHECK(!strptime("+0300","%z",&parsed));
    CHECK(!strptime("","%",&parsed) && !strptime("","%F",&parsed));
    CHECK(strptime("UTC","%Z",&parsed) && parsed.tm_isdst==0);
  }
  sigset_t all, signal_original, current, worker_mask=0;
  CHECK(sigfillset(&all)==0 && pthread_sigmask(SIG_BLOCK,nullptr,&signal_original)==0);
  errno=EDOM;
  CHECK(pthread_sigmask(999,&all,nullptr)==EINVAL && errno==EDOM);
  std::thread mask_worker([&] {
    sigset_t previous;
    if(pthread_sigmask(SIG_BLOCK,&all,&previous)!=0) return;
    if(pthread_sigmask(SIG_BLOCK,nullptr,&worker_mask)!=0) worker_mask=0;
    (void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);
  });
  mask_worker.join();
  CHECK(worker_mask==all && pthread_sigmask(SIG_BLOCK,nullptr,&current)==0 && current==signal_original);
  REPORT("UTC normalization, numeric time differences and per-thread signal masks");
  {
    pid_t process_id=getpid(), main_id=gettid();
    CHECK(process_id>0 && main_id>0);
    static_assert(std::is_same_v<perfetto::base::PlatformThreadId, pid_t>);
    CHECK(perfetto::base::GetThreadId()==main_id);
    CHECK(charm::runtime_query(0).value==static_cast<uint64_t>(process_id));
    CHECK(charm::runtime_query(1).value==static_cast<uint64_t>(main_id));
    auto bad_query=charm::runtime_query(UINT64_MAX);
    CHECK(bad_query.status==charm::Status::InvalidArgs && bad_query.value==0);
    CHECK(charm::runtime_query(4).ok() && charm::runtime_query(4).value>0);
    auto cpu_ns=[](clockid_t clock) -> uint64_t {
      timespec sample{};
      if(clock_gettime(clock,&sample)!=0) return UINT64_MAX;
      return static_cast<uint64_t>(sample.tv_sec)*1000000000ull + static_cast<uint64_t>(sample.tv_nsec);
    };
    uint64_t proc_cpu_before=cpu_ns(CLOCK_PROCESS_CPUTIME_ID);
    pid_t ids[4]{};
    uint64_t elapsed[4]{}, work[4]{};
    std::thread busy[4];
    for(unsigned i=0;i<4;++i) busy[i]=std::thread([&,i] {
      ids[i]=getpid()==process_id && perfetto::base::GetThreadId()==gettid() ? gettid() : 0;
      uint64_t begin=cpu_ns(CLOCK_THREAD_CPUTIME_ID);
      uint64_t until=charm::now_ns()+10000000;
      uint64_t sum=i+1;
      while(charm::now_ns()<until) sum=sum*6364136223846793005ull+1;
      work[i]=sum;
      elapsed[i]=cpu_ns(CLOCK_THREAD_CPUTIME_ID)-begin;
    });
    for(auto& t:busy) t.join();
    uint64_t after=cpu_ns(CLOCK_PROCESS_CPUTIME_ID), total=0;
    for(unsigned i=0;i<4;++i) {
      CHECK(ids[i]>0 && ids[i]!=main_id && elapsed[i]>0 && work[i]!=0);
      total+=elapsed[i];
      for(unsigned j=0;j<i;++j) CHECK(ids[i]!=ids[j]);
    }
    CHECK(proc_cpu_before!=UINT64_MAX && after>=proc_cpu_before && after-proc_cpu_before>=total);
    uint64_t wall_begin=charm::now_ns(), cpu_begin=cpu_ns(CLOCK_THREAD_CPUTIME_ID);
    CHECK(usleep(100000)==0);
    uint64_t cpu_end=cpu_ns(CLOCK_THREAD_CPUTIME_ID), wall_end=charm::now_ns();
    CHECK(cpu_end>=cpu_begin && (cpu_end-cpu_begin)*2<wall_end-wall_begin);
    timespec boot{};
    CHECK(clock_gettime(CLOCK_BOOTTIME,&boot)==0);
    errno=0; CHECK(clock_gettime(CLOCK_MONOTONIC_RAW,&boot)==-1 && errno==EINVAL);
    REPORT("kernel process/thread IDs and CPU accounting excluding sleep");
  }
  {
    int fd_file=open("/runtime-fd.txt",O_CREAT|O_TRUNC|O_RDWR|O_CLOEXEC,0600);
    REQUIRE(fd_file>=0);
    CHECK(fcntl(fd_file,F_GETFD)==FD_CLOEXEC);
    CHECK(write(fd_file,"abcdef",6)==6);
    int fd_peer=dup(fd_file); REQUIRE(fd_peer>=0);
    CHECK(fcntl(fd_peer,F_GETFD)==0 && lseek(fd_peer,0,SEEK_SET)==0);
    char fd_text[8]{};
    CHECK(read(fd_file,fd_text,2)==2 && !memcmp(fd_text,"ab",2));
    CHECK(read(fd_peer,fd_text,2)==2 && !memcmp(fd_text,"cd",2));
    CHECK(pread(fd_peer,fd_text,1,0)==1 && fd_text[0]=='a' && lseek(fd_file,0,SEEK_CUR)==4);
    CHECK(close(fd_file)==0 && read(fd_peer,fd_text,2)==2 && !memcmp(fd_text,"ef",2));
    CHECK(fcntl(fd_peer,F_SETFD,FD_CLOEXEC)==0 && dup2(fd_peer,fd_peer)==fd_peer);
    CHECK(fcntl(fd_peer,F_GETFD)==FD_CLOEXEC);
    int fd_copy=fcntl(fd_peer,F_DUPFD,100); REQUIRE(fd_copy>=100);
    int fd_clo=fcntl(fd_peer,F_DUPFD_CLOEXEC,120); REQUIRE(fd_clo>=120);
    CHECK(fcntl(fd_copy,F_GETFD)==0 && fcntl(fd_clo,F_GETFD)==FD_CLOEXEC);
    CHECK(fcntl(fd_peer,F_SETFL,O_APPEND|O_NONBLOCK|O_RDONLY)==0);
    CHECK(fcntl(fd_copy,F_GETFL)==(O_RDWR|O_APPEND|O_NONBLOCK));
    CHECK(lseek(fd_copy,0,SEEK_SET)==0 && write(fd_clo,"!",1)==1 && lseek(fd_peer,0,SEEK_CUR)==7);
    CHECK(fcntl(fd_peer,F_SETFL,O_RDWR)==0 && close(fd_copy)==0 && close(fd_clo)==0);
    CHECK(lseek(fd_peer,INT64_MAX,SEEK_SET)==INT64_MAX);
    errno=0; CHECK(lseek(fd_peer,1,SEEK_CUR)==-1 && errno==EOVERFLOW);
    errno=0; CHECK(lseek(fd_peer,INT64_MIN,SEEK_CUR)==-1 && errno==EINVAL);
    CHECK(lseek(fd_peer,0,SEEK_END)==7);
    errno=0; CHECK(fcntl(fd_peer,F_DUPFD,-1)==-1 && errno==EINVAL);
    flock fd_lock_request{};
    errno=0; CHECK(fcntl(fd_peer,F_SETLK,&fd_lock_request)==-1 && errno==ENOTSUP);
    int fd_target=open("/runtime-fd.txt",O_RDONLY|O_CLOEXEC); REQUIRE(fd_target>=0);
    CHECK(dup2(fd_peer,fd_target)==fd_target && fcntl(fd_target,F_GETFD)==0);
    CHECK(close(fd_target)==0 && pread(fd_peer,fd_text,7,0)==7 && !memcmp(fd_text,"abcdef!",7));
    CHECK(close(fd_peer)==0 && unlink("/runtime-fd.txt")==0);

    int fd_shared=open("/runtime-shared-fd.txt",O_CREAT|O_TRUNC|O_RDWR,0600); REQUIRE(fd_shared>=0);
    int fd_writers[4];
    for(auto& descriptor:fd_writers) { descriptor=dup(fd_shared); REQUIRE(descriptor>=0); }
    std::atomic<unsigned> fd_ready{0}, fd_errors{0};
    std::atomic<bool> fd_start{false};
    std::thread fd_workers[4];
    for(unsigned i=0;i<4;++i) fd_workers[i]=std::thread([&,i] {
      char record[32]; memset(record,static_cast<int>('A'+i),sizeof record);
      fd_ready.fetch_add(1);
      while(!fd_start.load()) std::this_thread::yield();
      for(unsigned j=0;j<32;++j)
        if(write(fd_writers[i],record,sizeof record)!=sizeof record) fd_errors.fetch_add(1);
    });
    while(fd_ready.load()!=4) std::this_thread::yield();
    fd_start.store(true);
    for(auto& t:fd_workers) t.join();
    char fd_contents[4096];
    CHECK(fd_errors.load()==0 && pread(fd_shared,fd_contents,sizeof fd_contents,0)==sizeof fd_contents);
    size_t fd_counts[4]{};
    for(char byte:fd_contents) {
      if(byte<'A' || byte>'D') { CHECK(false); break; }
      ++fd_counts[static_cast<unsigned>(byte-'A')];
    }
    for(auto count:fd_counts) CHECK(count==1024);
    CHECK(lseek(fd_shared,0,SEEK_CUR)==4096);
    for(auto descriptor:fd_writers) CHECK(close(descriptor)==0);
    CHECK(close(fd_shared)==0 && unlink("/runtime-shared-fd.txt")==0);
    REPORT("shared file descriptions, descriptor flags and concurrent offsets");
  }
  {
    char vector_name[]="/vector-XXXXXX";
    int vector_fd=mkstemp(vector_name); REQUIRE(vector_fd>=0);
    std::vector<char> first_piece(5000,'a'), second_piece(4000,'b');
    iovec output_vectors[]={{first_piece.data(),first_piece.size()},{nullptr,0},
                            {second_piece.data(),second_piece.size()}};
    CHECK(writev(vector_fd,output_vectors,3)==9000 && lseek(vector_fd,0,SEEK_CUR)==9000);
    REQUIRE(lseek(vector_fd,0,SEEK_SET)==0);
    std::vector<char> first_read(4999,'?'), second_read(4002,'?');
    iovec input_vectors[]={{first_read.data(),first_read.size()},{second_read.data(),second_read.size()}};
    CHECK(readv(vector_fd,input_vectors,2)==9000 && second_read[4001]=='?');
    bool vector_contents=true;
    for(char byte:first_read) if(byte!='a') vector_contents=false;
    if(second_read[0]!='a') vector_contents=false;
    for(size_t i=1;i<4001;++i) if(second_read[i]!='b') vector_contents=false;
    CHECK(vector_contents && readv(vector_fd,input_vectors,2)==0);
    iovec too_large[]={{first_piece.data(),static_cast<size_t>(INT64_MAX)},{first_piece.data(),1}};
    errno=0; CHECK(writev(vector_fd,too_large,2)==-1 && errno==EINVAL);
    errno=0; CHECK(readv(vector_fd,nullptr,1)==-1 && errno==EFAULT);
    errno=0; CHECK(writev(vector_fd,output_vectors,IOV_MAX+1)==-1 && errno==EINVAL);
    CHECK(close(vector_fd)==0 && unlink(vector_name)==0);
    REPORT("vectored file transfers, partial scatter and atomic pipe records");
  }
  {
    char base_path[]="/a/b///", parent_path[]="/a//b/";
    CHECK(!strcmp(basename(base_path),"b") && !strcmp(dirname(parent_path),"/a"));
    CHECK(!strcmp(basename(nullptr),".") && !strcmp(dirname(nullptr),"."));
    CHECK(fnmatch("*.txt","hello.txt",0)==0 && fnmatch("[a-c]?","b9",0)==0);
    CHECK(fnmatch("[[:alpha:]][![:digit:]]","a!",0)==0);
    CHECK(fnmatch("*",".hidden",FNM_PERIOD)==FNM_NOMATCH);
    CHECK(fnmatch(".*",".hidden",FNM_PERIOD)==0);
    CHECK(fnmatch("a/*","a/x/y",FNM_PATHNAME)==FNM_NOMATCH);
    CHECK(fnmatch("a/*","a/x/y",0)==0 && fnmatch("a/?","a/.",FNM_PATHNAME|FNM_PERIOD)==FNM_NOMATCH);
    CHECK(fnmatch("\\*","*",0)==0 && fnmatch("\\*","*",FNM_NOESCAPE)==FNM_NOMATCH);
    REPORT("native lexical paths and C-locale filename matching");
  }
  {
    char first[]="/identity-a-XXXXXX", second[]="/identity-b-XXXXXX";
    int a=mkstemp(first), b=mkstemp(second);
    REQUIRE(a>=0 && b>=0);
    struct stat sa{}, sb{}, path{}, copied{};
    int copy=dup(a); REQUIRE(copy>=0);
    CHECK(fstat(a,&sa)==0 && fstat(b,&sb)==0 && fstat(copy,&copied)==0);
    CHECK(sa.st_dev==copied.st_dev && sa.st_ino==copied.st_ino);
    CHECK(sa.st_dev!=sb.st_dev || sa.st_ino!=sb.st_ino);
    CHECK(stat(first,&path)==0 && path.st_dev==sa.st_dev && path.st_ino==sa.st_ino);
    CHECK(lstat(first,&path)==0 && path.st_ino==sa.st_ino);
    CHECK(sa.st_atime==0 && sa.st_ctime==0);
    errno=0; CHECK(futimes(a,nullptr)==-1 && errno==ENOTSUP);
    CHECK(close(copy)==0 && close(a)==0 && close(b)==0);
    a=open(first,O_RDONLY); REQUIRE(a>=0);
    CHECK(fstat(a,&path)==0 && path.st_dev==sa.st_dev && path.st_ino==sa.st_ino);
    CHECK(close(a)==0 && unlink(first)==0 && unlink(second)==0);
    REPORT("stable filesystem identity and explicit timestamp limitations");
  }
  {
    struct rlimit saved{}, limited{}, observed_limit{};
    REQUIRE(getrlimit(RLIMIT_NOFILE,&saved)==0 && saved.rlim_cur>16);
    int high=fcntl(0,F_DUPFD,64); REQUIRE(high>=64);
    limited={16,saved.rlim_max};
    REQUIRE(setrlimit(RLIMIT_NOFILE,&limited)==0);
    CHECK(sysconf(_SC_OPEN_MAX)==16 && fcntl(high,F_GETFD)>=0);
    errno=0; CHECK(dup2(0,64)==-1 && errno==EBADF);
    int occupied[16]; unsigned count=0;
    while(count<16) { int fd=dup(0); if(fd<0) break; occupied[count++]=fd; }
    CHECK(count<16 && errno==EMFILE);
    int unchanged[2]={1234,5678}; errno=0;
    CHECK(pipe(unchanged)==-1 && errno==EMFILE && unchanged[0]==1234 && unchanged[1]==5678);
    REQUIRE(setrlimit(RLIMIT_NOFILE,&saved)==0);
    for(unsigned i=0;i<count;++i) CHECK(close(occupied[i])==0);
    CHECK(close(high)==0 && sysconf(_SC_OPEN_MAX)==static_cast<long>(saved.rlim_cur));
    CHECK(getrlimit(RLIMIT_STACK,&observed_limit)==0 && observed_limit.rlim_cur==65536);
    CHECK(getrlimit(RLIMIT_CORE,&observed_limit)==0 && observed_limit.rlim_cur==0 && observed_limit.rlim_max==0);
    limited={saved.rlim_max+1,saved.rlim_max}; errno=0;
    CHECK(setrlimit(RLIMIT_NOFILE,&limited)==-1 && errno==EINVAL);
    REPORT("enforced descriptor limits and retained open descriptors");
  }
  {
    char directory_path[]="/directory-api-XXXXXX";
    REQUIRE(mkdtemp(directory_path)==directory_path);
    int directory_fd=open(directory_path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    REQUIRE(directory_fd>=0);
    int source_fd=openat(directory_fd,"./source",O_CREAT|O_EXCL|O_RDWR,0600);
    REQUIRE(source_fd>=0);
    CHECK(write(source_fd,"profile",7)==7);
    struct stat source_info{}, relative_info{}, renamed_info{};
    CHECK(fstat(source_fd,&source_info)==0);
    CHECK(fstatat(directory_fd,"source",&relative_info,AT_SYMLINK_NOFOLLOW)==0);
    CHECK(source_info.st_ino==relative_info.st_ino && source_info.st_dev==relative_info.st_dev);
    auto restricted_directory=charm::handle_duplicate(charm_fd_handle(directory_fd),
        charm::rights::kRead|charm::rights::kDuplicate);
    REQUIRE(restricted_directory.ok());
    auto restricted_handle=static_cast<charm::Handle>(restricted_directory.value);
    int restricted_fd=charm_fd_from_handle(restricted_handle,CHARM_FD_DIRECTORY);
    if(restricted_fd<0) (void)charm::handle_close(restricted_handle);
    REQUIRE(restricted_fd>=0);
    int restricted_read=openat(restricted_fd,"source",O_RDONLY);
    REQUIRE(restricted_read>=0);
    CHECK(close(restricted_read)==0);
    errno=0; CHECK(openat(restricted_fd,"forbidden",O_CREAT|O_RDWR,0600)==-1 && errno==EACCES);
    errno=0; CHECK(unlinkat(restricted_fd,"source",0)==-1 && errno==EACCES);
    errno=0; CHECK(renameat(restricted_fd,"source",directory_fd,"forbidden")==-1 && errno==EACCES);
    CHECK(close(restricted_fd)==0);
    errno=0; CHECK(openat(directory_fd,"../escape",O_RDONLY)==-1 && errno==ENOTSUP);
    errno=0; CHECK(unlinkat(directory_fd,"source",AT_REMOVEDIR)==-1 && errno==ENOTDIR);
    errno=0; CHECK(unlink(directory_path)==-1 && errno==EISDIR);
    errno=0; CHECK(fdopendir(source_fd)==nullptr && errno==ENOTDIR);
    CHECK(fcntl(source_fd,F_GETFD)>=0);
    REQUIRE(renameat(directory_fd,"source",directory_fd,"renamed")==0);
    errno=0; CHECK(fstatat(directory_fd,"source",&relative_info,0)==-1 && errno==ENOENT);
    CHECK(fstatat(directory_fd,"renamed",&renamed_info,0)==0);
    CHECK(renamed_info.st_ino==source_info.st_ino && renamed_info.st_dev==source_info.st_dev);
    char retained_bytes[8]{};
    CHECK(pread(source_fd,retained_bytes,7,0)==7 && !strcmp(retained_bytes,"profile"));
    int destination_fd=openat(directory_fd,"destination",O_CREAT|O_EXCL|O_RDWR,0600);
    REQUIRE(destination_fd>=0);
    errno=0; CHECK(renameat(directory_fd,"renamed",directory_fd,"destination")==-1 && errno==EBUSY);
    CHECK(close(destination_fd)==0);
    CHECK(renameat(directory_fd,"renamed",directory_fd,"destination")==0);
    CHECK(fstatat(directory_fd,"destination",&renamed_info,0)==0 && renamed_info.st_ino==source_info.st_ino);
    CHECK(close(source_fd)==0);
    char canonical[4096]="unchanged", canonical_input[4096];
    snprintf(canonical_input,sizeof canonical_input,"%s//./",directory_path);
    CHECK(realpath(canonical_input,canonical)==canonical && !strcmp(canonical,directory_path));
    snprintf(canonical_input,sizeof canonical_input,"%s/destination/..",directory_path);
    strcpy(canonical,"unchanged"); errno=0;
    CHECK(realpath(canonical_input,canonical)==nullptr && errno==ENOTDIR && !strcmp(canonical,"unchanged"));
    snprintf(canonical_input,sizeof canonical_input,"%s/missing/../destination",directory_path);
    errno=0; CHECK(realpath(canonical_input,canonical)==nullptr && errno==ENOENT && !strcmp(canonical,"unchanged"));
    char* allocated_path=realpath(directory_path,nullptr);
    CHECK(allocated_path && !strcmp(allocated_path,directory_path));
    free(allocated_path);
    CHECK(pathconf(directory_path,_PC_NAME_MAX)==255);
    DIR* owned_directory=fdopendir(directory_fd); REQUIRE(owned_directory);
    CHECK(dirfd(owned_directory)==directory_fd);
    bool found_destination=false;
    while(dirent* entry=readdir(owned_directory))
      if(!strcmp(entry->d_name,"destination")) found_destination=true;
    CHECK(found_destination);
    CHECK(unlinkat(dirfd(owned_directory),"destination",0)==0);
    CHECK(closedir(owned_directory)==0);
    errno=0; CHECK(fcntl(directory_fd,F_GETFD)==-1 && errno==EBADF);
    CHECK(rmdir(directory_path)==0);
    REPORT("directory-relative files, stable rename identity and checked canonical paths");
  }
  {
    charm::SharedMemory startup_memory;
    REQUIRE(charm::SharedMemory::create(4096,&startup_memory)==charm::Status::Ok);
    charm::Mapping startup_mapping;
    REQUIRE(startup_memory.map(charm::MemoryAccess::ReadWrite,&startup_mapping)==charm::Status::Ok);
    static_cast<char*>(startup_mapping.data())[0]=73;
    charm::UniqueHandle startup_transfer;
    REQUIRE(startup_memory.share(charm::rights::kRead|charm::rights::kMap|
        charm::rights::kTransfer,&startup_transfer)==charm::Status::Ok);
    uint32_t startup_handle=startup_transfer.get(),startup_role=0x40000001u;
    charm_spawn_opts startup_options{};
    startup_options.extra_count=1; startup_options.extra=&startup_handle; startup_options.extra_roles=&startup_role;
    char* startup_arguments[]={const_cast<char*>("runtime_cpp"),const_cast<char*>("startup-memory"),nullptr};
    int64_t startup_child=charm_spawn("/bin/runtime_cpp",startup_arguments,nullptr,&startup_options);
    REQUIRE(startup_child>0);
    (void)startup_transfer.release();
    auto startup_process=charm::UniqueHandle::adopt(static_cast<charm::Handle>(startup_child));
    int64_t startup_exit=0;
    CHECK(charm::ProcessView(startup_process.get()).wait(charm::kForever,&startup_exit)==charm::Status::Ok && startup_exit==40);
    REPORT("one-time startup capability ownership under concurrent consumers");
  }
  {
    charm::Handle channel_pair[2];
    REQUIRE(charm::channel_create(channel_pair)==charm::Status::Ok);
    auto outgoing_channel=charm::UniqueHandle::adopt(channel_pair[0]);
    auto incoming_channel=charm::UniqueHandle::adopt(channel_pair[1]);
    char channel_byte='x';
    for(unsigned i=0;i<abi::syscall::kChannelMaxQueue;++i)
      REQUIRE(charm::channel_write(outgoing_channel.get(),&channel_byte,1,nullptr,0)==charm::Status::Ok);
    CHECK(charm::channel_write(outgoing_channel.get(),&channel_byte,1,nullptr,0)==charm::Status::WouldBlock);
    abi::syscall::ChannelState queue_state{};
    CHECK(charm::channel_query(incoming_channel.get(),&queue_state)==charm::Status::Ok &&
          queue_state.messages==64 && queue_state.queued_bytes==64 && queue_state.flags==0);
    for(unsigned attempt=0;attempt<16;++attempt)
      CHECK(charm::wait_channel(outgoing_channel.get(),2,charm::now_ns()+1000000)==charm::Status::TimedOut);
    std::atomic<bool> channel_consumed{false};
    std::thread channel_reader([&] {
      char received_byte=0;
      auto received_message=charm::channel_read(incoming_channel.get(),&received_byte,1,nullptr,0,charm::kForever);
      channel_consumed=received_message.ok() && received_byte=='x';
    });
    CHECK(charm::wait_channel(outgoing_channel.get(),2,charm::now_ns()+1000000000)==charm::Status::Ok);
    channel_reader.join(); CHECK(channel_consumed);
    charm::UniqueHandle write_only_channel;
    REQUIRE(outgoing_channel.duplicate(charm::rights::kWrite,&write_only_channel)==charm::Status::Ok);
    CHECK(charm::channel_query(write_only_channel.get(),&queue_state)==charm::Status::AccessDenied);
    CHECK(charm::wait_channel(write_only_channel.get(),2,charm::now_ns()+1000000)==charm::Status::Ok);
    CHECK(charm::channel_shutdown(write_only_channel.get(),1)==charm::Status::AccessDenied);
    CHECK(charm::channel_shutdown(outgoing_channel.get(),0)==charm::Status::InvalidArgs);
    CHECK(charm::channel_shutdown(outgoing_channel.get(),4)==charm::Status::InvalidArgs);
    for(unsigned i=1;i<abi::syscall::kChannelMaxQueue;++i)
      CHECK(charm::channel_read(incoming_channel.get(),&channel_byte,1,nullptr,0,0).ok());
    CHECK(charm::channel_write(outgoing_channel.get(),"ab",2,nullptr,0)==charm::Status::Ok);
    CHECK(charm::channel_shutdown(outgoing_channel.get(),2)==charm::Status::Ok);
    CHECK(charm::channel_shutdown(outgoing_channel.get(),2)==charm::Status::Ok);
    char channel_tail[2]{};
    auto drained=charm::channel_read(incoming_channel.get(),channel_tail,2,nullptr,0,0);
    CHECK(drained.ok() && drained.value==(uint64_t{2}<<32) && channel_tail[0]=='a' && channel_tail[1]=='b');
    CHECK(charm::channel_read(incoming_channel.get(),channel_tail,2,nullptr,0,0).status==charm::Status::PeerClosed);
    CHECK(charm::channel_query(incoming_channel.get(),&queue_state)==charm::Status::Ok && (queue_state.flags&4u));
    CHECK(charm::channel_write(incoming_channel.get(),"r",1,nullptr,0)==charm::Status::Ok);
    CHECK(charm::channel_read(outgoing_channel.get(),&channel_byte,1,nullptr,0,0).ok() && channel_byte=='r');
    REPORT("channel writable readiness, cancelled watches and half-close draining");
  }
  {
    charm::Handle stream_pair[2];
    REQUIRE(charm::channel_create(stream_pair)==charm::Status::Ok);
    auto stream_writer=charm::UniqueHandle::adopt(stream_pair[0]);
    auto stream_reader=charm::UniqueHandle::adopt(stream_pair[1]);
    auto sending_stream=std::make_unique<charm::ChannelStream>(stream_writer.get());
    auto receiving_stream=std::make_unique<charm::ChannelStream>(stream_reader.get());
    std::vector<unsigned char> stream_payload(300000),stream_received(stream_payload.size());
    for(size_t i=0;i<stream_payload.size();++i) stream_payload[i]=static_cast<unsigned char>(i%251);
    bool stream_written=false;
    std::thread stream_producer([&] {
      auto transferred=sending_stream->write(stream_payload.data(),stream_payload.size());
      stream_written=transferred.status==charm::Status::Ok && transferred.bytes==stream_payload.size();
      (void)sending_stream->shutdown(2);
    });
    size_t stream_position=0;
    while(stream_position<stream_received.size()) {
      size_t amount=std::min(size_t{127},stream_received.size()-stream_position);
      auto transferred=receiving_stream->read(stream_received.data()+stream_position,amount,
                                               charm::now_ns()+5000000000);
      if(transferred.status!=charm::Status::Ok || !transferred.bytes) break;
      stream_position+=transferred.bytes;
    }
    if(stream_position!=stream_received.size()) (void)receiving_stream->shutdown();
    stream_producer.join();
    CHECK(stream_written && stream_position==stream_received.size() && stream_payload==stream_received);
    unsigned char eof_byte=0;
    auto stream_eof=receiving_stream->read(&eof_byte,1,0);
    CHECK(stream_eof.status==charm::Status::Ok && stream_eof.bytes==0);
    REPORT("native channel streams with bounded frames, backpressure and partial reads");
  }
  {
    charm::Handle discard_pair[2];
    REQUIRE(charm::channel_create(discard_pair)==charm::Status::Ok);
    auto discard_sender=charm::UniqueHandle::adopt(discard_pair[0]);
    auto discard_receiver=charm::UniqueHandle::adopt(discard_pair[1]);
    charm::SharedMemory queued_memory;
    REQUIRE(charm::SharedMemory::create(4096,&queued_memory)==charm::Status::Ok);
    charm::UniqueHandle queued_capability;
    REQUIRE(queued_memory.share(charm::rights::kRead|charm::rights::kTransfer,&queued_capability)==charm::Status::Ok);
    charm::Handle queued_handle=queued_capability.get();
    REQUIRE(charm::channel_write(discard_sender.get(),nullptr,0,&queued_handle,1)==charm::Status::Ok);
    (void)queued_capability.release();
    CHECK(charm::channel_shutdown(discard_receiver.get(),1)==charm::Status::Ok);
    abi::syscall::ChannelState discarded_state{};
    CHECK(charm::channel_query(discard_receiver.get(),&discarded_state)==charm::Status::Ok &&
          discarded_state.messages==0 && discarded_state.queued_bytes==0 && (discarded_state.flags&1u));
    auto descendants=charm::handle_revoke(queued_memory.handle());
    CHECK(descendants.ok() && descendants.value==0);
    CHECK(charm::channel_write(discard_sender.get(),"x",1,nullptr,0)==charm::Status::PeerClosed);
    std::atomic<bool> reader_started{false};
    charm::Status cancelled_read=charm::Status::Ok;
    std::thread blocked_reader([&] {
      char blocked_byte=0;
      reader_started=true;
      cancelled_read=charm::channel_read(discard_sender.get(),&blocked_byte,1,nullptr,0,charm::kForever).status;
    });
    while(!reader_started.load()) std::this_thread::yield();
    CHECK(charm::channel_shutdown(discard_sender.get(),1)==charm::Status::Ok);
    blocked_reader.join();
    CHECK(cancelled_read==charm::Status::PeerClosed);
    REPORT("channel read shutdown closes queued capabilities and cancels blocked reads");
  }
  {
    charm::Handle readiness_pair[2];
    REQUIRE(charm::channel_create(readiness_pair)==charm::Status::Ok);
    auto readiness_writer=charm::UniqueHandle::adopt(readiness_pair[0]);
    auto readiness_reader=charm::UniqueHandle::adopt(readiness_pair[1]);
    int read_watch=charm_channel_watch_fd(readiness_reader.get(),1);
    int write_watch=charm_channel_watch_fd(readiness_writer.get(),2);
    REQUIRE(read_watch>=0 && write_watch>=0);
    CHECK((fcntl(read_watch,F_GETFD)&FD_CLOEXEC)!=0);
    pollfd readiness_fds[]={{read_watch,POLLIN,0},{write_watch,POLLOUT,0}};
    CHECK(poll(readiness_fds,2,0)==1 && readiness_fds[0].revents==0 && (readiness_fds[1].revents&POLLOUT));
    REQUIRE(charm::channel_write(readiness_writer.get(),"abc",3,nullptr,0)==charm::Status::Ok);
    CHECK(poll(readiness_fds,1,0)==1 && (readiness_fds[0].revents&POLLIN));
    char readiness_bytes[3]{};
    errno=0; CHECK(read(read_watch,readiness_bytes,3)==-1 && errno==ENOTSUP);
    struct stat readiness_stat{}; readiness_stat.st_size=123;
    errno=0; CHECK(fstat(read_watch,&readiness_stat)==-1 && errno==ENOTSUP && readiness_stat.st_size==123);
    auto readiness_message=charm::channel_read(readiness_reader.get(),readiness_bytes,3,nullptr,0,0);
    CHECK(readiness_message.ok() && readiness_message.value==(uint64_t{3}<<32) && !memcmp(readiness_bytes,"abc",3));
    CHECK(poll(readiness_fds,1,0)==0);
    readiness_reader.reset();
    CHECK(charm::channel_write(readiness_writer.get(),"xyz",3,nullptr,0)==charm::Status::Ok);
    CHECK(poll(readiness_fds,1,0)==1 && (readiness_fds[0].revents&POLLIN));
    readiness_message=charm::channel_read(charm_fd_handle(read_watch),readiness_bytes,3,nullptr,0,0);
    CHECK(readiness_message.ok() && !memcmp(readiness_bytes,"xyz",3));
    CHECK(close(read_watch)==0);
    CHECK(poll(readiness_fds+1,1,0)==1 && (readiness_fds[1].revents&POLLERR));
    CHECK(close(write_watch)==0);
    CHECK(charm::channel_query(readiness_writer.get(),nullptr)==charm::Status::BadAddress);
    REPORT("channel readiness descriptors preserve native messages and capability lifetime");
  }
  return failures ? 1 : 0;
}
