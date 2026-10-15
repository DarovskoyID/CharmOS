#include <lib/crypto/aes.hpp>

namespace lib::crypto {

namespace {

// Умножение в GF(2^8) по модулю x^8 + x^4 + x^3 + x + 1 (§4.2).
constexpr uint8_t xtime(uint8_t b) { return static_cast<uint8_t>((b << 1) ^ ((b & 0x80) != 0 ? 0x1b : 0)); }

constexpr uint8_t gmul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  while (b != 0) {
    if ((b & 1) != 0) r ^= a;
    a = xtime(a);
    b = static_cast<uint8_t>(b >> 1);
  }
  return r;
}

// S-box по §5.1.1: обратный элемент в GF(2^8) (0 → 0), затем аффинное
// преобразование b'_i = b_i ^ b_{i+4} ^ b_{i+5} ^ b_{i+6} ^ b_{i+7} ^ c_i, c = 0x63.
struct Tables {
  uint8_t sbox[256];
  uint8_t inv[256];
};

constexpr Tables make_tables() {
  Tables t{};
  // Обратный элемент — через степени генератора {03}: x = 3^k ⇒ x^-1 = 3^(255-k).
  uint8_t exp[256] = {}, log[256] = {};
  uint8_t g = 1;
  for (int k = 0; k < 255; ++k) {
    exp[k] = g;
    log[g] = static_cast<uint8_t>(k);
    g = gmul(g, 3);
  }
  for (int x = 0; x < 256; ++x) {
    uint8_t inv = x == 0 ? 0 : exp[(255 - log[x]) % 255];
    uint8_t s = 0x63;
    for (int i = 0; i < 8; ++i) {
      int bit = ((inv >> i) ^ (inv >> ((i + 4) % 8)) ^ (inv >> ((i + 5) % 8)) ^ (inv >> ((i + 6) % 8)) ^
                 (inv >> ((i + 7) % 8))) &
                1;
      s = static_cast<uint8_t>(s ^ (bit << i));
    }
    t.sbox[x] = s;
  }
  for (int x = 0; x < 256; ++x) t.inv[t.sbox[x]] = static_cast<uint8_t>(x);
  return t;
}

constexpr Tables kT = make_tables();
static_assert(kT.sbox[0x00] == 0x63 && kT.sbox[0x53] == 0xed);  // §5.1.1: пример {53} → {ed}

// Раунд одним поиском на байт: столбец MixColumns(ShiftRows(SubBytes(s))) —
// сумма вкладов байтов, вклад байта b в строке r — столбец матрицы §5.1.3,
// умноженный на S(b), со сдвигом на r. Te[x] = {02}S(x), S(x), S(x), {03}S(x)
// (байты 0..3 слова, little-endian); Td — то же для InvMixColumns и InvS
// (§5.3.3: {0e}, {09}, {0d}, {0b}). Это та же формула, только посчитанная заранее.
struct RoundTables {
  uint32_t te[256];
  uint32_t td[256];
};

constexpr uint32_t word(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
  return uint32_t{b0} | uint32_t{b1} << 8 | uint32_t{b2} << 16 | uint32_t{b3} << 24;
}

constexpr RoundTables make_round_tables() {
  RoundTables r{};
  for (int x = 0; x < 256; ++x) {
    uint8_t s = kT.sbox[x], i = kT.inv[x];
    r.te[x] = word(gmul(s, 2), s, s, gmul(s, 3));
    r.td[x] = word(gmul(i, 14), gmul(i, 9), gmul(i, 13), gmul(i, 11));
  }
  return r;
}

constexpr RoundTables kR = make_round_tables();

constexpr uint32_t rotl8(uint32_t v, int n) { return n == 0 ? v : (v << (8 * n)) | (v >> (32 - 8 * n)); }

uint32_t load(const uint8_t* p) { return word(p[0], p[1], p[2], p[3]); }
void store(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
uint8_t byte(uint32_t v, int i) { return static_cast<uint8_t>(v >> (8 * i)); }





// §5.3.3: a^-1(x) = {0b}x^3 + {0d}x^2 + {09}x + {0e}.
void inv_mix_columns(uint8_t s[16]) {
  for (int c = 0; c < 4; ++c) {
    uint8_t* a = s + 4 * c;
    uint8_t a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
    a[0] = static_cast<uint8_t>(gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9));
    a[1] = static_cast<uint8_t>(gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13));
    a[2] = static_cast<uint8_t>(gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11));
    a[3] = static_cast<uint8_t>(gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14));
  }
}

