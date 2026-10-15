// SHA-256 known-answer tests (FIPS 180-4 examples). The credential denylist depends on this.
#include <gtest/gtest.h>

#include "rathena/core/sha256.hpp"

using rathena::core::sha256_hex;

TEST(CORE_Sha256, CORE_Sha256_KnownAnswers) {
	EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	EXPECT_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
	          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(CORE_Sha256, CORE_Sha256_MultiBlockInput) {
	EXPECT_EQ(sha256_hex(std::string(1000, 'a')),
	          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}
