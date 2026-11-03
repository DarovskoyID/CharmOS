#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/threading/sequence_bound.h"
#include "base/run_loop.h"
#include "base/task/bind_post_task.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/timer/timer.h"
#include "build/charmos/surface_ozone_canvas_charmos.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/gfx/vsync_provider.h"
#include <stdio.h>
#include <limits>

namespace {
class RenderClient {
 public:
  RenderClient(scoped_refptr<ui::NativeWindowPresenterCharmOS> presenter,
               base::PlatformThreadId ui_thread,base::RepeatingCallback<void(int)> report)
      : canvas_(std::move(presenter)),ui_thread_(ui_thread),report_(std::move(report)) {}
  void Start(gfx::Size size,int phase) {
    if(base::PlatformThread::CurrentId()==ui_thread_) { report_.Run(0); return; }
    size_=size; phase_=phase; frames_=0;
    canvas_.ResizeCanvas(size,std::numeric_limits<float>::quiet_NaN());
    if(canvas_.GetCanvas()) { report_.Run(0); return; }
    canvas_.ResizeCanvas(size,1.0f);
    if(!canvas_.GetCanvas() || !canvas_.SupportsAsyncBufferSwap() ||
       canvas_.MaxFramesPending()!=1 || canvas_.CreateVSyncProvider()) {
      report_.Run(0); return;
    }
    // Invalid damage must report failure, without pretending to display a frame.
    canvas_.PresentCanvas(gfx::Rect(-1,0,20,20));
    canvas_.OnSwapBuffers(base::BindOnce(&RenderClient::InvalidSwap,base::Unretained(this)),gfx::FrameData());
  }
  void KeyFrame() {
    phase_=3; frames_=5;
    Draw();
  }
 private:
  void InvalidSwap(const gfx::Size& size) {
    if(!size.IsEmpty()) { report_.Run(0); return; }
    Draw();
  }
  void Draw() {
    auto* canvas=canvas_.GetCanvas();
    if(!canvas) { report_.Run(0); return; }
    if(!frames_) canvas->clear(SkColorSetRGB(17,25,39));
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(phase_==3 ? SkColorSetRGB(245,197,24) : SkColorSetRGB(36,161,72));
    canvas->drawCircle(60+frames_*50,90,24,paint);
    // Later frames change a small region; the persistent raster and full-frame
    // transfer must preserve earlier circles across alternating native buffers.
    gfx::Rect damage=frames_ ? gfx::Rect(30+frames_*50,60,60,60) : gfx::Rect(size_);
    canvas_.PresentCanvas(damage);
    canvas_.OnSwapBuffers(base::BindOnce(&RenderClient::DidSwap,base::Unretained(this)),gfx::FrameData(frames_));
    bool second_called=false;
    canvas_.OnSwapBuffers(base::BindOnce([](bool* called,const gfx::Size& result) {
      *called=result.IsEmpty();
    },&second_called),gfx::FrameData());
    if(!second_called) report_.Run(0);
  }
  void DidSwap(const gfx::Size& size) {
    if(size!=size_ || base::PlatformThread::CurrentId()==ui_thread_) { report_.Run(0); return; }
    if(++frames_<6) Draw();
    else report_.Run(phase_);
  }
  ui::SurfaceOzoneCanvasCharmOS canvas_;
  base::PlatformThreadId ui_thread_;
  base::RepeatingCallback<void(int)> report_;
  gfx::Size size_;
  int phase_=0,frames_=0;
};
}

extern "C" int main(int argc,char** argv) {
  base::CommandLine::Init(argc,argv);
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::UI);
  base::RunLoop loop;
  base::Thread renderer("native-render");
  if(!renderer.Start()) return 1;
  auto presenter=base::MakeRefCounted<ui::NativeWindowPresenterCharmOS>(
      base::SequencedTaskRunner::GetCurrentDefault());
  bool ready=false,key_done=false;
  int result=0;
  base::SequenceBound<RenderClient> render;
  auto fail=[&] { result=1; printf("Ozone canvas: FAILED\n"); loop.Quit(); };
  auto observe=[&](charm::Status status,const abi::wm::WindowEvent& event) {
    if(status!=charm::Status::Ok) { fail(); return; }
    if(event.type==abi::wm::kConfigured) {
      if(event.value || presenter->window().width()!=480 || presenter->window().height()!=280) {
        fail(); return;
      }
      render.AsyncCall(&RenderClient::Start).WithArgs(gfx::Size(480,280),2);
    }
    if(event.type==abi::wm::kInput) {
      abi::syscall::InputEvent input{};
      while(presenter->window().next_input(&input)) {
        if(input.type!=abi::syscall::kInputKey || input.value!=1) continue;
        if(input.code==0x04 && ready && !key_done) {
          ready=false;
          render.AsyncCall(&RenderClient::KeyFrame);
        }
        if(input.code==0x29) {
          if(!key_done) { fail(); return; }
          printf("Ozone canvas: closed\n"); loop.Quit(); return;
        }
      }
    }
    if(event.type==abi::wm::kCloseRequest) loop.Quit();
  };
  if(presenter->Initialize("Native Ozone software canvas",640,360,
      base::BindRepeating([](decltype(observe)* fn,charm::Status status,const abi::wm::WindowEvent& event) {
        (*fn)(status,event);
      },base::Unretained(&observe)))!=charm::Status::Ok) return 1;
  auto report=[&](int phase) {
    if(!phase) { fail(); return; }
    if(phase==1) {
      printf("Ozone canvas: render thread and real frame acknowledgments ok\n");
      if(presenter->RequestBounds(gfx::Rect(120,180,480,280))!=charm::Status::Ok) fail();
    } else if(phase==2) {
      printf("Ozone canvas: resized surface ready for keyboard input\n"); ready=true;
    } else if(phase==3) {
      printf("Ozone canvas: keyboard frame acknowledged\n"); key_done=true;
    }
  };
  render=base::SequenceBound<RenderClient>(renderer.task_runner(),presenter,
      base::PlatformThread::CurrentId(),base::BindPostTaskToCurrentDefault(base::BindRepeating(
          [](decltype(report)* fn,int phase) { (*fn)(phase); },base::Unretained(&report))));
  render.AsyncCall(&RenderClient::Start).WithArgs(gfx::Size(640,360),1);
  base::OneShotTimer timeout;
  timeout.Start(FROM_HERE,base::Seconds(30),base::BindOnce([](decltype(fail)* fn) { (*fn)(); },base::Unretained(&fail)));
  loop.Run();
  presenter->Close();
  render.Reset();
  renderer.Stop();
  return result;
}
