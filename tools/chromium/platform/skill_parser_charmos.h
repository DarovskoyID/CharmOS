#ifndef BUILD_CHARMOS_SKILL_PARSER_CHARMOS_H_
#define BUILD_CHARMOS_SKILL_PARSER_CHARMOS_H_

#include <string>

namespace skills::ffi {

struct SkillParseResult {
  std::string name;
  std::string description;
  std::string prompt;
  bool success;
};

SkillParseResult parse_skill_yaml_frontmatter(const std::string& content);

}  // namespace skills::ffi

#endif
