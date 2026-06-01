// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

#include "statusbar/owlm/owlm_error.hpp"

#include "statusbar/test/test.hpp"

#include <string>
#include <system_error>

namespace {

using namespace statusbar;
using namespace statusbar::owlm;

TEST(owlm_error, make_error_code_round_trip)
{
    std::error_code ec = make_error_code(OwlmError::InvalidMagic);
    EXPECT_EQ(ec.value(), static_cast<int>(OwlmError::InvalidMagic));
    EXPECT_EQ(std::string{ec.category().name()}, std::string{"statusbar.owlm"});
}

TEST(owlm_error, message_distinct_per_value)
{
    auto m1 = make_error_code(OwlmError::InvalidMagic).message();
    auto m2 = make_error_code(OwlmError::UnsupportedVersion).message();
    auto m3 = make_error_code(OwlmError::SenderNotSynced).message();
    EXPECT_NE(m1, m2);
    EXPECT_NE(m2, m3);
    EXPECT_NE(m1, m3);
}

TEST(owlm_error, unknown_value_yields_unknown_message)
{
    std::error_code const ec{9999, owlm_error_category()};
    EXPECT_EQ(ec.message(), std::string{"Unknown OWLM error"});
}

}  // namespace

TEST_MAIN(statusbar_owlm, owlm_error_test)
