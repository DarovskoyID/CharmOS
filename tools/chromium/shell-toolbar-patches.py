"""Browser toolbar for the CharmOS content shell (upstream content/shell views).

Single dark row, matching the reference design:
  [back] [search engine] [reload] [ shield | address | page title ] [more] [star]
Bookmarks are listed in the more-menu. The file is hash-checked upstream
content; prepare-platform.py applies these replacements and refuses unknown
local edits.
"""

SHELL_VIEWS = "content/shell/browser/shell_platform_delegate_views.cc"
SHELL_VIEWS_DIGEST = "421f3f4318a1883de8bb3e26746b87b379901c03d3b8b68d9090b533727966bc"

INCLUDES_BEFORE = """#include "ui/views/background.h"
"""
INCLUDES_AFTER = """#include "ui/views/background.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/save_page_type.h"
#include "content/public/common/content_client.h"
#include "content/shell/browser/shell_content_browser_client.h"
#include "ui/base/models/image_model.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/vector_icons.h"
#include <cctype>
#include <set>
#include <string>
"""

HELPERS_BEFORE = """// Maintain the UI controls and web view for content shell
"""
HELPERS_AFTER = """// CharmOS browser chrome. Profile state is kept out of incognito storage.
constexpr SkColor kToolbarColor = SkColorSetRGB(0xff, 0xff, 0xff);
constexpr SkColor kPillColor = SkColorSetRGB(0xf1, 0xf3, 0xf4);
constexpr SkColor kIconColor = SkColorSetRGB(0x3c, 0x40, 0x43);
constexpr int kIconSize = 20;

bool& ToolbarPopupsSupported() {
  static bool supported = true;
  return supported;
}

bool& YandexSearch() {
  static bool yandex = true;
  return yandex;
}

std::set<std::string>& BookmarkStore() {
  static std::set<std::string> bookmarks;
  return bookmarks;
}

std::vector<std::string>& HistoryStore() {
  static std::vector<std::string> entries;
  return entries;
}

std::vector<std::string> ReadLines(const char* path) {
  std::string contents;
  base::ReadFileToString(base::FilePath(FILE_PATH_LITERAL(path)), &contents);
  return base::SplitString(contents, "\\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

void WriteLines(const char* path, const std::vector<std::string>& lines) {
  std::string contents;
  for (const auto& line : lines) contents += line + "\\n";
  base::WriteFile(base::FilePath(FILE_PATH_LITERAL(path)), contents);
}

void EnsureBrowserState() {
  static bool loaded = false;
  if (loaded) return;
  loaded = true;
  for (auto& url : ReadLines("/data/browser-bookmarks.txt"))
    BookmarkStore().insert(std::move(url));
  HistoryStore() = ReadLines("/data/browser-history.txt");
  std::vector<std::string> engine = ReadLines("/data/browser-settings.txt");
  YandexSearch() = engine.empty() || engine.front() != "google";
}

std::set<std::string>& Bookmarks() {
  EnsureBrowserState();
  return BookmarkStore();
}

std::vector<std::string>& HistoryEntries() {
  EnsureBrowserState();
  return HistoryStore();
}

void SaveEngine() {
  WriteLines("/data/browser-settings.txt", {YandexSearch() ? "yandex" : "google"});
}

void SaveBookmarks() {
  WriteLines("/data/browser-bookmarks.txt",
             std::vector<std::string>(BookmarkStore().begin(), BookmarkStore().end()));
}

void RecordHistory(WebContents* contents) {
  if (contents->GetBrowserContext()->IsOffTheRecord()) return;
  std::string url = contents->GetLastCommittedURL().spec();
  if (url.empty() || url == "about:blank") return;
  auto& entries = HistoryEntries();
  if (!entries.empty() && entries.back() == url) return;
  entries.push_back(url);
  base::File history(base::FilePath(FILE_PATH_LITERAL("/data/browser-history.txt")),
                     base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  if (history.IsValid()) {
    std::string line = url + "\n";
    history.WriteAtCurrentPos(line.data(), static_cast<int>(line.size()));
  }
}

std::u16string EngineLabel() {
  EnsureBrowserState();
  return YandexSearch() ? u"\\u042f" : u"G";
}

// Typed text that is not a URL goes to the selected search engine.
bool LooksLikeSearch(const std::string& text) {
  return text.find(' ') != std::string::npos || text.find('.') == std::string::npos ||
         text.find(':') != std::string::npos;
}

std::string SearchUrl(const std::string& text) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string encoded;
  for (unsigned char c : text) {
    if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += kHex[c >> 4];
      encoded += kHex[c & 15];
    }
  }
  if (YandexSearch()) return "https://yandex.ru/search/?text=" + encoded;
  return "https://www.google.com/search?q=" + encoded;
}

// Menu model delegate with a single command callback and per-item state.
class ToolbarMenuDelegate : public ui::SimpleMenuModel::Delegate {
 public:
  explicit ToolbarMenuDelegate(base::RepeatingCallback<void(int)> on_command)
      : on_command_(std::move(on_command)) {}

  void SetDisabled(int command_id) { disabled_.insert(command_id); }
  void SetChecked(int command_id, bool checked) {
    if (checked) checked_.insert(command_id);
  }

  bool IsCommandIdChecked(int command_id) const override {
    return checked_.contains(command_id);
  }
  bool IsCommandIdEnabled(int command_id) const override {
    return !disabled_.contains(command_id);
  }
  bool GetAcceleratorForCommandId(int, ui::Accelerator*) const override {
    return false;
  }
  void ExecuteCommand(int command_id, int) override { on_command_.Run(command_id); }

 private:
  base::RepeatingCallback<void(int)> on_command_;
  std::set<int> disabled_;
  std::set<int> checked_;
};

// Command ids for the toolbar menu.
constexpr int kCmdZoomIn = 1;
constexpr int kCmdZoomOut = 2;
constexpr int kCmdZoomReset = 3;
constexpr int kCmdSaveHtml = 4;
constexpr int kCmdPrint = 5;
constexpr int kCmdDevTools = 6;
constexpr int kCmdBookmarkToggle = 7;
constexpr int kCmdEngineGoogle = 10;
constexpr int kCmdEngineYandex = 11;
constexpr int kCmdNewTab = 12;
constexpr int kCmdNewIncognito = 13;
constexpr int kCmdClearHistory = 14;
constexpr int kCmdNoBookmarks = 98;
constexpr int kCmdNoDownloads = 97;
constexpr int kCmdBookmarkBase = 100;
constexpr int kCmdTabBase = 1000;
constexpr int kCmdHistoryBase = 2000;

"""

