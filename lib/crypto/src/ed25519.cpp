// Ed25519 по RFC 8032 §5.1. Поле GF(p), p = 2^255 − 19: пять 51-битных
// частей. Точки — расширенные координаты (X:Y:Z:T), x = X/Z, y = Y/Z,
// xy = T/Z (§5.1.4). Сложение — полная формула для a = −1, она же удвоение.
#include <lib/crypto/ed25519.hpp>
#include <lib/crypto/sha512.hpp>

namespace lib::crypto {

namespace {

using u128 = unsigned __int128;
constexpr uint64_t kMask = (uint64_t{1} << 51) - 1;

struct Fe {
  uint64_t v[5];
};

Fe fe(uint64_t x) { return {{x, 0, 0, 0, 0}}; }

Fe carry(u128 t[5]) {
  Fe r;
  for (int i = 0; i < 4; ++i) {
    t[i + 1] += t[i] >> 51;
    t[i] &= kMask;
  }
  u128 top = t[4] >> 51;
  t[4] &= kMask;
  t[0] += top * 19;  // 2^255 ≡ 19
  t[1] += t[0] >> 51;
  t[0] &= kMask;
  for (int i = 0; i < 5; ++i) r.v[i] = static_cast<uint64_t>(t[i]);
  return r;
}

// Результаты add/sub нормализуются: части < 2^52, как после mul.
Fe add(const Fe& a, const Fe& b) {
  u128 t[5];
  for (int i = 0; i < 5; ++i) t[i] = static_cast<u128>(a.v[i]) + b.v[i];
  return carry(t);
}

// a − b: прибавляем 2p (части b < 2^52), чтобы не уйти в минус.
Fe sub(const Fe& a, const Fe& b) {
  u128 t[5];
  t[0] = static_cast<u128>(a.v[0]) + 0xfffffffffffdaull - b.v[0];
  for (int i = 1; i < 5; ++i) t[i] = static_cast<u128>(a.v[i]) + 0xffffffffffffeull - b.v[i];
  return carry(t);
}

Fe mul(const Fe& a, const Fe& b) {
  u128 t[5] = {};
  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 5; ++j) {
      u128 p = static_cast<u128>(a.v[i]) * b.v[j];
      if (i + j >= 5) {
        t[i + j - 5] += p * 19;
      } else {
        t[i + j] += p;
      }
    }
  }
  return carry(t);
}

Fe sq(const Fe& a) { return mul(a, a); }

// Нормализует части и приводит по модулю p полностью.
void reduce(Fe& a) {
  u128 t[5];
  for (int i = 0; i < 5; ++i) t[i] = a.v[i];
  a = carry(t);
  a = carry(t);  // повторно: после первого прохода t[1] мог превысить 51 бит
  // Теперь a < 2^255 + малое; вычесть p, если a ≥ p.
  uint64_t q = (a.v[0] + 19) >> 51;
  for (int i = 1; i < 5; ++i) q = (a.v[i] + q) >> 51;
  a.v[0] += 19 * q;
  for (int i = 0; i < 4; ++i) {
    a.v[i + 1] += a.v[i] >> 51;
    a.v[i] &= kMask;
  }
  a.v[4] &= kMask;
}

