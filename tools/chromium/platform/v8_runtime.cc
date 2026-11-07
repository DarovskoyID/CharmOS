// Minimal native V8 smoke: evaluate real JavaScript in a real isolate.
// A JavaScript engine is the core of the browser; this proves V8 runs on
// CharmOS. This probe uses the interpreter-only profile, without JIT.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

namespace {

int RunV8(int argc, char** argv) {
  // CharmOS has no MAP_FIXED: V8's default hole unmapping would replace pages
  // inside live reservations with PROT_NONE mappings at fixed addresses.
  // Disable that (it is a hardening optimization, not required for semantics).
  // CharmOS enforces W^X for VMO mappings (see docs/abi/executable-memory.md).
  // V8's x64 code range pre-commits its pages RWX, which the kernel rejects.
  // Run the interpreter-only profile for now; JIT needs kernel PKU/RWX support.
  // Single-threaded mode avoids V8's concurrent GC/compiler threads, which the
  // initial native port does not exercise yet.
  v8::V8::SetFlagsFromString("--no-unmap-holes --jitless --single-threaded");
  // V8 i18n uses Chromium's static ICU whose data lives in icudtl.dat next to
  // the executable (ICU_UTIL_DATA_FILE). Install path is fixed at /bin; do not
  // derive it from argv[0], which the shell passes without a directory.
  if (!v8::V8::InitializeICUDefaultLocation("/bin/v8_runtime", nullptr)) {
    printf("Native V8: ICU initialization failed\n");
    return 1;
  }
  std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
  if (!platform) {
    printf("Native V8: no platform\n");
    return 1;
  }
  v8::V8::InitializePlatform(platform.get());
  if (!v8::V8::Initialize()) {
    printf("Native V8: initialize failed\n");
    return 1;
  }
  v8::Isolate::CreateParams params;
  // With lazy VMO backing, V8 may use its default x64 CodeRange reservation;
  // PROT_NONE does not commit physical pages up front.
  std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  params.array_buffer_allocator = array_buffer_allocator.get();
  v8::Isolate* isolate = v8::Isolate::New(params);
  if (!isolate) {
    printf("Native V8: isolate failed\n");
    return 1;
  }
  int exit_code = 0;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);
    auto run = [&](const char* source, int expected) -> bool {
      v8::Local<v8::String> text =
          v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
      v8::Local<v8::Script> script;
      if (!v8::Script::Compile(context, text).ToLocal(&script)) return false;
      v8::Local<v8::Value> result;
      if (!script->Run(context).ToLocal(&result)) return false;
      return result->Int32Value(context).FromMaybe(-1) == expected;
    };
    // Interpreter semantics, including real backing storage and exceptions.
    if (!run("1 + 2 * 3", 7) ||
        !run("(function(){ var a=[1,2,3]; var s=0; for (var i of a) s+=i; return s; })()", 6) ||
        !run("({x: 4, y: 5}).x + (function f(n){ return n<2?1:n*f(n-1); })(3)", 10) ||
        !run("(()=>{const a=new Uint8Array(new ArrayBuffer(8192)); a[0]=17; a[8191]=29; return a[0]+a[8191]})()",46) ||
        !run("(()=>{try {throw new Error('native')} catch(e) {return e.message==='native'?23:0}})()",23) ||
        !run("(()=>{const m=new Map([['x',19]]); return JSON.parse(JSON.stringify({n:m.get('x')})).n})()",19)) {
      printf("Native V8: script evaluation failed\n");
      exit_code = 1;
    }
  }
  // Exercise collection with the context handles gone. Do not replace weak
  // list invariants with permissive type guards to make this operation pass.
  {
    v8::Isolate::Scope isolate_scope(isolate);
    isolate->LowMemoryNotification();
  }
  printf("Native V8: full collection ok\n");
  if (!exit_code) printf("Native V8: JavaScript evaluation ok (arithmetic, closures, loops, objects)\n");
  printf("Native V8: disposing isolate\n");
  isolate->Dispose();
  if (!v8::V8::Dispose()) {
    printf("Native V8: dispose failed\n");
    return 1;
  }
  v8::V8::DisposePlatform();
  return exit_code;
}

void* ThreadMain(void* arg) {
  auto* context = static_cast<std::pair<int, char**>*>(arg);
  int code = RunV8(context->first, context->second);
  return reinterpret_cast<void*>(static_cast<intptr_t>(code));
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  // The kernel bootstrap stack is 64 KiB, while V8 expects the multi-megabyte
  // main-thread stack of a desktop OS. Run the engine on a dedicated thread.
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) != 0 ||
      pthread_attr_setstacksize(&attr, 8u << 20) != 0) {
    printf("Native V8: stack setup failed\n");
    return 1;
  }
  std::pair<int, char**> context{argc, argv};
  pthread_t thread;
  if (pthread_create(&thread, &attr, &ThreadMain, &context) != 0) {
    pthread_attr_destroy(&attr);
    printf("Native V8: thread creation failed\n");
    return 1;
  }
  pthread_attr_destroy(&attr);
  void* result = nullptr;
  if (pthread_join(thread, &result) != 0) {
    printf("Native V8: thread join failed\n");
    return 1;
  }
  int code=static_cast<int>(reinterpret_cast<intptr_t>(result));
  if (!code) printf("Native V8: clean shutdown ok\n");
  return code;
}