BLOCK_BEFORE = """    if (!Shell::ShouldHideToolbar()) {
      builder.AddChild(
          views::Builder<views::FlexLayoutView>()
              .CopyAddressTo(&toolbar_view_)
              .SetOrientation(views::LayoutOrientation::kHorizontal)
              // Top padding = 2, Bottom padding = 5
              .SetProperty(views::kMarginsKey, gfx::Insets::TLBR(2, 0, 5, 0))
              .AddChildren(
                  views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&back_button_)
                      .SetText(u"Back")
                      .SetCallback(base::BindRepeating(
                          &Shell::GoBackOrForward,
                          base::Unretained(shell_.get()), -1))
                      .SetProperty(views::kFlexBehaviorKey,
                                   views::FlexSpecification(base::BindRepeating(
                                       toolbar_button_rule))),
                  views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&forward_button_)
                      .SetText(u"Forward")
                      .SetCallback(base::BindRepeating(
                          &Shell::GoBackOrForward,
                          base::Unretained(shell_.get()), 1))
                      .SetProperty(views::kFlexBehaviorKey,
                                   views::FlexSpecification(base::BindRepeating(
                                       toolbar_button_rule))),
                  views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&refresh_button_)
                      .SetText(u"Refresh")
                      .SetCallback(base::BindRepeating(
                          &Shell::Reload, base::Unretained(shell_.get())))
                      .SetProperty(views::kFlexBehaviorKey,
                                   views::FlexSpecification(base::BindRepeating(
                                       toolbar_button_rule))),
                  views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&stop_button_)
                      .SetText(u"Stop")
                      .SetCallback(base::BindRepeating(
                          &Shell::Stop, base::Unretained(shell_.get())))
                      .SetProperty(views::kFlexBehaviorKey,
                                   views::FlexSpecification(base::BindRepeating(
                                       toolbar_button_rule))),
                  views::Builder<views::Textfield>()
                      .CopyAddressTo(&url_entry_)
                      .SetAccessibleName(u"Enter URL")
                      .SetController(this)
                      .SetTextInputType(ui::TextInputType::TEXT_INPUT_TYPE_URL)
                      .SetProperty(
                          views::kFlexBehaviorKey,
                          views::FlexSpecification(
                              views::LayoutOrientation::kHorizontal,
                              views::MinimumFlexSizeRule::kScaleToMinimum,
                              views::MaximumFlexSizeRule::kUnbounded))
                      // Left padding  = 2, Right padding = 2
                      .SetProperty(views::kMarginsKey,
                                   gfx::Insets::TLBR(0, 2, 0, 2))));
    }
"""

