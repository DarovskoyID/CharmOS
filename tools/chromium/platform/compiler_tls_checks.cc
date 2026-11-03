#include <charm/libc.h>
#include <charm/process.hpp>
#include <atomic>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <thread>

thread_local int native_tls_scalar=7;
int IncrementNativeTlsFromAnotherUnit();
namespace {
struct Wide { uint64_t words[16]; };
alignas(256) thread_local Wide wide_tls{{17,29}};
thread_local unsigned slot_tls=0;
std::atomic<bool> tls_failure{false},release_workers{false};
std::atomic<unsigned> ready_workers{0},constructed{0},key_destructors{0};
uintptr_t scalar_addresses[8]{};
unsigned destructor_order[8][4]{},destructor_count[8]{};
pthread_key_t cleanup_key;
unsigned key_values[8]{};
struct Probe;
Probe& LateProbe();
struct Probe {
  unsigned id,slot;
  explicit Probe(unsigned identifier):id(identifier),slot(slot_tls) { ++constructed; }
  ~Probe() {
    if(slot>=8 || slot_tls!=slot) { tls_failure=true; return; }
    unsigned position=destructor_count[slot]++;
    if(position>=4) { tls_failure=true; return; }
    destructor_order[slot][position]=id;
    void* specific=pthread_getspecific(cleanup_key);
    if((id<4 && specific!=&key_values[slot]) || (id==4 && specific)) tls_failure=true;
    if(id==2) (void)LateProbe();
  }
};
// This test deliberately requires real exit-time destructors, unlike normal
// Chromium globals. Keep the exception local to these probe declarations.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
Probe& FirstProbe() { thread_local Probe probe(1); return probe; }
Probe& SecondProbe() { thread_local Probe probe(2); return probe; }
Probe& LateProbe() { thread_local Probe probe(3); return probe; }
Probe& KeyProbe() { thread_local Probe probe(4); return probe; }
#pragma clang diagnostic pop
void KeyDestructor(void* value) {
  if(value!=&key_values[slot_tls] || native_tls_scalar!=static_cast<int>(slot_tls)+21) tls_failure=true;
  (void)KeyProbe();
  ++key_destructors;
}
void ExerciseTls(unsigned slot) {
  slot_tls=slot;
  if(native_tls_scalar!=7 || wide_tls.words[0]!=17 || wide_tls.words[1]!=29 ||
     wide_tls.words[2]!=0 || (reinterpret_cast<uintptr_t>(&wide_tls)&255u)) tls_failure=true;
  native_tls_scalar=static_cast<int>(slot)+20;
  if(IncrementNativeTlsFromAnotherUnit()!=static_cast<int>(slot)+21) tls_failure=true;
  wide_tls.words[0]=slot+100;
  if(pthread_setspecific(cleanup_key,&key_values[slot])) tls_failure=true;
  auto* first=&FirstProbe();
  (void)SecondProbe();
  if(&FirstProbe()!=first || first->id!=1 || first->slot!=slot) tls_failure=true;
  scalar_addresses[slot]=reinterpret_cast<uintptr_t>(&native_tls_scalar);
}
void* SequentialWorker(void*) { ExerciseTls(0); return nullptr; }
int main_exit_marker=0;
struct MainExitProbe { ~MainExitProbe() { main_exit_marker=1; } };
void VerifyMainExitOrdering() { _Exit(main_exit_marker==1 ? 42 : 76); }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
int RunNativeCompilerTlsExitChild() {
  thread_local MainExitProbe probe;
  (void)probe;
  if(atexit(VerifyMainExitOrdering)) return 77;
  return 0; // libc exit must run the TLS destructor before the atexit check.
}
#pragma clang diagnostic pop

int TestNativeCompilerTls() {
  if(native_tls_scalar!=7 || IncrementNativeTlsFromAnotherUnit()!=8) return 130;
  if(pthread_key_create(&cleanup_key,KeyDestructor)) return 131;
  std::thread workers[8];
  for(unsigned slot=0;slot<8;++slot) workers[slot]=std::thread([slot] {
    ExerciseTls(slot);
    ++ready_workers;
    while(!release_workers.load()) std::this_thread::yield();
    if(native_tls_scalar!=static_cast<int>(slot)+21 || wide_tls.words[0]!=slot+100) tls_failure=true;
  });
  while(ready_workers.load()!=8) std::this_thread::yield();
  bool distinct=true;
  for(unsigned i=0;i<8;++i) for(unsigned j=0;j<i;++j)
    if(scalar_addresses[i]==scalar_addresses[j]) distinct=false;
  release_workers=true;
  for(auto& worker:workers) worker.join();
  if(!distinct || tls_failure.load() || constructed.load()!=32 || key_destructors.load()!=8 || native_tls_scalar!=8) return 132;
  for(unsigned slot=0;slot<8;++slot)
    if(destructor_count[slot]!=4 || destructor_order[slot][0]!=2 || destructor_order[slot][1]!=3 ||
       destructor_order[slot][2]!=1 || destructor_order[slot][3]!=4) return 133;
  size_t retained=charm_heap_allocated_bytes();
  for(unsigned round=0;round<32;++round) {
    destructor_count[0]=0;
    pthread_t worker;
    if(pthread_create(&worker,nullptr,SequentialWorker,nullptr) || pthread_join(worker,nullptr)) return 134;
    if(tls_failure.load() || destructor_count[0]!=4 || charm_heap_allocated_bytes()!=retained) return 135;
  }
  if(pthread_key_delete(cleanup_key)) return 136;
  char* arguments[]={const_cast<char*>("runtime_cpp"),const_cast<char*>("compiler-tls-exit"),nullptr};
  charm_spawn_opts options{};
  int64_t child=charm_spawn("/bin/runtime_cpp",arguments,nullptr,&options);
  if(child<=0) return 137;
  auto owner=charm::UniqueHandle::adopt(static_cast<charm::Handle>(child));
  int64_t exit_code=0;
  if(charm::ProcessView(owner.get()).wait(charm::kForever,&exit_code)!=charm::Status::Ok || exit_code!=42) return 138;
  return 0;
}
