// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/udptun/udptun_reflect.hpp"

#include "statusbar/test/test.hpp"

#include <cstdint>
#include <sstream>

using namespace statusbar;

TEST(udptun_reflect_stats, default_zero)
{
    udptun::ReflectStats s{};
    EXPECT_EQ(s.reflected, uint64_t{0});
    EXPECT_EQ(s.dropped_invalid, uint64_t{0});
    EXPECT_EQ(s.rx_truncated, uint64_t{0});
    EXPECT_EQ(s.tx_failed, uint64_t{0});
}

TEST(udptun_reflect_summary, prints_counts)
{
    udptun::ReflectStats s{.reflected = 12, .dropped_invalid = 3, .rx_truncated = 1, .tx_failed = 2};
    std::ostringstream oss;
    udptun::print_reflect_summary(oss, s);
    auto const out = oss.str();
    EXPECT_TRUE(out.find("reflected=12") != std::string::npos);
    EXPECT_TRUE(out.find("dropped_invalid=3") != std::string::npos);
    EXPECT_TRUE(out.find("rx_truncated=1") != std::string::npos);
    EXPECT_TRUE(out.find("tx_failed=2") != std::string::npos);
}

TEST_MAIN(statusbar_udptun, udptun_reflect_test)
