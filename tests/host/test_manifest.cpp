#include <lib/manifest/manifest.hpp>

#include "test.hpp"

using lib::core::StringView;
using lib::manifest::DeviceId;
using lib::manifest::match_score;
using lib::manifest::parse;

namespace {

bool fails_at(const char* text, uint32_t line) {
  auto r = parse(StringView(text));
  return r.is_err() && r.error().line == line;
}

}  // namespace

TEST(manifest_full_example) {
  auto r = parse(StringView(
      "# драйвер учебного устройства QEMU\n"
      "name = \"edu\"\n"
      "\n"
      "[[match]]\n"
      "vendor = 0x1234   # QEMU\n"
      "device = [0x11e8, 0x11E9]\n"
      "score = 100\n"
      "[[match]]\n"
      "class_code = 0x00ff00\n"));
  CHECK(r.is_ok());
  auto m = r.value();
  CHECK(StringView(m.name) == StringView("edu"));
  CHECK(m.match_count == 2);
  CHECK(m.matches[0].has_vendor && m.matches[0].vendor == 0x1234);
  CHECK(m.matches[0].device_count == 2 && m.matches[0].devices[1] == 0x11e9);
  CHECK(m.matches[0].score == 100);
  CHECK(m.matches[1].has_class && m.matches[1].class_code == 0xff00 && m.matches[1].score == 0);

  CHECK(match_score(m, DeviceId{0x1234, 0x11e8, 0}) == 100);
  CHECK(match_score(m, DeviceId{0x8086, 0x1000, 0x00ff00}) == 0);  // по классу
  CHECK(match_score(m, DeviceId{0x1234, 0x0001, 0x010601}) == -1);
}

TEST(manifest_single_device_and_crlf) {
  auto r = parse(StringView("name = \"x-1\"\r\n[[match]]\r\ndevice = 7\r\n"));
  CHECK(r.is_ok() && r.value().matches[0].device_count == 1);
}

TEST(manifest_rejects) {
  CHECK(fails_at("[[match]]\nvendor = 1\n", 3));                    // нет name
  CHECK(fails_at("name = \"a\"\n", 2));                              // нет [[match]]
  CHECK(fails_at("name = \"a\"\nfoo = 1\n", 2));                     // неизвестный ключ
  CHECK(fails_at("name = \"a\"\nname = \"b\"\n", 2));                // повтор
  CHECK(fails_at("name = \"A\"\n", 1));                              // недопустимый символ
  CHECK(fails_at("name = \"a\n", 1));                                // незакрытая строка
  CHECK(fails_at("name = \"a\"\n[match]\n", 2));                     // не [[match]]
  CHECK(fails_at("name = \"a\"\n[[match]]\nvendor = 0x10000\n", 3)); // диапазон
  CHECK(fails_at("name = \"a\"\n[[match]]\nvendor = 1 2\n", 3));     // мусор
  CHECK(fails_at("name = \"a\"\n[[match]]\nvendor = 1\nvendor = 2\n", 4));
  CHECK(fails_at("name = \"a\"\n[[match]]\ndevice = [1, 2\n", 3));
  CHECK(fails_at("name = \"a\"\n[[match]]\nscore = 5\n", 4));        // match без условий
  CHECK(fails_at("name = \"a\"\n[[match]]\nvendor = 99999999999999999999999\n", 3));
  CHECK(fails_at("name = \"a\"\n[[match]]\ndevice = [1,2,3,4,5,6,7,8,9]\n", 3));
}

TEST(manifest_class) {
  auto r = parse(StringView("name = \"vblk\"\nclass = \"block\"\n[[match]]\nvendor = 0x1af4\n"));
  CHECK(r.is_ok() && r.value().driver_class == lib::manifest::DriverClass::Block);
  CHECK(parse(StringView("name = \"e\"\n[[match]]\nvendor = 1\n")).value().driver_class ==
        lib::manifest::DriverClass::None);
  CHECK(parse(StringView("name = \"n\"\nclass = \"net\"\n[[match]]\nvendor = 1\n")).value().driver_class ==
        lib::manifest::DriverClass::Net);
  CHECK(fails_at("name = \"a\"\nclass = \"usb\"\n", 2));                 // неизвестный класс
  CHECK(fails_at("name = \"a\"\nclass = \"block\"\nclass = \"block\"\n", 3));
  CHECK(fails_at("name = \"a\"\n[[match]]\nclass = \"block\"\n", 3));     // только сверху
}