BLOCK_AFTER = """    if (!Shell::ShouldHideToolbar()) {
      builder.AddChild(
          views::Builder<views::FlexLayoutView>()
              .CopyAddressTo(&tab_strip_)
              .SetOrientation(views::LayoutOrientation::kHorizontal)
              .SetBackground(views::CreateSolidBackground(kToolbarColor))
              .SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 10, 0, 10)));
      builder.AddChild(
          views::Builder<views::FlexLayoutView>()
              .CopyAddressTo(&toolbar_view_)
              .SetOrientation(views::LayoutOrientation::kHorizontal)
              .SetBackground(views::CreateSolidBackground(kToolbarColor))
              .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
              .SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 10, 6, 10))
              .AddChildren(
                  IconButton(&back_button_, vector_icons::kArrowBackIcon,
                             u"Назад",
                             base::BindRepeating(
                                 &Shell::GoBackOrForward,
                                 base::Unretained(shell_.get()), -1)),
                  IconButton(&forward_button_, vector_icons::kArrowForwardIcon,
                             u"Вперёд",
                             base::BindRepeating(
                                 &Shell::GoBackOrForward,
                                 base::Unretained(shell_.get()), 1))
                      .SetVisible(false),
                  views::Builder<views::MdTextButton>()
                      .CopyAddressTo(&engine_button_)
                      .SetText(EngineLabel())
                      .SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 6, 0, 6))
                      .SetCallback(base::BindRepeating(
                          &ShellView::ToggleEngine, base::Unretained(this))),
                  IconButton(&refresh_button_,
                             vector_icons::kReloadChromeRefreshOldIcon,
                             u"Обновить",
                             base::BindRepeating(
                                 &Shell::Reload,
                                 base::Unretained(shell_.get()))),
                  IconButton(&stop_button_,
                             vector_icons::kCancelChromeRefreshOldIcon,
                             u"Остановить",
                             base::BindRepeating(
                                 &Shell::Stop, base::Unretained(shell_.get())))
                      .SetVisible(false),
                  views::Builder<views::FlexLayoutView>()
                      .CopyAddressTo(&url_pill_)
                      .SetOrientation(views::LayoutOrientation::kHorizontal)
                      .SetBackground(views::CreateRoundedRectBackground(kPillColor, 18))
                      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
                      .SetProperty(
                          views::kFlexBehaviorKey,
                          views::FlexSpecification(
                              views::LayoutOrientation::kHorizontal,
                              views::MinimumFlexSizeRule::kScaleToMinimum,
                              views::MaximumFlexSizeRule::kUnbounded))
                      .SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 10, 0, 10))
                      .AddChildren(
                          views::Builder<views::ImageView>()
                              .SetImage(ui::ImageModel::FromVectorIcon(
                                  vector_icons::kShieldIcon, kIconColor, kIconSize))
                              .SetProperty(views::kMarginsKey,
                                           gfx::Insets::TLBR(0, 10, 0, 6)),
                          views::Builder<views::Textfield>()
                              .CopyAddressTo(&url_entry_)
                              .SetAccessibleName(u"Enter URL")
                              .SetController(this)
                              .SetTextInputType(ui::TextInputType::TEXT_INPUT_TYPE_URL)
                              .SetBackgroundColor(kPillColor)
                              .SetBorder(views::CreateEmptyBorder(gfx::Insets()))
                              .CustomConfigure(base::BindOnce(
                                  [](views::Textfield* field) {
                                    field->SetColor(kIconColor);
                                  }))
                              .SetProperty(
                                  views::kFlexBehaviorKey,
                                  views::FlexSpecification(
                                      views::LayoutOrientation::kHorizontal,
                                      views::MinimumFlexSizeRule::kScaleToMinimum,
                                      views::MaximumFlexSizeRule::kUnbounded)),
                          views::Builder<views::Label>()
                              .CopyAddressTo(&title_label_)
                              .SetEnabledColor(kIconColor)
                              .SetProperty(
                                  views::kFlexBehaviorKey,
                                  views::FlexSpecification(
                                      views::LayoutOrientation::kHorizontal,
                                      views::MinimumFlexSizeRule::kScaleToMinimum,
                                      views::MaximumFlexSizeRule::kUnbounded))
                              .SetProperty(views::kMarginsKey,
                                           gfx::Insets::TLBR(0, 8, 0, 8))),
                  IconButton(&downloads_button_, vector_icons::kFolderOpenIcon,
                             u"Загрузки",
                             base::BindRepeating(&ShellView::OpenDownloadsMenu,
                                                 base::Unretained(this))),
                  IconButton(&more_button_, views::kMoreHorizIcon,
                             u"Меню",
                             base::BindRepeating(&ShellView::OpenMainMenu,
                                                 base::Unretained(this))),
                  IconButton(&star_button_, vector_icons::kStarOldIcon,
                             u"Добавить в закладки",
                             base::BindRepeating(&ShellView::ToggleBookmark,
                                                 base::Unretained(this)))));
    }
"""

