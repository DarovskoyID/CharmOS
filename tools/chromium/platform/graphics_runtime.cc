#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/charmos/window_event_source_charmos.h"
#include "skia/ext/codec_utils.h"
#include "skia/ext/font_utils.h"
#include "third_party/skia/include/codec/SkCodec.h"
#include "third_party/skia/include/codec/SkPngDecoder.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFont.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include <charm/window.hpp>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace {
constexpr int kWidth=640,kHeight=400;
int Failure(const char* operation) {
  printf("Skia native: FAILED %s\n",operation);
  return 1;
}
bool CheckPng(const SkPixmap& pixels) {
  auto encoded=skia::EncodePngAsSkData(pixels);
  if(!encoded || encoded->size()<8) return false;
  SkCodec::Result result=SkCodec::kInvalidInput;
  auto codec=SkPngDecoder::Decode(encoded,&result);
  if(!codec || result!=SkCodec::kSuccess || codec->dimensions()!=pixels.dimensions()) return false;
  auto info=pixels.info();
  std::vector<unsigned char> decoded(info.computeMinByteSize());
  if(codec->getPixels(info,decoded.data(),info.minRowBytes())!=SkCodec::kSuccess)
    return false;
  for(int y=0;y<info.height();++y) {
    if(memcmp(pixels.addr(0,y),decoded.data()+static_cast<size_t>(y)*info.minRowBytes(),
              info.minRowBytes())) return false;
  }
  // A truncated stream must not fabricate decoded pixels or crash its process.
  auto truncated=SkData::MakeWithCopy(encoded->data(),8);
  if(SkPngDecoder::Decode(std::move(truncated),&result)) return false;
  return true;
}
void Draw(SkCanvas* canvas,const SkFont& font,unsigned frame,bool key_seen=false) {
  canvas->clear(SkColorSetRGB(17,25,39));
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(SkColorSetRGB(40,58,81));
  canvas->drawRoundRect(SkRect::MakeXYWH(24,24,592,352),20,20,paint);
  paint.setColor(SK_ColorWHITE);
  canvas->drawString("Chromium Skia on CharmOS",48,82,font,paint);
  SkFont small(font);
  small.setSize(17);
  paint.setColor(SkColorSetRGB(164,187,215));
  canvas->drawString("CPU raster / FreeType / native compositor",48,116,small,paint);
  canvas->save();
  canvas->translate(180,230);
  canvas->rotate(static_cast<float>(frame%360));
  paint.setColor(SkColorSetRGB(71,182,230));
  canvas->drawRoundRect(SkRect::MakeXYWH(-58,-58,116,116),16,16,paint);
  canvas->restore();
  paint.setColor(key_seen ? SkColorSetRGB(245,197,24) : SkColorSetRGB(154,224,169));
  canvas->drawCircle(405,230,57,paint);
  paint.setColor(SK_ColorWHITE);
  canvas->drawString("PNG roundtrip verified",48,344,small,paint);
  if(key_seen) canvas->drawString("Native key: A",340,326,small,paint);
}
}  // namespace

extern "C" int main(int argc,char** argv) {
  if(!base::CommandLine::Init(argc,argv)) return Failure("command line");
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::UI);
  bool testing=base::CommandLine::ForCurrentProcess()->HasSwitch("test");
  skia::EnsurePNGDecoderRegistered();
  auto manager=skia::DefaultFontMgr();
  if(!manager || manager->countFamilies()<2) return Failure("font directory");
  auto typeface=manager->matchFamilyStyle("Space Grotesk",SkFontStyle());
  if(!typeface || !typeface->unicharToGlyph('A')) return Failure("FreeType glyphs");
  SkFont font(typeface,26);
  font.setEdging(SkFont::Edging::kAntiAlias);
  auto info=SkImageInfo::Make(kWidth,kHeight,kBGRA_8888_SkColorType,kOpaque_SkAlphaType);
  auto surface=SkSurfaces::Raster(info);
  if(!surface) return Failure("raster allocation");
  Draw(surface->getCanvas(),font,0);
  SkPixmap pixels;
  if(!surface->peekPixels(&pixels) || !CheckPng(pixels)) return Failure("PNG roundtrip");
  printf("Skia native: raster, FreeType glyphs and PNG roundtrip ok\n");
  charm::gui::Window window;
  if(window.create("Native Chromium graphics",kWidth,kHeight)!=charm::Status::Ok)
    return Failure("window creation");
  charm::gui::Damage damage{0,0,kWidth,kHeight};
  auto present=[&]() {
    return window.present_pixels(pixels.addr(),pixels.computeByteSize(),
        static_cast<uint32_t>(pixels.rowBytes()),abi::syscall::kDisplayBgrx,damage);
  };
  if(present()!=charm::Status::Ok) return Failure("first frame");
  unsigned frames=0;
  bool key_seen=false,key_presented=false,test_passed=false;
  int exit_status=0;
  base::RunLoop loop;
  ui::WindowEventSourceCharmOS events;
  ui::WindowEventSourceCharmOS::Callback dispatch;
  auto handle_event=[&](charm::Status result,const abi::wm::WindowEvent& event) {
    if(result!=charm::Status::Ok) {
      exit_status=Failure("window events");
      loop.Quit();
      return;
    }
    if(event.type==abi::wm::kCloseRequest) { events.Stop(); loop.Quit(); return; }
    if(event.type==abi::wm::kInput) {
      abi::syscall::InputEvent input{};
      while(window.next_input(&input)) {
        if(input.type==abi::syscall::kInputKey && input.value==1) {
          if(input.code==0x29) {
            if(testing && !test_passed) exit_status=Failure("early close");
            else printf("Skia native: window closed\n");
            events.Stop();
            loop.Quit();
            return;
          }
          if(input.code==0x04) key_seen=true;
          if(!testing) frames+=15;
        }
      }
    }
    if(event.type!=abi::wm::kFrameDone) return;
    ++frames;
    if(testing && frames==1) {
      if(!events.Start(&window,dispatch)) {
        exit_status=Failure("UI watcher restart");
        loop.Quit();
        return;
      }
      printf("Skia native: UI watcher restart ok\n");
      printf("Skia native: ready for keyboard input\n");
    }
    if(testing && !test_passed && key_presented && frames>=12) {
      printf("Skia native: keyboard input and compositor frame acknowledgments ok\n");
      test_passed=true;
    }
    Draw(surface->getCanvas(),font,frames,key_seen);
    if(present()!=charm::Status::Ok) {
      exit_status=Failure("next frame");
      loop.Quit();
      return;
    }
    key_presented=key_seen;
  };
  dispatch=base::BindRepeating(
      [](decltype(handle_event)* handler,charm::Status status,const abi::wm::WindowEvent& event) {
        (*handler)(status,event);
      },base::Unretained(&handle_event));
  if(!events.Start(&window,dispatch)) return Failure("UI window watcher");
  base::OneShotTimer timeout;
  if(testing) timeout.Start(FROM_HERE,base::Seconds(30),base::BindOnce(
      [](int* status,base::RunLoop* run_loop) {
        *status=Failure("UI event deadline");
        run_loop->Quit();
      },base::Unretained(&exit_status),base::Unretained(&loop)));
  loop.Run();
  events.Stop();
  return exit_status;
}
