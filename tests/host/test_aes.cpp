// XTS-AES-256 по векторам IEEE P1619/D16, приложение B (векторы 10 и 11:
// единица данных 512 байт), docs/specs/ieee-p1619-d16.pdf. Через них
// проверяется и AES-256 (FIPS 197): шифр твика и шифр данных.
#include <lib/crypto/aes.hpp>

#include <string>
#include <vector>

#include "test.hpp"

using namespace lib::crypto;

namespace {

std::vector<uint8_t> hex(const std::string& s) {
  std::vector<uint8_t> v;
  auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(static_cast<uint8_t>(nib(s[i]) * 16 + nib(s[i + 1])));
  return v;
}

const char* const kKey1 = "2718281828459045235360287471352662497757247093699959574966967627";
const char* const kKey2 = "3141592653589793238462643383279502884197169399375105820974944592";

// PTX векторов 10 и 11: байты 00..ff, дважды.
std::vector<uint8_t> plaintext() {
  std::vector<uint8_t> p(512);
  for (size_t i = 0; i < p.size(); ++i) p[i] = static_cast<uint8_t>(i);
  return p;
}

const char* const kCtx10 =
    "1c3b3a102f770386e4836c99e370cf9bea00803f5e482357a4ae12d414a3e63b"
    "5d31e276f8fe4a8d66b317f9ac683f44680a86ac35adfc3345befecb4bb188fd"
    "5776926c49a3095eb108fd1098baec70aaa66999a72a82f27d848b21d4a741b0"
    "c5cd4d5fff9dac89aeba122961d03a757123e9870f8acf1000020887891429ca"
    "2a3e7a7d7df7b10355165c8b9a6d0a7de8b062c4500dc4cd120c0f7418dae3d0"
    "b5781c34803fa75421c790dfe1de1834f280d7667b327f6c8cd7557e12ac3a0f"
    "93ec05c52e0493ef31a12d3d9260f79a289d6a379bc70c50841473d1a8cc81ec"
    "583e9645e07b8d9670655ba5bbcfecc6dc3966380ad8fecb17b6ba02469a020a"
    "84e18e8f84252070c13e9f1f289be54fbc481457778f616015e1327a02b140f1"
    "505eb309326d68378f8374595c849d84f4c333ec4423885143cb47bd71c5edae"
    "9be69a2ffeceb1bec9de244fbe15992b11b77c040f12bd8f6a975a44a0f90c29"
    "a9abc3d4d893927284c58754cce294529f8614dcd2aba991925fedc4ae74ffac"
    "6e333b93eb4aff0479da9a410e4450e0dd7ae4c6e2910900575da401fc07059f"
    "645e8b7e9bfdef33943054ff84011493c27b3429eaedb4ed5376441a77ed4385"
    "1ad77f16f541dfd269d50d6a5f14fb0aab1cbb4c1550be97f7ab4066193c4caa"
    "773dad38014bd2092fa755c824bb5e54c4f36ffda9fcea70b9c6e693e148c151";

const char* const kCtx11 =
    "77a31251618a15e6b92d1d66dffe7b50b50bad552305ba0217a610688eff7e11"
    "e1d0225438e093242d6db274fde801d4cae06f2092c728b2478559df58e837c2"
    "469ee4a4fa794e4bbc7f39bc026e3cb72c33b0888f25b4acf56a2a9804f1ce6d"
    "3d6e1dc6ca181d4b546179d55544aa7760c40d06741539c7e3cd9d2f6650b201"
    "3fd0eeb8c2b8e3d8d240ccae2d4c98320a7442e1c8d75a42d6e6cfa4c2eca179"
    "8d158c7aecdf82490f24bb9b38e108bcda12c3faf9a21141c3613b58367f922a"
    "aa26cd22f23d708dae699ad7cb40a8ad0b6e2784973dcb605684c08b8d6998c6"
    "9aac049921871ebb65301a4619ca80ecb485a31d744223ce8ddc2394828d6a80"
    "470c092f5ba413c3378fa6054255c6f9df4495862bbb3287681f931b687c888a"
    "bf844dfc8fc28331e579928cd12bd2390ae123cf03818d14dedde5c0c24c8ab0"
    "18bfca75ca096f2d531f3d1619e785f1ada437cab92e980558b3dce1474afb75"
    "bfedbf8ff54cb2618e0244c9ac0d3c66fb51598cd2db11f9be39791abe447c63"
    "094f7c453b7ff87cb5bb36b7c79efb0872d17058b83b15ab0866ad8a58656c5a"
    "7e20dbdf308b2461d97c0ec0024a2715055249cf3b478ddd4740de654f75ca68"
    "6e0d7345c69ed50cdc2a8b332b1f8824108ac937eb050585608ee734097fc090"
    "54fbff89eeaeea791f4a7ab1f9868294a4f9e27b42af8100cb9d59cef9645803";

void check_vector(uint64_t unit, const char* ctx) {
  Xts256 x;
  CHECK(x.set_key(hex(std::string(kKey1) + kKey2).data()));
  std::vector<uint8_t> p = plaintext(), c(512), back(512);
  CHECK(x.encrypt(unit, p.data(), c.data(), c.size()));
  CHECK(c == hex(ctx));
  CHECK(x.decrypt(unit, c.data(), back.data(), back.size()));
  CHECK(back == p);
}

}  // namespace

TEST(xts_aes256_vector10) { check_vector(0xff, kCtx10); }

TEST(xts_aes256_vector11) { check_vector(0xffff, kCtx11); }

TEST(xts_rejects_bad_input) {
  Xts256 x;
  std::vector<uint8_t> same = hex(std::string(kKey1) + kKey1);
  CHECK(!x.set_key(same.data()));  // SP 800-38E: Key1 ≠ Key2
  CHECK(x.set_key(hex(std::string(kKey1) + kKey2).data()));
  uint8_t buf[24] = {};
  CHECK(!x.encrypt(0, buf, buf, sizeof buf));  // не кратно 16
  // На месте (in == out) — как будет в ядре.
  std::vector<uint8_t> p = plaintext();
  CHECK(x.encrypt(0xff, p.data(), p.data(), p.size()));
  CHECK(p == hex(kCtx10));
}
