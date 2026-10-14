// Владелец страницы (ROADMAP M4): даёт B урезанный доступ, отзывает его.
// Стартовый хендл — канал A↔B; первым сообщением ядро кладёт в него канал A↔C.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle ab) {
  char buf[64];
  Handle hs[4];
  uint32_t n, nh;
  EXPECT(read_msg(ab, buf, sizeof buf, hs, 4, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "ac") && nh == 1);
  Handle ac = hs[0];

  auto v = vmo_create(4096);
  EXPECT(v.ok());
  Handle vmo = static_cast<Handle>(v.value);
  auto m = vmo_map(vmo, rights::kRead | rights::kWrite);
  EXPECT(m.ok());
  char* page = reinterpret_cast<char*>(m.value);
  const char secret[] = "secret-42";
  for (uint32_t i = 0; i < sizeof secret; ++i) page[i] = secret[i];

  // B получает только чтение и отображение: ни записи, ни копирования.
  auto d = handle_duplicate(vmo, rights::kRead | rights::kMap | rights::kTransfer);
  EXPECT(d.ok());
  Handle give = static_cast<Handle>(d.value);
  EXPECT(send(ab, "vmo", &give, 1) == Status::Ok);
  EXPECT(handle_close(give) == Status::BadHandle);  // хендл переместился к B
  EXPECT(send(ac, "nothing for you") == Status::Ok);

  EXPECT(read_msg(ab, buf, sizeof buf, hs, 4, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "read-ok"));

  auto r = handle_revoke(vmo);
  EXPECT(r.ok() && r.value == 1);
  EXPECT(page[0] == 's');  // свой хендл и своё отображение отзыв не трогает
  print("cap_a: revoked B's access\n");
  EXPECT(send(ab, "revoked") == Status::Ok);

  // B обратится к снятому отображению и будет убит: его конец канала закроется.
  EXPECT(channel_read(ab, buf, sizeof buf, hs, 4, kForever).status == Status::PeerClosed);
  EXPECT(read_msg(ac, buf, sizeof buf, hs, 4, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "forge-ok"));
  print("cap_a: done\n");
  return 0;
}
