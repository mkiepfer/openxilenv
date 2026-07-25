// Contract tests for small, pure global helper functions used across the core:
// exact double comparison, overflow-safe 64-bit muldiv, the quiet-NaN factory,
// the case-sensitive wildcard matcher, and the address-range section filter.
// All are self-contained (no global state / IO), so their contracts are pinned
// directly.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

extern "C" {
#include "Compare2DoubleEqual.h"
#include "GetNotANumber.h"
#include "SectionFilter.h"
#include "Wildcards.h"
#include "my_udiv128.h"
}

namespace {

// CompareDoubleEqual_int: exact bit-agnostic equality via subtraction.

TEST(GlobalHelpersTest, CompareDoubleEqual_EqualValues_ReturnsOne)
{
    EXPECT_EQ(1, CompareDoubleEqual_int(1.0, 1.0));
    EXPECT_EQ(1, CompareDoubleEqual_int(0.0, -0.0));
}

TEST(GlobalHelpersTest, CompareDoubleEqual_DifferentValues_ReturnsZero)
{
    EXPECT_EQ(0, CompareDoubleEqual_int(1.0, 2.0));
}

TEST(GlobalHelpersTest, CompareDoubleEqual_NaN_IsNeverEqual)
{
    // (NaN - NaN) is NaN, which is neither <= 0 nor >= 0 -> not equal.
    EXPECT_EQ(0, CompareDoubleEqual_int(GetNotANumber(), GetNotANumber()));
}

// GetNotANumber: quiet NaN factory.

TEST(GlobalHelpersTest, GetNotANumber_ReturnsQuietNaN)
{
    const double result_nan = GetNotANumber();

    EXPECT_TRUE(std::isnan(result_nan));
    EXPECT_FALSE(result_nan == result_nan);
}

// my_umuldiv64: (a*b)/c through a 128-bit intermediate, so a*b may exceed 64
// bits without wrapping.

TEST(GlobalHelpersTest, MulDiv64_PlainProduct_DividesExactly)
{
    EXPECT_EQ(uint64_t{50}, my_umuldiv64(10u, 20u, 4u));
    // 1000000 * 1000 == 1e9; divided by 500 == 2e6.
    EXPECT_EQ(uint64_t{2000000ull}, my_umuldiv64(1000000u, 1000u, 500u));
}

TEST(GlobalHelpersTest, MulDiv64_ProductOverflows64Bit_NoWraparound)
{
    // UINT64_MAX * 2 == 2^65 - 2; divided by 2 == 2^64 - 1 == UINT64_MAX.
    EXPECT_EQ(UINT64_MAX, my_umuldiv64(UINT64_MAX, 2u, 2u));
}

TEST(GlobalHelpersTest, MulDiv64_DivideByZero_SaturatesOrZero)
{
    EXPECT_EQ(UINT64_MAX, my_umuldiv64(5u, 5u, 0u));
    EXPECT_EQ(uint64_t{0}, my_umuldiv64(0u, 7u, 0u)) << "0/0 is defined as 0";
}

// Compare2StringsWithWildcardsAlwaysCaseSensitive: glob match with '*' and '?'.
// NOTE the inverted convention: 0 == match, non-zero == no match.

TEST(GlobalHelpersTest, Wildcard_StarAndQuestion_Match_ReturnZero)
{
    EXPECT_EQ(0, Compare2StringsWithWildcardsAlwaysCaseSensitive("hello", "h*o"));
    EXPECT_EQ(0, Compare2StringsWithWildcardsAlwaysCaseSensitive("hello", "h?llo"));
    EXPECT_EQ(0, Compare2StringsWithWildcardsAlwaysCaseSensitive("abc", "abc"));
}

TEST(GlobalHelpersTest, Wildcard_NoMatch_ReturnsNonZero)
{
    EXPECT_NE(0, Compare2StringsWithWildcardsAlwaysCaseSensitive("hello", "h*x"));
}

TEST(GlobalHelpersTest, Wildcard_IsCaseSensitive)
{
    EXPECT_NE(0, Compare2StringsWithWildcardsAlwaysCaseSensitive("ABC", "abc"));
}

// CheckAddressRangeFilter: 1 if the address falls in any [Start, End) range, or
// if the filter is absent/empty; 0 otherwise. End is exclusive.

TEST(GlobalHelpersTest, AddressRangeFilter_NullFilter_Passes)
{
    EXPECT_EQ(1, CheckAddressRangeFilter(0x1234u, nullptr));
}

TEST(GlobalHelpersTest, AddressRangeFilter_EmptyFilter_Passes)
{
    SECTION_ADDR_RANGES_FILTER pre_filter{};
    pre_filter.NumOfAddrRanges = 0;

    EXPECT_EQ(1, CheckAddressRangeFilter(0x1234u, &pre_filter));
}

TEST(GlobalHelpersTest, AddressRangeFilter_InsideRange_Passes_EndExclusive)
{
    SECTION_ADDR_RANGES_FILTER pre_filter{};
    pre_filter.NumOfAddrRanges = 1;
    pre_filter.AddrRages[0].Start = 0x1000u;
    pre_filter.AddrRages[0].End = 0x2000u;

    EXPECT_EQ(1, CheckAddressRangeFilter(0x1000u, &pre_filter)) << "Start is inclusive";
    EXPECT_EQ(1, CheckAddressRangeFilter(0x1500u, &pre_filter));
    EXPECT_EQ(0, CheckAddressRangeFilter(0x2000u, &pre_filter)) << "End is exclusive";
    EXPECT_EQ(0, CheckAddressRangeFilter(0x0999u, &pre_filter));
}

} // namespace
