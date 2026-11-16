#ifndef BUILD_CHARMOS_PORTABILITY_HISTORY_CHARMOS_H_
#define BUILD_CHARMOS_PORTABILITY_HISTORY_CHARMOS_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "components/user_data_importer/utility/history_callback_from_rust.h"

namespace user_data_importer {

struct StablePortabilityHistoryEntry {
  bool synced = false;
  std::string title;
  std::string url;
  uint64_t visit_time_unix_epoch_usec = 0;
  uint64_t visit_count = 1;
  uint64_t typed_count = 0;
};

void parse_stable_portability_history(
    int owned_fd,
    std::unique_ptr<StablePortabilityHistoryCallbackFromRust> history_callback,
    size_t history_size_threshold);

}  // namespace user_data_importer

#endif
