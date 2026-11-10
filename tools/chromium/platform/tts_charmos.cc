// CharmOS text-to-speech platform.
//
// No speech engine is installed. Reporting PlatformImplSupported() == false
// makes the TTS controller disable speech synthesis cleanly instead of trying
// to open a missing system service.
#include <string>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "content/browser/speech/tts_platform_impl.h"
#include "content/public/browser/tts_platform.h"

namespace content {

namespace {

class CharmTtsPlatformImpl final : public TtsPlatformImpl {
 public:
  static CharmTtsPlatformImpl* GetInstance() {
    static base::NoDestructor<CharmTtsPlatformImpl> instance;
    return instance.get();
  }

  CharmTtsPlatformImpl() = default;

  CharmTtsPlatformImpl(const CharmTtsPlatformImpl&) = delete;
  CharmTtsPlatformImpl& operator=(const CharmTtsPlatformImpl&) = delete;

  bool PlatformImplSupported() override { return false; }
  bool PlatformImplInitialized() override { return false; }

  void Speak(int /*utterance_id*/,
             const std::string& /*utterance*/,
             const std::string& /*lang*/,
             const VoiceData& /*voice*/,
             const UtteranceContinuousParameters& /*params*/,
             base::OnceCallback<void(bool)> did_start_speaking_callback)
      override {
    if (did_start_speaking_callback) {
      std::move(did_start_speaking_callback).Run(false);
    }
  }

  bool StopSpeaking() override { return false; }
  bool IsSpeaking() override { return false; }
  void GetVoices(std::vector<VoiceData>* /*out_voices*/) override {}
  void Pause() override {}
  void Resume() override {}
};

}  // namespace

TtsPlatformImpl* TtsPlatformImpl::GetInstance() {
  return CharmTtsPlatformImpl::GetInstance();
}

}  // namespace content
