#include "components/browser_actuator/internal/transport/proto_stream_client/rust_stream_framer.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/bind.h"

namespace browser_actuator {
namespace {

constexpr size_t kMaxMessageBytes = 10 * 1024 * 1024;

enum class VarintResult { kValue, kNeedMore, kMalformed };

VarintResult ReadVarint(std::string_view input, uint64_t* value, size_t* length) {
  *value = 0;
  for (size_t i = 0; i < 10; ++i) {
    if (i >= input.size()) {
      return VarintResult::kNeedMore;
    }
    const uint8_t byte = static_cast<uint8_t>(input[i]);
    if (i == 9) {
      if ((byte & 0x7e) || (byte & 0x80)) {
        return VarintResult::kMalformed;
      }
      *value |= static_cast<uint64_t>(byte & 1) << 63;
      *length = 10;
      return VarintResult::kValue;
    }
    *value |= static_cast<uint64_t>(byte & 0x7f) << (i * 7);
    if (!(byte & 0x80)) {
      *length = i + 1;
      return VarintResult::kValue;
    }
  }
  return VarintResult::kMalformed;
}

}  // namespace

struct RustStreamFramer::Impl {
  std::string pending;
  bool failed = false;
};

RustStreamFramer::RustStreamFramer() : impl_(std::make_unique<Impl>()) {}
RustStreamFramer::~RustStreamFramer() = default;

StreamFramerFactory RustStreamFramer::MakeFactory() {
  return base::BindRepeating([]() -> std::unique_ptr<StreamFramer> {
    return std::make_unique<RustStreamFramer>();
  });
}

StreamFramer::FeedResult RustStreamFramer::Feed(std::string_view chunk) {
  FeedResult result;
  if (!impl_->failed) {
    impl_->pending.append(chunk);
    std::string_view input = impl_->pending;
    size_t position = 0;
    while (position < input.size()) {
      const size_t field_start = position;
      uint64_t tag = 0;
      size_t tag_size = 0;
      VarintResult read = ReadVarint(input.substr(position), &tag, &tag_size);
      if (read == VarintResult::kNeedMore) {
        break;
      }
      if (read == VarintResult::kMalformed || tag > UINT32_MAX || !(tag >> 3)) {
        impl_->failed = true;
        break;
      }
      position += tag_size;
      const uint32_t field_number = tag >> 3;
      const uint32_t wire_type = tag & 7;
      if (wire_type == 0) {
        uint64_t ignored = 0;
        size_t value_size = 0;
        read = ReadVarint(input.substr(position), &ignored, &value_size);
        if (read == VarintResult::kNeedMore) {
          position = field_start;
          break;
        }
        if (read == VarintResult::kMalformed) {
          impl_->failed = true;
          break;
        }
        position += value_size;
        continue;
      }
      if (wire_type == 1 || wire_type == 5) {
        const size_t width = wire_type == 1 ? 8 : 4;
        if (input.size() - position < width) {
          position = field_start;
          break;
        }
        position += width;
        continue;
      }
      if (wire_type != 2) {
        impl_->failed = true;
        break;
      }

      uint64_t length = 0;
      size_t length_size = 0;
      read = ReadVarint(input.substr(position), &length, &length_size);
      if (read == VarintResult::kNeedMore) {
        position = field_start;
        break;
      }
      if (read == VarintResult::kMalformed || length > kMaxMessageBytes) {
        impl_->failed = true;
        break;
      }
      position += length_size;
      if (input.size() - position < length) {
        position = field_start;
        break;
      }
      const std::string_view payload = input.substr(position, length);
      if (field_number == 1) {
        result.messages.emplace_back(payload);
      } else if (field_number == 2) {
        result.status = std::string(payload);
      }
      position += length;
    }
    impl_->pending.erase(0, position);
  }
  if (impl_->failed) {
    impl_->pending.clear();
    result.failed = true;
  }
  return result;
}

}  // namespace browser_actuator
