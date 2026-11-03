#ifndef CHARMOS_WINDOW_EVENT_SOURCE_H_
#define CHARMOS_WINDOW_EVENT_SOURCE_H_

#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_pump_for_ui.h"
#include "base/sequence_checker.h"
#include <charm/window.hpp>

namespace ui {
class WindowEventSourceCharmOS final : public base::MessagePumpForUI::FdWatcher {
 public:
  using Callback=base::RepeatingCallback<void(charm::Status,const abi::wm::WindowEvent&)>;
  WindowEventSourceCharmOS();
  ~WindowEventSourceCharmOS() override;
  WindowEventSourceCharmOS(const WindowEventSourceCharmOS&)=delete;
  WindowEventSourceCharmOS& operator=(const WindowEventSourceCharmOS&)=delete;
  bool Start(charm::gui::Window* window,Callback callback);
  void Stop();

 private:
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;
  void Fail(charm::Status status);
  SEQUENCE_CHECKER(sequence_checker_);
  charm::gui::Window* window_=nullptr;
  Callback callback_;
  base::ScopedFD readiness_;
  base::MessagePumpForUI::FdWatchController controller_{FROM_HERE};
  base::WeakPtrFactory<WindowEventSourceCharmOS> weak_factory_{this};
};
}  // namespace ui
#endif