void to_bytes(uint8_t out[32], Fe a) {
  reduce(a);
  for (int i = 0; i < 32; ++i) out[i] = 0;
  int bit = 0;
  for (int i = 0; i < 5; ++i) {
    for (int b = 0; b < 51; ++b, ++bit) {
      if (((a.v[i] >> b) & 1) != 0) out[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
    }
  }
}

Fe from_bytes(const uint8_t in[32]) {  // бит 255 игнорируется
  Fe r = {};
  for (int bit = 0; bit < 255; ++bit) {
    if (((in[bit / 8] >> (bit % 8)) & 1) != 0) r.v[bit / 51] |= uint64_t{1} << (bit % 51);
  }
  return r;
}

bool equal(const Fe& a, const Fe& b) {
  uint8_t x[32], y[32];
  to_bytes(x, a);
  to_bytes(y, b);
  for (int i = 0; i < 32; ++i) {
    if (x[i] != y[i]) return false;
  }
  return true;
}

bool is_odd(const Fe& a) {
  uint8_t x[32];
  to_bytes(x, a);
  return (x[0] & 1) != 0;
}

// a^e, e — 32 байта little-endian.
Fe pow(const Fe& a, const uint8_t e[32]) {
  Fe r = fe(1);
  for (int bit = 255; bit >= 0; --bit) {
    r = sq(r);
    if (((e[bit / 8] >> (bit % 8)) & 1) != 0) r = mul(r, a);
  }
  return r;
}

// p − 2, (p − 5)/8, (p − 1)/4 — little-endian.
void exponent(uint8_t out[32], int kind) {
  // p = 2^255 − 19: младший байт 0xed, остальные 0xff, старший 0x7f.
  for (int i = 0; i < 32; ++i) out[i] = 0xff;
  out[31] = 0x7f;
  out[0] = 0xed;
  if (kind == 0) {
    out[0] = 0xeb;  // p − 2
  } else {
    // (p − 5)/8 = (2^255 − 24)/8 = 2^252 − 3; (p − 1)/4 = 2^253 − 5.
    for (int i = 0; i < 32; ++i) out[i] = 0xff;
    if (kind == 1) {
      out[31] = 0x0f;
      out[0] = 0xfd;
    } else {
      out[31] = 0x1f;
      out[0] = 0xfb;
    }
  }
}

Fe inv(const Fe& a) {
  uint8_t e[32];
  exponent(e, 0);
  return pow(a, e);
}

struct Point {
  Fe x, y, z, t;
};

Fe g_d, g_d2, g_sqrtm1;
Point g_base;
bool g_ready = false;

Point padd(const Point& p, const Point& q) {
  Fe a = mul(sub(p.y, p.x), sub(q.y, q.x));
  Fe b = mul(add(p.y, p.x), add(q.y, q.x));
  Fe c = mul(mul(p.t, g_d2), q.t);
  Fe d = mul(add(p.z, p.z), q.z);
  Fe e = sub(b, a), f = sub(d, c), g = add(d, c), h = add(b, a);
  return {mul(e, f), mul(g, h), mul(f, g), mul(e, h)};
}

Point identity() { return {fe(0), fe(1), fe(1), fe(0)}; }

// [s]P, s — 32 байта little-endian.
Point smul(const uint8_t s[32], const Point& p) {
  Point r = identity();
  for (int bit = 255; bit >= 0; --bit) {
    r = padd(r, r);
    if (((s[bit / 8] >> (bit % 8)) & 1) != 0) r = padd(r, p);
  }
  return r;
}

void encode(uint8_t out[32], const Point& p) {
  Fe zi = inv(p.z);
  Fe x = mul(p.x, zi), y = mul(p.y, zi);
  to_bytes(out, y);
  if (is_odd(x)) out[31] |= 0x80;
}

// §5.1.3
bool decode(const uint8_t in[32], Point* out) {
  // y < p: старшие биты 0x7fff…ffed и больше — неканонично.
  uint8_t y_bytes[32];
  for (int i = 0; i < 32; ++i) y_bytes[i] = in[i];
  y_bytes[31] &= 0x7f;
  bool big = y_bytes[31] == 0x7f && y_bytes[0] >= 0xed;
  for (int i = 1; i < 31 && big; ++i) big = y_bytes[i] == 0xff;
  if (big) return false;
  bool sign = (in[31] & 0x80) != 0;
  Fe y = from_bytes(y_bytes);
  Fe y2 = sq(y);
  Fe u = sub(y2, fe(1));
  Fe v = add(mul(g_d, y2), fe(1));
  Fe v3 = mul(sq(v), v);
  Fe v7 = mul(sq(v3), v);
  uint8_t e[32];
  exponent(e, 1);
  Fe x = mul(mul(u, v3), pow(mul(u, v7), e));
  Fe vx2 = mul(v, sq(x));
  if (!equal(vx2, u)) {
    if (!equal(vx2, sub(fe(0), u))) return false;
    x = mul(x, g_sqrtm1);
  }
  uint8_t xb[32];
  to_bytes(xb, x);
  bool zero = true;
  for (int i = 0; i < 32; ++i) zero = zero && xb[i] == 0;
  if (zero && sign) return false;
  if (is_odd(x) != sign) x = sub(fe(0), x);
  *out = {x, y, fe(1), mul(x, y)};
  return true;
}

void init() {
  if (g_ready) return;
  // d = −121665/121666 (§5.1).
  g_d = mul(sub(fe(0), fe(121665)), inv(fe(121666)));
  g_d2 = add(g_d, g_d);
  uint8_t e[32];
  exponent(e, 2);
  g_sqrtm1 = pow(fe(2), e);  // 2^((p−1)/4) — корень из −1
  // B: y = 4/5, x чётный (§5.1): кодировка 0x58 0x66 … 0x66.
  uint8_t b[32];
  b[0] = 0x58;
  for (int i = 1; i < 32; ++i) b[i] = 0x66;
  (void)decode(b, &g_base);
  g_ready = true;
}

// --- арифметика по модулю L = 2^252 + 27742317777372353535851937790883648493 ---

constexpr uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                            0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

// r (33 байта, < 2L) ≥ L ?
bool ge_l(const uint8_t r[33]) {
  if (r[32] != 0) return true;
  for (int i = 31; i >= 0; --i) {
    if (r[i] != kL[i]) return r[i] > kL[i];
  }
  return true;
}

void sub_l(uint8_t r[33]) {
  int borrow = 0;
  for (int i = 0; i < 33; ++i) {
    int v = r[i] - (i < 32 ? kL[i] : 0) - borrow;
    borrow = v < 0 ? 1 : 0;
    r[i] = static_cast<uint8_t>(v + (borrow << 8));
  }
}

// n байт little-endian по модулю L (побитовое деление, правило 9).
void mod_l(const uint8_t* in, int n, uint8_t out[32]) {
  uint8_t r[33] = {};
  for (int bit = n * 8 - 1; bit >= 0; --bit) {
    int c = (in[bit / 8] >> (bit % 8)) & 1;
    for (int i = 0; i < 33; ++i) {
      int nc = r[i] >> 7;
      r[i] = static_cast<uint8_t>((r[i] << 1) | c);
      c = nc;
    }
    if (ge_l(r)) sub_l(r);
  }
  for (int i = 0; i < 32; ++i) out[i] = r[i];
}

// (a·b + c) mod L, все — 32 байта.
void muladd_l(const uint8_t a[32], const uint8_t b[32], const uint8_t c[32], uint8_t out[32]) {
  uint32_t t[65] = {};
  for (int i = 0; i < 32; ++i) {
    for (int j = 0; j < 32; ++j) t[i + j] += uint32_t{a[i]} * b[j];
  }
  for (int i = 0; i < 32; ++i) t[i] += c[i];
  uint8_t prod[65];
  uint32_t carry_v = 0;
  for (int i = 0; i < 65; ++i) {
    uint32_t v = t[i] + carry_v;
    prod[i] = static_cast<uint8_t>(v);
    carry_v = v >> 8;
  }
  mod_l(prod, 65, out);
}

bool less_than_l(const uint8_t s[32]) {
  for (int i = 31; i >= 0; --i) {
    if (s[i] != kL[i]) return s[i] < kL[i];
  }
  return false;
}

void expand(const uint8_t seed[32], uint8_t scalar[32], uint8_t prefix[32]) {
  uint8_t h[64];
  sha512(seed, 32, h);
  for (int i = 0; i < 32; ++i) {
    scalar[i] = h[i];
    prefix[i] = h[32 + i];
  }
  scalar[0] &= 0xf8;  // §5.1.5: обнулить 3 младших бита,
  scalar[31] &= 0x7f;  // старший бит,
  scalar[31] |= 0x40;  // установить второй по старшинству
}

}  // namespace

