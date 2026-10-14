#include "cmdline.hpp"
#include "test.hpp"

using kernel::Cmdline;
using lib::core::StringView;

TEST(cmdline_lookup) {
  Cmdline c(StringView("  ktest=pf  console=serial quiet x="));
  bool found = false;
  CHECK(c.get("ktest", &found) == StringView("pf") && found);
  CHECK(c.get("console") == StringView("serial"));
  CHECK(c.has("quiet") && c.get("quiet").empty());
  CHECK(c.has("x") && c.get("x").empty());
  CHECK(!c.has("ktes") && !c.has("missing"));
  CHECK(!Cmdline().has("ktest"));
}
