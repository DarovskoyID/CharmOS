#include "base/command_line.h"
#include "base/at_exit.h"
#include "base/functional/bind.h"
#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/i18n/icu_util.h"
#include "base/i18n/tag_converters.h"
#include "base/i18n/language_tag_matcher.h"
#include "base/i18n/icubridge/normalizer.h"
#include "net/http/structured_headers.h"
#include <array>
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/timer/timer.h"
#include "base/threading/thread.h"
#include "build/charmos/input_translator_charmos.h"
#include "build/charmos/ozone_platform_charmos.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/display/display.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/ozone/public/platform_screen.h"
#include "ui/ozone/public/surface_factory_ozone.h"
#include "ui/ozone/public/surface_ozone_canvas.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include <stdio.h>

bool CheckNativeCertificates();

namespace {
bool CheckLanguageMatching() {
  using namespace base::i18n;
  auto en=GetLanguageTagFromString("EN_us");
  auto gb=GetLanguageTagFromString("en-GB");
  auto de=GetLanguageTagFromString("de");
  auto fr=GetLanguageTagFromString("fr");
  if(!en || !gb || !de || !fr || en->tag_string()!="en-US" ||
     GetLanguageTagFromString("en-@")) return false;
  std::array supported{*en};
  auto matcher=LanguageTagMatcherWithDefault::Create(*fr,supported);
  using Form=IcuBridge::Normalizer::NormalizationForm;
  const auto& normalizer=IcuBridge::GetInstance().normalizer();
  return matcher.Match(*gb)==en && !matcher.Match(*de) && matcher.MatchOrDefault(*de)==*fr &&
      normalizer.Normalize(Form::NFC,u"e\u0301")==u"\u00e9" &&
      normalizer.Normalize(Form::NFD,u"\u00e9")==u"e\u0301" &&
      normalizer.Normalize(Form::NFKC,u"\ufb01")==u"fi";
}
bool CheckHttpParser() {
  namespace sh=net::structured_headers;
  auto dictionary=sh::ParseDictionary("a=1, b=(2 3);q");
  if(!dictionary || sh::ParseItem("?2") || sh::ParseList("foo,,")) return false;
  auto serialized=sh::SerializeDictionary(*dictionary);
  return serialized && sh::ParseDictionary(*serialized).has_value();
}
bool CheckInputTranslation() {
  ui::InputTranslatorCharmOS translator;
  auto key=[&](uint16_t code,int value) {
    abi::syscall::InputEvent input{};
    input.type=abi::syscall::kInputKey; input.code=code; input.value=value;
    return translator.Translate(input);
  };
  auto a=key(4,1);
  if(a.size()!=2 || a[0]->AsKeyEvent()->code()!=ui::DomCode::US_A ||
     a[1]->AsKeyEvent()->GetCharacter()!=u'a') return false;
  key(0xe1,1); key(0xe5,1); key(0xe1,0);
  auto shifted=key(4,1);
  if(shifted.size()!=2 || shifted[1]->AsKeyEvent()->GetCharacter()!=u'A') return false;
  key(0xe5,0);
  auto apostrophe=key(0x34,1);
  if(apostrophe.size()!=2 || apostrophe[1]->AsKeyEvent()->GetCharacter()!=u'\'') return false;
  key(0xe0,1);
  if(key(4,1).size()!=1) return false;
  translator.ResetPressedState();
  if(key(4,1).size()!=2 || !key(0xff,1).empty()) return false;
  translator.set_russian(true);
  auto russian=key(4,1);
  return russian.size()==2 && russian[1]->AsKeyEvent()->GetCharacter()==u'ф';
}
class Runtime;

class Delegate final : public ui::PlatformWindowDelegate {
 public:
  explicit Delegate(Runtime* runtime) : runtime_(runtime) {}
  void OnBoundsChanged(const BoundsChange& change) override;
  void OnDamageRect(const gfx::Rect& damaged_region) override {}
  void DispatchEvent(ui::Event* event) override;
  void OnCloseRequest() override;
  void OnClosed() override;
  void OnWindowStateChanged(ui::PlatformWindowState old_state,
                            ui::PlatformWindowState new_state) override {}
  void OnLostCapture() override {}
  void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget) override {
    widget_ = widget;
  }
  void OnWillDestroyAcceleratedWidget() override {}
  void OnAcceleratedWidgetDestroyed() override {}
  void OnActivationChanged(bool active) override { active_ = active; }
  void OnCursorUpdate() override {}
  gfx::AcceleratedWidget widget() const { return widget_; }
  bool active() const { return active_; }

