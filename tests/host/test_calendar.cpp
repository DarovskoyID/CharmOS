#include <lib/core/calendar.hpp>
#include "test.hpp"

TEST(calendar_epoch_and_timezone) {
  uint64_t out = 99;
  CHECK(lib::core::utc_nanoseconds(1970,1,1,0,0,0,0,0,&out) && out == 0);
  CHECK(lib::core::utc_nanoseconds(1970,1,1,0,0,0,123,480,&out));
  CHECK(out == 28'800'000'000'123ull);
  CHECK(lib::core::utc_nanoseconds(1970,1,1,3,0,0,0,-180,&out) && out == 0);
  CHECK(!lib::core::utc_nanoseconds(1970,1,1,0,0,0,0,-1,&out));
  CHECK(out == 0);
}
TEST(calendar_validation_and_leap) {
  uint64_t out = 7;
  CHECK(lib::core::leap_year(2000));
  CHECK(!lib::core::leap_year(1900));
  CHECK(!lib::core::utc_nanoseconds(2100,2,29,0,0,0,0,0,&out));
  CHECK(!lib::core::utc_nanoseconds(2026,4,31,0,0,0,0,0,&out));
  CHECK(!lib::core::utc_nanoseconds(2026,1,1,24,0,0,0,0,&out));
  CHECK(!lib::core::utc_nanoseconds(2026,1,1,0,0,60,0,0,&out));
  CHECK(!lib::core::utc_nanoseconds(2026,1,1,0,0,0,1000000000,0,&out));
  CHECK(!lib::core::utc_nanoseconds(2554,1,1,0,0,0,0,0,&out));
  CHECK(out == 7);
  CHECK(lib::core::utc_nanoseconds(2000,2,29,0,0,0,0,0,&out));
  CHECK(out == 951782400000000000ull);
  CHECK(lib::core::utc_nanoseconds(2038,1,19,3,14,8,0,0,&out));
  CHECK(out == 2147483648000000000ull);
}