RULE_BEFORE = """    auto toolbar_button_rule = [](const views::View* view,
                                  const views::SizeBounds& size_bounds) {
      gfx::Size preferred_size = view->GetPreferredSize({});
      if (size_bounds != views::SizeBounds() &&
          size_bounds.width().is_bounded()) {
        preferred_size.set_width(std::max(
            std::min(size_bounds.width().value(), preferred_size.width()),
            preferred_size.width() / 2));
      }
      return preferred_size;
    };

"""

ENABLE_BEFORE = """    } else if (control == FORWARD_BUTTON) {
      forward_button_->SetState(is_enabled ? views::Button::STATE_NORMAL
                                           : views::Button::STATE_DISABLED);
    } else if (control == STOP_BUTTON) {
      stop_button_->SetState(is_enabled ? views::Button::STATE_NORMAL
                                        : views::Button::STATE_DISABLED);
    }"""

ENABLE_AFTER = """    } else if (control == FORWARD_BUTTON) {
      // Forward appears only when there is a page to go to.
      forward_button_->SetVisible(is_enabled);
    } else if (control == STOP_BUTTON) {
      // Stop is shown only while a page is loading.
      stop_button_->SetVisible(is_enabled);
    }"""

SET_WEB_CONTENTS_BEFORE = """  void SetWebContents(WebContents* web_contents, const gfx::Size& size) {
"""
SET_WEB_CONTENTS_AFTER = """  void SetTitleText(const std::u16string& title) {
    if (title_label_) title_label_->SetText(title);
    RebuildTabStrip();
  }

  void SetWebContents(WebContents* web_contents, const gfx::Size& size) {
"""

MEMBERS_BEFORE = """  raw_ptr<views::Textfield> url_entry_ = nullptr;
"""
MEMBERS_AFTER = """  raw_ptr<views::Textfield> url_entry_ = nullptr;
  raw_ptr<views::View> tab_strip_ = nullptr;
  raw_ptr<views::View> url_pill_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Button> downloads_button_ = nullptr;
  raw_ptr<views::Button> more_button_ = nullptr;
  raw_ptr<views::ImageButton> star_button_ = nullptr;
  raw_ptr<views::LabelButton> engine_button_ = nullptr;

  // An open menu owns its model, delegate, adapter and runner. Declaration
  // order makes the runner die first.
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<ToolbarMenuDelegate> menu_delegate_;
  std::unique_ptr<views::MenuModelAdapter> menu_adapter_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
"""

