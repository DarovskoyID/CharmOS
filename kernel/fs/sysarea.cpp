#include "sysarea.hpp"

#include "../core/log.hpp"
#include "../tpm/tpm.hpp"

namespace kernel::fs {

namespace sa = lib::sysarea;

namespace {

constinit sa::Header g_header = {};

void wipe(void* p, size_t n) {
  auto* b = static_cast<volatile uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) b[i] = 0;
}

bool locked(const char* why) {
  kprint("sysarea: locked: {}\n", why);
  return false;
}

bool write_header(lib::fs::Io* part) {
  sa::seal_check(&g_header);
  return part->write(0, &g_header, sizeof g_header).is_ok() && part->flush().is_ok();
}

}  // namespace

bool sysarea_unlock(lib::fs::Io* part, lib::crypto::Xts256* key, sa::XtsIo* out) {
  if (part->size() <= sa::kDataOffset || part->read(0, &g_header, sizeof g_header).is_err()) {
    return locked("cannot read header");
  }
  if (!sa::valid(g_header, part->size())) return locked("bad header");
  if (!tpm::present()) return locked("no TPM");
  auto state = static_cast<sa::State>(g_header.f.state);
  uint64_t units = g_header.f.data_units;
  uint8_t k[sa::kKeySize];
  if (state == sa::State::Plain) {
    // §4.1: новый ключ, запечатать, затем шифровать на месте.
    do {
      if (!tpm::get_random(k, sizeof k)) return locked("TPM GetRandom failed");
    } while (!key->set_key(k));
    tpm::Sealed s;
    bool sealed = tpm::seal(k, sizeof k, g_header.f.pcr_mask, &s) &&
                  size_t{s.pub_size} + s.priv_size <= sa::kSealedMax;
    if (!sealed) {
      wipe(k, sizeof k);
      kprint("sysarea: TPM error {x}\n", tpm::last_error());
      return locked("cannot seal key");
    }
    for (uint16_t i = 0; i < s.pub_size; ++i) g_header.sealed[i] = s.pub[i];
    for (uint16_t i = 0; i < s.priv_size; ++i) g_header.sealed[s.pub_size + i] = s.priv[i];
    g_header.f.public_size = s.pub_size;
    g_header.f.private_size = s.priv_size;
    g_header.f.state = static_cast<uint32_t>(sa::State::Encrypting);
    g_header.f.progress = 0;
    if (!write_header(part)) {
      wipe(k, sizeof k);
      return locked("cannot write header");
    }
    kprint("sysarea: key sealed to TPM (PCR mask {x}), encrypting {} KiB\n", g_header.f.pcr_mask, units / 2);
  } else {
    // §4.2–4.3: распечатать (политику PCR проверяет TPM).
    const uint8_t* pub = g_header.sealed;
    const uint8_t* priv = g_header.sealed + g_header.f.public_size;
    if (!tpm::unseal(pub, g_header.f.public_size, priv, g_header.f.private_size, g_header.f.pcr_mask, k, sizeof k)) {
      kprint("sysarea: TPM error {x}\n", tpm::last_error());
      return locked("cannot unseal key (PCR changed?)");
    }
    if (!key->set_key(k)) {
      wipe(k, sizeof k);
      return locked("bad key");
    }
  }
  wipe(k, sizeof k);
  if (static_cast<sa::State>(g_header.f.state) == sa::State::Encrypting) {
    // Кусками с сохранением прогресса: сбой питания не теряет данные (§4.2).
    constexpr uint64_t kStep = 2048;  // единиц (1 МиБ) между записями заголовка
    while (g_header.f.progress < units) {
      uint64_t to = g_header.f.progress + kStep < units ? g_header.f.progress + kStep : units;
      if (sa::encrypt_units(part, *key, g_header.f.progress, to).is_err() || part->flush().is_err()) {
        return locked("encryption I/O error");
      }
      g_header.f.progress = to;
      if (!write_header(part)) return locked("cannot write header");
    }
    g_header.f.state = static_cast<uint32_t>(sa::State::Encrypted);
    if (!write_header(part)) return locked("cannot write header");
    kprint("sysarea: encrypted\n");
  }
  out->init(part, key, units, units);
  kprint("sysarea: unlocked (AES-256-XTS)\n");
  return true;
}

}  // namespace kernel::fs
