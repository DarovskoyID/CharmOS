// Отвечает на каждое сообщение ping значением +1, пока канал не закроют.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle ch) {
  for (;;) {
    uint64_t v = 0;
    Handle hs[1];
    auto r = channel_read(ch, &v, sizeof v, hs, 1, kForever);
    if (r.status == Status::PeerClosed) return 0;
    EXPECT(r.ok());
    ++v;
    EXPECT(channel_write(ch, &v, sizeof v, nullptr, 0) == Status::Ok);
  }
}