void ed25519_public_key(const uint8_t seed[32], uint8_t pub[32]) {
  init();
  uint8_t s[32], prefix[32];
  expand(seed, s, prefix);
  encode(pub, smul(s, g_base));
}

void ed25519_sign(const uint8_t seed[32], const void* msg, size_t len, uint8_t sig[64]) {
  init();
  uint8_t s[32], prefix[32], pub[32];
  expand(seed, s, prefix);
  encode(pub, smul(s, g_base));
  uint8_t h[64], r[32], k[32];
  Sha512 hs;
  hs.update(prefix, 32);
  hs.update(msg, len);
  hs.finish(h);
  mod_l(h, 64, r);
  encode(sig, smul(r, g_base));  // R
  hs.update(sig, 32);
  hs.update(pub, 32);
  hs.update(msg, len);
  hs.finish(h);
  mod_l(h, 64, k);
  muladd_l(k, s, r, sig + 32);  // S = (r + k·s) mod L
}

bool ed25519_verify(const uint8_t pub[32], const void* msg, size_t len, const uint8_t sig[64]) {
  init();
  Point a, r;
  if (!less_than_l(sig + 32) || !decode(pub, &a) || !decode(sig, &r)) return false;
  uint8_t h[64], k[32];
  Sha512 hs;
  hs.update(sig, 32);
  hs.update(pub, 32);
  hs.update(msg, len);
  hs.finish(h);
  mod_l(h, 64, k);
  uint8_t lhs[32], rhs[32];
  encode(lhs, smul(sig + 32, g_base));
  encode(rhs, padd(r, smul(k, a)));
  for (int i = 0; i < 32; ++i) {
    if (lhs[i] != rhs[i]) return false;
  }
  return true;
}

}  // namespace lib::crypto