METHODS_BEFORE = """  void InitAccelerators() {
"""
METHODS_AFTER = """  template <typename T>
  views::Builder<views::ImageButton> IconButton(raw_ptr<T>* out,
                                                const gfx::VectorIcon& icon,
                                                std::u16string tooltip,
                                                base::RepeatingClosure action) {
    return views::Builder<views::ImageButton>()
        .CopyAddressTo(out)
        .SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromVectorIcon(icon, kIconColor, kIconSize))
        .SetAccessibleName(std::move(tooltip))
        .SetCallback(base::BindRepeating(
            [](base::RepeatingClosure run) {
              LOG(ERROR) << "charmos-toolbar: pressed";
              run.Run();
            },
            std::move(action)));
  }

  void RebuildTabStrip() {
    if (!tab_strip_) return;
    tab_strip_->RemoveAllChildViews();
    for (size_t i = 0; i < shell_->tab_count(); ++i) {
      std::u16string title = shell_->TabTitle(i);
      if (title.empty()) title = u"Новая вкладка";
      if (title.size() > 24) title = title.substr(0, 23) + u"…";
      if (i + 1 == shell_->tab_count()) title = u"• " + title;
      tab_strip_->AddChildView(
          views::Builder<views::MdTextButton>()
              .SetText(title)
              .SetCallback(base::BindRepeating(
                  &Shell::SelectTab, base::Unretained(shell_.get()), i))
              .Build());
    }
    tab_strip_->AddChildView(
        views::Builder<views::MdTextButton>()
            .SetText(u"+")
            .SetAccessibleName(u"Новая вкладка")
            .SetCallback(base::BindRepeating(&ShellView::CreateTab,
                base::Unretained(this),
                shell_->web_contents()->GetBrowserContext()->IsOffTheRecord()))
            .Build());
    if (shell_->tab_count() > 1) {
      tab_strip_->AddChildView(
          views::Builder<views::MdTextButton>()
              .SetText(u"×")
              .SetAccessibleName(u"Закрыть вкладку")
              .SetCallback(base::BindRepeating(
                  &Shell::CloseActiveTab, base::Unretained(shell_.get())))
              .Build());
    }
    tab_strip_->Layout();
  }

  void CreateTab(bool incognito) {
    auto* client = static_cast<ShellContentBrowserClient*>(
        GetContentClient()->browser());
    BrowserContext* context = incognito
        ? client->off_the_record_browser_context()
        : client->browser_context();
    shell_->NewTab(context, GURL("about:blank"));
  }

  void ToggleEngine() {
    YandexSearch() = !YandexSearch();
    SaveEngine();
    engine_button_->SetText(EngineLabel());
  }

  void ToggleBookmark() {
    WebContents* contents = shell_->web_contents();
    if (!contents) return;
    std::string url = contents->GetLastCommittedURL().spec();
    auto& bookmarks = Bookmarks();
    if (bookmarks.count(url)) {
      bookmarks.erase(url);
    } else {
      bookmarks.insert(url);
    }
    SaveBookmarks();
    UpdateStar();
  }

  void UpdateStar() {
    WebContents* contents = shell_->web_contents();
    bool saved = contents && Bookmarks().count(contents->GetLastCommittedURL().spec());
    star_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(
            saved ? vector_icons::kStarFilledIcon : vector_icons::kStarOldIcon,
            kIconColor, kIconSize));
  }

  void OpenMainMenu() {
    auto delegate = std::make_unique<ToolbarMenuDelegate>(
        base::BindRepeating(&ShellView::OnMenuCommand, base::Unretained(this)));
    delegate->SetChecked(kCmdEngineGoogle, !YandexSearch());
    delegate->SetChecked(kCmdEngineYandex, YandexSearch());
    delegate->SetDisabled(kCmdPrint);
    delegate->SetDisabled(kCmdDevTools);
    delegate->SetDisabled(kCmdNoBookmarks);
    auto* raw_delegate = delegate.get();
    auto model = std::make_unique<ui::SimpleMenuModel>(raw_delegate);
    model->AddItem(kCmdZoomIn, u"Увеличить");
    model->AddItem(kCmdZoomOut, u"Уменьшить");
    model->AddItem(kCmdZoomReset, u"Масштаб 100%");
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItem(kCmdNewTab, u"Новая вкладка");
    model->AddItem(kCmdNewIncognito, u"Новое окно инкогнито");
    for (size_t i = 0; i < shell_->tab_count(); ++i) {
      std::u16string title = shell_->TabTitle(i);
      model->AddItem(kCmdTabBase + static_cast<int>(i),
                     title.empty() ? u"Новая вкладка" : title);
    }
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    size_t history_start = HistoryEntries().size() > 10
        ? HistoryEntries().size() - 10 : 0;
    for (size_t i = history_start; i < HistoryEntries().size(); ++i) {
      model->AddItem(kCmdHistoryBase + static_cast<int>(i),
                     base::ASCIIToUTF16(HistoryEntries()[i]));
    }
    model->AddItem(kCmdClearHistory, u"Очистить историю");
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItem(kCmdBookmarkToggle, u"Добавить в закладки");
    if (Bookmarks().empty()) {
      model->AddItem(kCmdNoBookmarks, u"Закладок пока нет");
    }
    int index = 0;
    for (const std::string& url : Bookmarks()) {
      model->AddItem(kCmdBookmarkBase + index++, base::ASCIIToUTF16(url));
    }
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItem(kCmdSaveHtml, u"Сохранить страницу как HTML");
    model->AddItem(kCmdPrint, u"Печать (пока недоступна)");
    model->AddItem(kCmdDevTools, u"Панель разработчика (пока недоступна)");
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddCheckItem(kCmdEngineGoogle, u"Поиск: Google");
    model->AddCheckItem(kCmdEngineYandex, u"Поиск: Яндекс");
    RunToolbarMenu(more_button_, std::move(model), std::move(delegate));
  }

  void OpenDownloadsMenu() {
    auto delegate = std::make_unique<ToolbarMenuDelegate>(
        base::BindRepeating(&ShellView::OnMenuCommand, base::Unretained(this)));
    delegate->SetDisabled(kCmdNoDownloads);
    auto* raw_delegate = delegate.get();
    auto model = std::make_unique<ui::SimpleMenuModel>(raw_delegate);
    model->AddItem(kCmdNoDownloads, u"Загрузок пока нет");
    RunToolbarMenu(downloads_button_, std::move(model), std::move(delegate));
  }

  void RunToolbarMenu(views::Button* anchor,
                      std::unique_ptr<ui::SimpleMenuModel> model,
                      std::unique_ptr<ToolbarMenuDelegate> delegate) {
    // Menus are popup windows; CharmOS windows cannot be popups yet, and the
    // platform aborts the browser on the first attempt. Stay closed until then.
    if (!ToolbarPopupsSupported()) {
      LOG(ERROR) << "charmos-toolbar: menu skipped, popup windows unsupported";
      return;
    }
    menu_runner_.reset();
    menu_adapter_.reset();
    menu_model_ = std::move(model);
    menu_delegate_ = std::move(delegate);
    menu_adapter_ = std::make_unique<views::MenuModelAdapter>(menu_model_.get());
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_adapter_->CreateMenu(), views::MenuRunner::HAS_MNEMONICS);
    menu_runner_->RunMenuAt(anchor->GetWidget(), nullptr,
                            anchor->GetBoundsInScreen(),
                            views::MenuAnchorPosition::kTopRight,
                            ui::mojom::MenuSourceType::kMouse);
  }

  void OnMenuCommand(int command_id) {
    WebContents* contents = shell_->web_contents();
    if (!contents) return;
    if (command_id >= kCmdHistoryBase) {
      size_t index = static_cast<size_t>(command_id - kCmdHistoryBase);
      if (index < HistoryEntries().size()) shell_->LoadURL(GURL(HistoryEntries()[index]));
      return;
    }
    if (command_id >= kCmdTabBase) {
      size_t index = static_cast<size_t>(command_id - kCmdTabBase);
      if (index < shell_->tab_count()) shell_->SelectTab(index);
      return;
    }
    if (command_id >= kCmdBookmarkBase) {
      int index = command_id - kCmdBookmarkBase;
      int current = 0;
      for (const std::string& url : Bookmarks()) {
        if (current++ == index) {
          shell_->LoadURL(GURL(url));
          break;
        }
      }
      return;
    }
    switch (command_id) {
      case kCmdZoomIn:
        HostZoomMap::SetZoomLevel(contents, HostZoomMap::GetZoomLevel(contents) + 0.5);
        break;
      case kCmdZoomOut:
        HostZoomMap::SetZoomLevel(contents, HostZoomMap::GetZoomLevel(contents) - 0.5);
        break;
      case kCmdZoomReset:
        HostZoomMap::SetZoomLevel(contents, 0.0);
        break;
      case kCmdSaveHtml:
        contents->SavePage(base::FilePath(FILE_PATH_LITERAL("/data/saved-page.html")),
                           base::FilePath(), SAVE_PAGE_TYPE_AS_ONLY_HTML);
        break;
      case kCmdBookmarkToggle:
        ToggleBookmark();
        break;
      case kCmdEngineGoogle:
        YandexSearch() = false;
        SaveEngine();
        engine_button_->SetText(EngineLabel());
        break;
      case kCmdEngineYandex:
        YandexSearch() = true;
        SaveEngine();
        engine_button_->SetText(EngineLabel());
        break;
      case kCmdNewTab:
        CreateTab(false);
        break;
      case kCmdNewIncognito:
        CreateTab(true);
        break;
      case kCmdClearHistory:
        HistoryEntries().clear();
        base::DeleteFile(base::FilePath(FILE_PATH_LITERAL("/data/browser-history.txt")));
        break;
      default:
        break;
    }
  }

  void InitAccelerators() {
"""