void secure_zero(void* p, size_t n) {
  auto* b = static_cast<volatile uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) b[i] = 0;
}

}  // namespace

// §5.2 KEYEXPANSION, Nk = 8: слова w[i] по 4 байта, 4·(Nr+1) = 60 слов.
void Aes256::set_key(const uint8_t key[kAes256Key]) {
  for (int i = 0; i < 32; ++i) w_[i] = key[i];
  uint8_t rcon = 1;
  for (int i = 8; i < 4 * (kRounds + 1); ++i) {
    uint8_t t[4] = {w_[4 * (i - 1)], w_[4 * (i - 1) + 1], w_[4 * (i - 1) + 2], w_[4 * (i - 1) + 3]};
    if (i % 8 == 0) {
      uint8_t t0 = t[0];  // ROTWORD, затем SUBWORD и Rcon
      t[0] = static_cast<uint8_t>(kT.sbox[t[1]] ^ rcon);
      t[1] = kT.sbox[t[2]];
      t[2] = kT.sbox[t[3]];
      t[3] = kT.sbox[t0];
      rcon = xtime(rcon);
    } else if (i % 8 == 4) {
      for (uint8_t& b : t) b = kT.sbox[b];
    }
    for (int j = 0; j < 4; ++j) w_[4 * i + j] = static_cast<uint8_t>(w_[4 * (i - 8) + j] ^ t[j]);
  }
  // §5.3.5: dw — w, у раундов 1..Nr-1 через InvMixColumns.
  for (int i = 0; i < 16 * (kRounds + 1); ++i) dw_[i] = w_[i];
  for (int round = 1; round < kRounds; ++round) inv_mix_columns(dw_ + 16 * round);
}

// §5.1 CIPHER.
// Состояние — четыре столбца-слова (байт строки r — бит 8r).
void Aes256::encrypt(const uint8_t in[kAesBlock], uint8_t out[kAesBlock]) const {
  uint32_t c[4], t[4];
  for (int j = 0; j < 4; ++j) c[j] = load(in + 4 * j) ^ load(w_ + 4 * j);
  for (int round = 1; round < kRounds; ++round) {
    // ShiftRows: строка r нового столбца j берётся из столбца j + r (§5.1.2).
    for (int j = 0; j < 4; ++j) {
      t[j] = kR.te[byte(c[j], 0)] ^ rotl8(kR.te[byte(c[(j + 1) % 4], 1)], 1) ^
             rotl8(kR.te[byte(c[(j + 2) % 4], 2)], 2) ^ rotl8(kR.te[byte(c[(j + 3) % 4], 3)], 3) ^
             load(w_ + 16 * round + 4 * j);
    }
    for (int j = 0; j < 4; ++j) c[j] = t[j];
  }
  for (int j = 0; j < 4; ++j) {  // последний раунд — без MixColumns
    t[j] = word(kT.sbox[byte(c[j], 0)], kT.sbox[byte(c[(j + 1) % 4], 1)], kT.sbox[byte(c[(j + 2) % 4], 2)],
                kT.sbox[byte(c[(j + 3) % 4], 3)]) ^
           load(w_ + 16 * kRounds + 4 * j);
  }
  for (int j = 0; j < 4; ++j) store(out + 4 * j, t[j]);
}

