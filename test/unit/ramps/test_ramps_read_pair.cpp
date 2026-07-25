// Contract tests for ReadPairOfValues (Src/Ramps/RampsSingle.c), the parser for
// a "<a>/<b>" value pair used by the ramp/curve file format. Only the success
// path is exercised (the error paths route through ThrowError); it splits the
// token at '/', atof()s both halves, and returns 0.

#include <gtest/gtest.h>

extern "C" {
int ReadPairOfValues(char* word, double* a, double* b, int* lc);
}

namespace {

TEST(RampsReadPairTest, TwoFractionalValues_ParsesBoth)
{
    char pre_word[] = "1.5/2.5";
    double result_a{0.0};
    double result_b{0.0};
    int pre_lc{1};

    const int result_state = ReadPairOfValues(pre_word, &result_a, &result_b, &pre_lc);

    EXPECT_EQ(0, result_state);
    EXPECT_DOUBLE_EQ(1.5, result_a);
    EXPECT_DOUBLE_EQ(2.5, result_b);
}

TEST(RampsReadPairTest, TwoIntegerValues_ParsesBoth)
{
    char pre_word[] = "10/20";
    double result_a{0.0};
    double result_b{0.0};
    int pre_lc{1};

    const int result_state = ReadPairOfValues(pre_word, &result_a, &result_b, &pre_lc);

    EXPECT_EQ(0, result_state);
    EXPECT_DOUBLE_EQ(10.0, result_a);
    EXPECT_DOUBLE_EQ(20.0, result_b);
}

TEST(RampsReadPairTest, NegativeAndExponent_ParsesBoth)
{
    char pre_word[] = "-3.0/1e2";
    double result_a{0.0};
    double result_b{0.0};
    int pre_lc{1};

    const int result_state = ReadPairOfValues(pre_word, &result_a, &result_b, &pre_lc);

    EXPECT_EQ(0, result_state);
    EXPECT_DOUBLE_EQ(-3.0, result_a);
    EXPECT_DOUBLE_EQ(100.0, result_b);
}

TEST(RampsReadPairTest, SingleValueNoSlash_SecondDefaultsToZero)
{
    char pre_word[] = "3.0";
    double result_a{0.0};
    double result_b{9.9};
    int pre_lc{1};

    const int result_state = ReadPairOfValues(pre_word, &result_a, &result_b, &pre_lc);

    EXPECT_EQ(0, result_state);
    EXPECT_DOUBLE_EQ(3.0, result_a);
    EXPECT_DOUBLE_EQ(0.0, result_b) << "with no '/', the second field is empty -> atof(\"\") == 0";
}

} // namespace