ADDRESS_BEFORE = """  void SetAddressBarURL(const GURL& url) {
    url_entry_->SetText(base::ASCIIToUTF16(url.spec()));
  }
"""
ADDRESS_AFTER = """  void SetAddressBarURL(const GURL& url) {
    url_entry_->SetText(base::ASCIIToUTF16(url.spec()));
    RecordHistory(shell_->web_contents());
  }
"""

ACCELERATORS_BEFORE = """    for (size_t i = 0; i < std::size(keys); ++i) {
      GetFocusManager()->RegisterAccelerator(
          ui::Accelerator(keys[i], ui::EF_NONE),
          ui::AcceleratorManager::kNormalPriority, this);
    }
  }
"""
ACCELERATORS_AFTER = """    for (size_t i = 0; i < std::size(keys); ++i) {
      GetFocusManager()->RegisterAccelerator(
          ui::Accelerator(keys[i], ui::EF_NONE),
          ui::AcceleratorManager::kNormalPriority, this);
    }
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_T, ui::EF_CONTROL_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_W, ui::EF_CONTROL_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_N, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
  }
"""

ACCELERATOR_PRESS_BEFORE = """    switch (accelerator.key_code()) {
      case ui::VKEY_F5:
"""
ACCELERATOR_PRESS_AFTER = """    switch (accelerator.key_code()) {
      case ui::VKEY_T:
        CreateTab(shell_->web_contents()->GetBrowserContext()->IsOffTheRecord());
        return true;
      case ui::VKEY_W:
        shell_->CloseActiveTab();
        return true;
      case ui::VKEY_N:
        CreateTab(true);
        return true;
      case ui::VKEY_F5:
"""

