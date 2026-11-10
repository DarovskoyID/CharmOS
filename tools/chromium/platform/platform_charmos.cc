// CharmOS platform entry points that upstream only builds for specific OSes.
//
// Crash reporting has no Crashpad backend on CharmOS, so initialization
// reports "unavailable" and the upstream CrashReporterClient defaults (compiled
// from components/crash/core/app/crash_reporter_client.cc) remain authoritative
// for consent and upload decisions. Perfetto producer tracing is likewise not
// hosted by a local traced service; the socket name is still exposed so
// embedders can opt in by running the service.
#include <stdlib.h>

#include <memory>
#include <new>
#include <string>

#include "base/process/memory.h"
#include "media/audio/audio_manager.h"
#include "media/audio/audio_thread.h"
#include "media/audio/fake_audio_manager.h"

namespace base {

void EnableTerminationOnHeapCorruption() {
  // The native allocator already aborts on detected metadata corruption.
}

void EnableTerminationOnOutOfMemory() {
  // Route exhausted allocation through the process-wide OOM path instead of
  // throwing std::bad_alloc, matching Chromium's Linux contract.
  std::set_new_handler([] { TerminateBecauseOutOfMemory(0); });
}

}  // namespace base

namespace crash_reporter {

// Declared in components/crash/core/app/crashpad.h.
bool InitializeCrashpad(bool initial_client, const std::string& process_type);

bool InitializeCrashpad(bool /*initial_client*/,
                        const std::string& /*process_type*/) {
  // No crashpad handler or client library is present in the Rust-free build.
  return false;
}

}  // namespace crash_reporter

namespace media {

std::unique_ptr<AudioManager> CreateAudioManager(
    std::unique_ptr<AudioThread> audio_thread,
    AudioLogFactory* audio_log_factory) {
  // CharmOS currently exposes no PCM device to Chromium; the fake manager
  // provides a real timed audio clock and silence stream for MediaStream and
  // Web Audio without opening a hardware backend.
  return std::make_unique<FakeAudioManager>(std::move(audio_thread),
                                            audio_log_factory);
}

}  // namespace media

namespace perfetto {

// Declared in perfetto/tracing/default_socket.h.
const char* GetProducerSocket();

const char* GetProducerSocket() {
  if (const char* name = getenv("PERFETTO_PRODUCER_SOCK_NAME")) {
    return name;
  }
  return "/tmp/perfetto-producer";
}

}  // namespace perfetto