 private:
  Runtime* runtime_;
  gfx::AcceleratedWidget widget_ = gfx::kNullAcceleratedWidget;
  bool active_ = false;
};

class Runtime {
  friend class Delegate;
 public:
  Runtime() : delegate_(this) {}
  int Run() {
    if(!CheckHttpParser()) return Fail();
    printf("Native Ozone: C++ structured HTTP parser ok\n");
    if(!CheckLanguageMatching()) return Fail();
    printf("Native Ozone: ICU4C canonicalization and unmatched language fallback ok\n");
    if(!CheckInputTranslation()) return Fail();
    printf("Native Ozone: HID, punctuation, modifier ownership and text ok\n");
    auto* platform = ui::OzonePlatform::GetInstance();
    screen_ = platform->CreateScreen();
    platform->InitScreen(screen_.get());
    if (!screen_ || screen_->GetPrimaryDisplay().bounds().IsEmpty()) return Fail();
    ui::PlatformWindowInitProperties properties(gfx::Rect(140,160,520,300));
    window_ = platform->CreatePlatformWindow(&delegate_,std::move(properties));
    if (!window_ || !delegate_.widget()) return Fail();
    // Factory lookup must work on the render thread and retain a UI-owned
    // presenter without ever reading PlatformWindow fields from that thread.
    base::Thread render_probe("ozone-factory-probe");
    if(!render_probe.Start()) return Fail();
    base::RunLoop probe_loop;
    bool probe_ok=false;
    render_probe.task_runner()->PostTaskAndReplyWithResult(FROM_HERE,
        base::BindOnce([](ui::SurfaceFactoryOzone* factory,gfx::AcceleratedWidget widget) {
          auto surface=factory->CreateCanvasForWidget(widget);
          if(!surface || factory->CreateCanvasForWidget(gfx::kNullAcceleratedWidget)) return false;
          surface->ResizeCanvas(gfx::Size(32,32),1.0f);
          return surface->GetCanvas()!=nullptr;
        },base::Unretained(platform->GetSurfaceFactoryOzone()),delegate_.widget()),
        base::BindOnce([](bool* success,base::RunLoop* loop,bool value) {
          *success=value; loop->Quit();
        },&probe_ok,base::Unretained(&probe_loop)));
    probe_loop.Run();
    render_probe.Stop();
    if(!probe_ok) return Fail();
    printf("Native Ozone: render thread factory lookup ok\n");
    canvas_ = platform->GetSurfaceFactoryOzone()->CreateCanvasForWidget(delegate_.widget());
    if (!canvas_) return Fail();
    window_->Show();
    window_->Activate();
    phase_ = 1;
    Draw();
    timeout_.Start(FROM_HERE,base::Seconds(30),base::BindOnce([](Runtime* runtime) {
      runtime->Fail();
    },base::Unretained(this)));
    loop_.Run();
    return result_;
  }
  std::unique_ptr<ui::PlatformWindow> window_;
  Delegate delegate_;

