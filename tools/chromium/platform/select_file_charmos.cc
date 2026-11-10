// CharmOS file-selection dialog.
//
// The browser UI does not yet expose a native file picker. Rather than
// returning null (which callers treat as an implementation error), the dialog
// reports cancellation through the normal Listener path. File inputs therefore
// behave as if the user dismissed the picker.
#include <utility>

#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/select_file_policy.h"

namespace ui {

namespace {

class CharmSelectFileDialog : public SelectFileDialog {
 public:
  CharmSelectFileDialog(Listener* listener,
                        std::unique_ptr<SelectFilePolicy> policy)
      : SelectFileDialog(listener, std::move(policy)) {}

  CharmSelectFileDialog(const CharmSelectFileDialog&) = delete;
  CharmSelectFileDialog& operator=(const CharmSelectFileDialog&) = delete;

 protected:
  bool IsRunning(gfx::NativeWindow /*owning_window*/) const override {
    return false;
  }

  void ListenerDestroyed() override { listener_ = nullptr; }

  void SelectFileImpl(Type /*type*/,
                      const std::u16string& /*title*/,
                      const base::FilePath& /*default_path*/,
                      const FileTypeInfo* /*file_types*/,
                      int /*file_type_index*/,
                      const base::FilePath::StringType& /*default_extension*/,
                      gfx::NativeWindow /*owning_window*/,
                      const GURL* /*caller*/) override {
    if (listener_) listener_->FileSelectionCanceled();
  }

  bool HasMultipleFileTypeChoicesImpl() override { return false; }
};

}  // namespace

SelectFileDialog* CreateSelectFileDialog(
    SelectFileDialog::Listener* listener,
    std::unique_ptr<SelectFilePolicy> policy) {
  return new CharmSelectFileDialog(listener, std::move(policy));
}

}  // namespace ui
