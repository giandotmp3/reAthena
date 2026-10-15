// CAP-03 / CAP-25: IP bans, dynamic password-failure bans, audit log.
#include <gtest/gtest.h>

#include "rathena/login/ip_ban.hpp"
#include "rathena/login/login_log.hpp"
#include "test_support.hpp"

using namespace rathena::login;
using namespace rathena::login::test;

TEST(LoginIpBan, BR_LOGIN_022_MaskMatchingWithFutureRelease) {
  const Ipv4 ip = make_ipv4(192, 0, 2, 77);
  for (std::uint8_t oct = 1; oct <= 4; ++oct) {
    IpBanLookup l;
    l.rows = {IpBan{ip, oct, kNow - 10, kNow + 60, "t"}};
    EXPECT_TRUE(is_ip_banned(l, ip, kNow)) << int{oct};
    l.rows[0].rtime = kNow;  // released
    EXPECT_FALSE(is_ip_banned(l, ip, kNow)) << int{oct};
  }
  IpBanLookup l;
  l.rows = {IpBan{make_ipv4(192, 0, 2, 0), 3, 0, kNow + 60, "abc"}};
  EXPECT_TRUE(is_ip_banned(l, make_ipv4(192, 0, 2, 1), kNow));
  EXPECT_FALSE(is_ip_banned(l, make_ipv4(192, 0, 3, 1), kNow));
}

TEST(LoginIpBan, BR_LOGIN_022_FailsClosedOnSqlError) {
  IpBanLookup l;
  l.error = true;
  EXPECT_TRUE(is_ip_banned(l, make_ipv4(198, 51, 100, 1), kNow));
}

TEST(LoginIpBan, BR_LOGIN_023_SevenFailuresInFiveMinutesBanSubnetForFiveMinutes) {
  LoginConfig cfg;
  PasswordFailureCounter c(cfg);
  const Ipv4 ip = make_ipv4(203, 0, 113, 9);
  for (int i = 0; i < 6; ++i) EXPECT_FALSE(c.record_failure(ip, kNow + i * 10)) << i;
  const auto ban = c.record_failure(ip, kNow + 60);
  ASSERT_TRUE(ban);
  EXPECT_EQ(ban->octets, 3);
  EXPECT_EQ(ban->base, make_ipv4(203, 0, 113, 0));
  EXPECT_EQ(ban->rtime, kNow + 60 + 300);
  EXPECT_TRUE(ip_ban_covers(*ban, make_ipv4(203, 0, 113, 250)));
}

TEST(LoginIpBan, BR_LOGIN_023_DefectFix_CountsWithLoginLogDisabled) {
  LoginConfig cfg;
  cfg.log_login = false;  // legacy never fired in this configuration
  PasswordFailureCounter c(cfg);
  const Ipv4 ip = make_ipv4(203, 0, 113, 10);
  std::optional<IpBan> ban;
  for (int i = 0; i < 7; ++i) ban = c.record_failure(ip, kNow + i);
  EXPECT_TRUE(ban.has_value());
}

TEST(LoginIpBan, BR_LOGIN_023_FailuresOutsideWindowDoNotCount) {
  LoginConfig cfg;
  PasswordFailureCounter c(cfg);
  const Ipv4 ip = make_ipv4(203, 0, 113, 11);
  for (int i = 0; i < 6; ++i) c.record_failure(ip, kNow);
  EXPECT_FALSE(c.record_failure(ip, kNow + 301));  // older six expired
  EXPECT_EQ(c.failures_in_window(ip, kNow + 301), 1u);
}

TEST(LoginIpBan, BR_LOGIN_024_ExpiredBansArePurged) {
  std::vector<IpBan> bans = {
      IpBan{make_ipv4(1, 0, 0, 0), 1, 0, kNow - 1, ""},
      IpBan{make_ipv4(2, 0, 0, 0), 1, 0, kNow, ""},
      IpBan{make_ipv4(3, 0, 0, 0), 1, 0, kNow + 1, ""},
  };
  EXPECT_EQ(purge_expired_ip_bans(bans, kNow), 2u);
  ASSERT_EQ(bans.size(), 1u);
  EXPECT_EQ(bans[0].rtime, kNow + 1);
  EXPECT_EQ(kIpBanPurgeInterval, 60);
  EXPECT_EQ(LoginConfig{}.ipban_cleanup_interval_s, 60);
}

TEST(LoginLog, BR_LOGIN_039_AttemptsLoggedWithResultCode) {
  const auto e = make_login_log(kNow, make_ipv4(10, 1, 2, 3), "player01", 1, "Incorrect Password");
  EXPECT_EQ(e.rcode, 1);
  EXPECT_EQ(e.userid, "player01");
  EXPECT_EQ(e.time, kNow);
  const auto b = make_ip_banned_log(kNow, make_ipv4(10, 1, 2, 3));
  EXPECT_EQ(b.rcode, -3);
  EXPECT_EQ(b.userid, "unknown");
}

TEST(LoginLog, BR_LOGIN_039_HandlerWritesEveryAttempt) {
  GTEST_SKIP() << "PENDING BR-LOGIN-039: LoginLogRepository wiring in the client handler "
                  "(every attempt, including -3 for IP-banned) not implemented yet";
}
