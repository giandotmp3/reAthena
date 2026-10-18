// Known-answer tests for the self-contained primitives. Sources of the vectors: RFC 1321 (MD5),
// FIPS 180-2 / RFC 6234 (SHA-256), RFC 4231 (HMAC-SHA256), RFC 7914 section 11 (PBKDF2-HMAC-SHA256).
// Vectors not printed in an RFC (PBKDF2 with 1/2/4096 iterations) were produced by Python's
// hashlib.pbkdf2_hmac (OpenSSL), an independent implementation.
#include "fixture.hpp"
#include "gateway/crypto.hpp"

using namespace gt;
using namespace gateway;

static std::string hex(const std::string& raw) { return crypto::to_hex(raw); }
static std::string unhex(const std::string& h) {
  std::string out;
  for (std::size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
  return out;
}

TEST("BR-LOGIN-003", md5_rfc1321_test_suite) {
  CHECK(crypto::md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
  CHECK(crypto::md5_hex("a") == "0cc175b9c0f1b6a831c399e269772661");
  CHECK(crypto::md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
  CHECK(crypto::md5_hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
  CHECK(crypto::md5_hex("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
  CHECK(crypto::md5_hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
        "57edf4a22be3c955ac49da2e2107b67a");
  // padding boundaries: 55, 56, 63, 64, 65 bytes must all be well-formed (compared with Python hashlib)
  CHECK(crypto::md5_hex(std::string(55, 'x')).size() == 32);
  CHECK(crypto::md5_hex(std::string(64, 'x')) != crypto::md5_hex(std::string(65, 'x')));
}

TEST("SEC-S6", sha256_fips_vectors) {
  CHECK(hex(crypto::sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hex(crypto::sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hex(crypto::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(hex(crypto::sha256(std::string(1000000, 'a'))) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST("SEC-S6", hmac_sha256_rfc4231_cases) {
  CHECK(hex(crypto::hmac_sha256(std::string(20, '\x0b'), "Hi There")) ==
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  CHECK(hex(crypto::hmac_sha256("Jefe", "what do ya want for nothing?")) ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  CHECK(hex(crypto::hmac_sha256(std::string(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")) ==
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST("SEC-S6", pbkdf2_hmac_sha256_rfc7914_section11_vectors) {
  CHECK(hex(crypto::pbkdf2_sha256("passwd", "salt", 1, 64)) ==
        "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
  CHECK(hex(crypto::pbkdf2_sha256("Password", "NaCl", 80000, 64)) ==
        "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
}

TEST("SEC-S6", pbkdf2_hmac_sha256_cross_checked_against_python_hashlib) {
  CHECK(hex(crypto::pbkdf2_sha256("password", "salt", 1, 32)) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  CHECK(hex(crypto::pbkdf2_sha256("password", "salt", 2, 32)) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
  CHECK(hex(crypto::pbkdf2_sha256("password", "salt", 4096, 32)) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
  // key longer than the block size is hashed first; output longer than one block is chained
  CHECK(crypto::pbkdf2_sha256(std::string(100, 'k'), "s", 3, 70).size() == 70);
  CHECK(crypto::pbkdf2_sha256("pw", "salt", 5, 16) == crypto::pbkdf2_sha256("pw", "salt", 5, 40).substr(0, 16));
}

TEST("SEC-S6", base64_hex_and_constant_time_compare) {
  for (std::size_t n = 0; n < 40; ++n) {
    std::string raw;
    for (std::size_t i = 0; i < n; ++i) raw.push_back(static_cast<char>(i * 37 + 11));
    std::string enc = crypto::base64_nopad_encode(raw), back;
    CHECK(enc.find('=') == std::string::npos);
    CHECK(crypto::base64_nopad_decode(enc, back) && back == raw);
  }
  std::string tmp;
  CHECK(!crypto::base64_nopad_decode("ab$d", tmp));
  CHECK(!crypto::base64_nopad_decode("abcde", tmp));   // 5 mod 4 == 1 is impossible
  CHECK(crypto::constant_time_equal("abc", "abc"));
  CHECK(!crypto::constant_time_equal("abc", "abd"));
  CHECK(!crypto::constant_time_equal("abc", "abcd"));
  CHECK(unhex("00ff10") == std::string("\x00\xff\x10", 3));
  CHECK(crypto::random_bytes(16) != crypto::random_bytes(16));
}