URL_BEFORE = """      GURL url(text);
      if (!url.has_scheme()) {
        url = GURL(std::string("http://") + text);
        url_entry_->SetText(base::ASCIIToUTF16(url.spec()));
      }
"""
URL_AFTER = """      GURL url(text);
      if (!url.has_scheme() && LooksLikeSearch(text)) {
        url = GURL(SearchUrl(text));
        url_entry_->SetText(base::ASCIIToUTF16(url.spec()));
      } else if (!url.has_scheme()) {
        url = GURL(std::string("http://") + text);
        url_entry_->SetText(base::ASCIIToUTF16(url.spec()));
      }
"""

SET_TITLE_BEFORE = """  shell_data.window_widget->widget_delegate()->SetTitle(title);
"""
SET_TITLE_AFTER = """  shell_data.window_widget->widget_delegate()->SetTitle(title);
  ShellViewForWidget(shell_data.window_widget)->SetTitleText(title);
"""


def shell_toolbar_patches():
    replacements = [
        (INCLUDES_BEFORE, INCLUDES_AFTER),
        (HELPERS_BEFORE, HELPERS_AFTER),
        (RULE_BEFORE, ""),
        (BLOCK_BEFORE, BLOCK_AFTER),
        (ENABLE_BEFORE, ENABLE_AFTER),
        (SET_WEB_CONTENTS_BEFORE, SET_WEB_CONTENTS_AFTER),
        (MEMBERS_BEFORE, MEMBERS_AFTER),
        (METHODS_BEFORE, METHODS_AFTER),
        (ADDRESS_BEFORE, ADDRESS_AFTER),
        (ACCELERATORS_BEFORE, ACCELERATORS_AFTER),
        (ACCELERATOR_PRESS_BEFORE, ACCELERATOR_PRESS_AFTER),
        (URL_BEFORE, URL_AFTER),
        (SET_TITLE_BEFORE, SET_TITLE_AFTER),
    ]
    return {SHELL_VIEWS: (SHELL_VIEWS_DIGEST, replacements)}