 private:
  int Fail() {
    printf("Native Ozone: FAILED\n");
    result_ = 1;
    loop_.Quit();
    return 1;
  }
  void Draw() {
    if (!canvas_) { Fail(); return; }
    const gfx::Size size(window_->GetBoundsInPixels().size());
    canvas_->ResizeCanvas(size,1.0f);
    auto* canvas = canvas_->GetCanvas();
    if (!canvas) { Fail(); return; }
    canvas->clear(SkColorSetRGB(17,25,39));
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(phase_ >= 3 ? SkColorSetRGB(245,197,24) : SkColorSetRGB(36,161,72));
    canvas->drawCircle(60+phase_*50,90,24,paint);
    canvas_->PresentCanvas(gfx::Rect(size));
    canvas_->OnSwapBuffers(base::BindOnce(&Runtime::DidSwap,base::Unretained(this)),
                           gfx::FrameData(phase_));
  }
  void DidSwap(const gfx::Size& size) {
    if (size != window_->GetBoundsInPixels().size()) { Fail(); return; }
    if (phase_ == 1) {
      printf("Native Ozone: registered window and software factory ok\n");
      phase_ = 2;
      // Last request must survive while the first reconfiguration is pending.
      window_->SetBoundsInPixels(gfx::Rect(110,170,460,260));
      window_->SetBoundsInPixels(gfx::Rect(120,180,480,280));
    } else if (phase_ == 2) {
      printf("Native Ozone: delegate resize ready for input\n");
      phase_ = 3;
    } else if (phase_ == 3) {
      printf("Native Ozone: keyboard raw and text events rendered\n");
      phase_ = 4;
    }
  }
  void BoundsChanged() {
    if (phase_ == 2 && window_->GetBoundsInPixels() == gfx::Rect(120,180,480,280))
      Draw();
  }
  void Event(ui::Event* event) {
    if (event->IsKeyEvent()) {
      auto* key = event->AsKeyEvent();
      if (key->type() == ui::EventType::kKeyPressed && key->code() == ui::DomCode::US_A) {
        if (key->is_char()) text_a_ = key->GetCharacter() == u'a';
        else raw_a_ = true;
        if (phase_ == 3 && raw_a_ && text_a_) Draw();
      }
      if (key->type() == ui::EventType::kKeyPressed && key->code() == ui::DomCode::ESCAPE) {
        if (phase_ != 5 || !delegate_.active()) Fail();
        else window_->Close();
      }
    } else if (event->type() == ui::EventType::kMousePressed) {
      mouse_ = true;
    } else if (event->type() == ui::EventType::kMousewheel) {
      wheel_ = event->AsMouseWheelEvent()->y_offset() > 0;
    }
    if (phase_ == 4 && mouse_ && wheel_) {
      printf("Native Ozone: pointer button and wheel events ok\n");
      phase_ = 5;
    }
  }
  void CloseRequest() { window_->Close(); }
  void Closed() {
    if(ui::OzonePlatform::GetInstance()->GetSurfaceFactoryOzone()->CreateCanvasForWidget(delegate_.widget())) {
      Fail(); return;
    }
    if (phase_ == 5) {
      printf("Native Ozone: closed\n");
      result_ = 0;
    } else {
      result_ = 1;
    }
    loop_.Quit();
  }
  std::unique_ptr<ui::PlatformScreen> screen_;
  std::unique_ptr<ui::SurfaceOzoneCanvas> canvas_;
  base::RunLoop loop_;
  base::OneShotTimer timeout_;
  int phase_ = 0;
  int result_ = 1;
  bool raw_a_ = false,text_a_ = false,mouse_ = false,wheel_ = false;
};

void Delegate::OnBoundsChanged(const BoundsChange& change) { runtime_->BoundsChanged(); }
void Delegate::DispatchEvent(ui::Event* event) { runtime_->Event(event); }
void Delegate::OnCloseRequest() { runtime_->CloseRequest(); }
void Delegate::OnClosed() { runtime_->Closed(); }
}  // namespace

extern "C" int main(int argc,char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc,argv);
  if(!base::PathService::Override(base::DIR_ASSETS,base::FilePath("/bin")) ||
     !base::i18n::InitializeICU()) return 1;
  if (!CheckNativeCertificates()) return 1;
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::UI);
  if (!ui::OzonePlatform::InitializeForUI(ui::OzonePlatform::InitParams())) return 1;
  Runtime runtime;
  return runtime.Run();
}