// §5.3 INVCIPHER.
// §5.3.5 EQINVCIPHER: InvSubBytes, InvShiftRows, InvMixColumns и ключи раундов
// 1..Nr-1, пропущенные через InvMixColumns (dw_).
void Aes256::decrypt(const uint8_t in[kAesBlock], uint8_t out[kAesBlock]) const {
  uint32_t c[4], t[4];
  for (int j = 0; j < 4; ++j) c[j] = load(in + 4 * j) ^ load(dw_ + 16 * kRounds + 4 * j);
  for (int round = kRounds - 1; round >= 1; --round) {
    // InvShiftRows: строка r нового столбца j — из столбца j - r (§5.3.1).
    for (int j = 0; j < 4; ++j) {
      t[j] = kR.td[byte(c[j], 0)] ^ rotl8(kR.td[byte(c[(j + 3) % 4], 1)], 1) ^
             rotl8(kR.td[byte(c[(j + 2) % 4], 2)], 2) ^ rotl8(kR.td[byte(c[(j + 1) % 4], 3)], 3) ^
             load(dw_ + 16 * round + 4 * j);
    }
    for (int j = 0; j < 4; ++j) c[j] = t[j];
  }
  for (int j = 0; j < 4; ++j) {
    t[j] = word(kT.inv[byte(c[j], 0)], kT.inv[byte(c[(j + 3) % 4], 1)], kT.inv[byte(c[(j + 2) % 4], 2)],
                kT.inv[byte(c[(j + 1) % 4], 3)]) ^
           load(dw_ + 4 * j);
  }
  for (int j = 0; j < 4; ++j) store(out + 4 * j, t[j]);
}

void Aes256::wipe() {
  secure_zero(w_, sizeof w_);
  secure_zero(dw_, sizeof dw_);
}

bool Xts256::set_key(const uint8_t key[2 * kAes256Key]) {
  bool same = true;
  for (size_t i = 0; i < kAes256Key; ++i) same = same && key[i] == key[kAes256Key + i];
  if (same) return false;
  data_.set_key(key);
  tweak_.set_key(key + kAes256Key);
  return true;
}

// P1619 §5.3.1: T = AES-enc(Key2, i) ⊗ α^j; C = AES-enc(Key1, P ⊕ T) ⊕ T.
// Умножение на α — сдвиг 128-битного little-endian числа влево с
// приведением по x^128 + x^7 + x^2 + x + 1 (§5.2).
bool Xts256::run(uint64_t unit, const uint8_t* in, uint8_t* out, size_t len, bool enc) const {
  if (len == 0 || len % kAesBlock != 0) return false;
  uint8_t t[16] = {};
  for (int i = 0; i < 8; ++i) t[i] = static_cast<uint8_t>(unit >> (8 * i));
  tweak_.encrypt(t, t);
  uint8_t b[16];
  for (size_t off = 0; off < len; off += kAesBlock) {
    for (int i = 0; i < 16; ++i) b[i] = static_cast<uint8_t>(in[off + static_cast<size_t>(i)] ^ t[i]);
    if (enc) {
      data_.encrypt(b, b);
    } else {
      data_.decrypt(b, b);
    }
    for (int i = 0; i < 16; ++i) out[off + static_cast<size_t>(i)] = static_cast<uint8_t>(b[i] ^ t[i]);
    uint8_t carry = 0;
    for (int i = 0; i < 16; ++i) {
      uint8_t next = static_cast<uint8_t>(t[i] >> 7);
      t[i] = static_cast<uint8_t>((t[i] << 1) | carry);
      carry = next;
    }
    if (carry != 0) t[0] ^= 0x87;
  }
  secure_zero(t, sizeof t);
  secure_zero(b, sizeof b);
  return true;
}

bool Xts256::encrypt(uint64_t unit, const uint8_t* in, uint8_t* out, size_t len) const {
  return run(unit, in, out, len, true);
}

bool Xts256::decrypt(uint64_t unit, const uint8_t* in, uint8_t* out, size_t len) const {
  return run(unit, in, out, len, false);
}

void Xts256::wipe() {
  data_.wipe();
  tweak_.wipe();
}

}  // namespace lib::crypto
