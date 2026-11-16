#include "build/charmos/skill_parser_charmos.h"

#include <string_view>
#include <utility>

#include "base/strings/string_util.h"

namespace skills::ffi {
namespace {

std::string Trim(std::string_view value) {
  return std::string(base::TrimWhitespaceASCII(value, base::TRIM_ALL));
}

}  // namespace

SkillParseResult parse_skill_yaml_frontmatter(const std::string& content) {
  SkillParseResult result;
  std::string normalized = content;
  size_t position = 0;
  while ((position = normalized.find("\r\n", position)) != std::string::npos) {
    normalized.replace(position, 2, "\n");
  }

  const size_t boundary = normalized.find("\n---");
  if (boundary == std::string::npos) {
    return result;
  }
  std::string_view frontmatter(normalized.data(), boundary);
  while (frontmatter.starts_with("---")) {
    frontmatter.remove_prefix(3);
  }
  std::string metadata = Trim(frontmatter);

  std::string_view body(normalized);
  body.remove_prefix(boundary + 4);
  while (body.starts_with('\n')) {
    body.remove_prefix(1);
  }
  result.prompt = Trim(body);

  while (!metadata.empty()) {
    const size_t newline = metadata.find('\n');
    std::string line = Trim(std::string_view(metadata).substr(0, newline));
    if (newline == std::string::npos) {
      metadata.clear();
    } else {
      metadata.erase(0, newline + 1);
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = Trim(std::string_view(line).substr(0, colon));
    std::string value = Trim(std::string_view(line).substr(colon + 1));
    const size_t first = value.find_first_not_of(" \"'");
    if (first == std::string::npos) {
      value.clear();
    } else {
      const size_t last = value.find_last_not_of(" \"'");
      value = value.substr(first, last - first + 1);
    }
    if (key == "name") {
      result.name = std::move(value);
    } else if (key == "description") {
      result.description = std::move(value);
    }
  }
  result.success = !result.name.empty();
  return result;
}

}  // namespace skills::ffi
