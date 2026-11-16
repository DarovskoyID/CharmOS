#include "build/charmos/portability_history_charmos.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/scoped_file.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "unistd.h"

namespace user_data_importer {
namespace {

std::optional<uint64_t> ReadCount(const base::DictValue& dict,
                                  std::string_view key,
                                  uint64_t fallback,
                                  bool required) {
  std::optional<double> value = dict.FindDouble(key);
  if (!value) {
    return required ? std::nullopt : std::optional<uint64_t>(fallback);
  }
  if (*value < 0 || *value >= 18446744073709551616.0 ||
      std::trunc(*value) != *value) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(*value);
}

}  // namespace

void parse_stable_portability_history(
    int owned_fd,
    std::unique_ptr<StablePortabilityHistoryCallbackFromRust> history_callback,
    size_t history_size_threshold) {
  base::ScopedFD file(owned_fd);
  std::string input;
  char buffer[4096];
  for (;;) {
    ssize_t count = read(file.get(), buffer, sizeof(buffer));
    if (count > 0) {
      input.append(buffer, count);
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      history_callback->Fail();
      return;
    }
    break;
  }

  std::optional<base::Value> root = base::JSONReader::Read(input, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  const base::DictValue* metadata = root && root->is_dict()
      ? root->GetDict().FindDict("metadata") : nullptr;
  const base::ListValue* visits = root && root->is_dict()
      ? root->GetDict().FindList("history_visits") : nullptr;
  const std::string* data_type = metadata ? metadata->FindString("data_type") : nullptr;
  if (!data_type || *data_type != "history_visits" || !visits) {
    history_callback->Fail();
    return;
  }

  auto batch = std::make_unique<std::vector<StablePortabilityHistoryEntry>>();
  const size_t threshold = std::max<size_t>(history_size_threshold, 1);
  for (const base::Value& value : *visits) {
    if (!value.is_dict()) {
      continue;
    }
    const base::DictValue& dict = value.GetDict();
    const std::string* title = dict.FindString("title");
    const std::string* url = dict.FindString("url");
    auto visit_time = ReadCount(dict, "visit_time_unix_epoch_usec", 0, true);
    auto visit_count = ReadCount(dict, "visit_count", 1, false);
    auto typed_count = ReadCount(dict, "typed_count", 0, false);
    if (!title || !url || !visit_time || !visit_count || !typed_count) {
      continue;
    }
    StablePortabilityHistoryEntry entry;
    entry.synced = dict.FindBool("synced").value_or(false);
    entry.title = *title;
    entry.url = *url;
    entry.visit_time_unix_epoch_usec = *visit_time;
    entry.visit_count = *visit_count;
    entry.typed_count = *typed_count;
    batch->push_back(std::move(entry));
    if (batch->size() >= threshold) {
      history_callback->ImportHistoryEntries(std::move(batch), false);
      batch = std::make_unique<std::vector<StablePortabilityHistoryEntry>>();
    }
  }
  history_callback->ImportHistoryEntries(std::move(batch), true);
}

}  // namespace user_data_importer
