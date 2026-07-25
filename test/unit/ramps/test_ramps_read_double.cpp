// Contract tests for ReadDoubleValueFromFile (Src/Ramps/RampsSingle.c), a
// hand-rolled floating-point scanner reading a [+-]?digits[.digits]?[eE...]?
// token from a stream. It has no global state, so it is driven through a
// portable tmpfile() populated in memory. On success it returns 0, atof()s the
// token into *v, and ungetc()s the terminating character.

#include <gtest/gtest.h>

#include <cstdio>

extern "C" {
int ReadDoubleValueFromFile(FILE* fh, double* v);
}

namespace {

// Writes `content` into a fresh temp stream positioned at the start.
FILE* MakeStream(const char* content)
{
    FILE* fh = tmpfile();
    if (fh != nullptr)
    {
        std::fputs(content, fh);
        std::rewind(fh);
    }
    return fh;
}

TEST(RampsReadDoubleTest, PlainInteger_Parses)
{
    FILE* fh = MakeStream("42;");
    ASSERT_NE(nullptr, fh);

    double result_value{0.0};
    const int result_state = ReadDoubleValueFromFile(fh, &result_value);

    EXPECT_EQ(0, result_state);
    EXPECT_DOUBLE_EQ(42.0, result_value);
    EXPECT_EQ(';', std::fgetc(fh)) << "the terminator is pushed back for the caller";

    std::fclose(fh);
}

TEST(RampsReadDoubleTest, SignedFractionWithExponent_Parses)
{
    FILE* fh = MakeStream("-3.14e2 ");
    ASSERT_NE(nullptr, fh);

    double result_value{0.0};
    const int result_state = ReadDoubleValueFromFile(fh, &result_value);

    EXPECT_EQ(0, result_state);
    EXPECT_DOUBLE_EQ(-314.0, result_value);

    std::fclose(fh);
}

TEST(RampsReadDoubleTest, NoLeadingDigit_ReturnsError)
{
    FILE* fh = MakeStream("abc");
    ASSERT_NE(nullptr, fh);

    double result_value{0.0};
    const int result_state = ReadDoubleValueFromFile(fh, &result_value);

    EXPECT_EQ(-1, result_state) << "a token without an integer part is rejected";

    std::fclose(fh);
}

TEST(RampsReadDoubleTest, TrailingDotWithoutFraction_ReturnsError)
{
    FILE* fh = MakeStream("5.x");
    ASSERT_NE(nullptr, fh);

    double result_value{0.0};
    const int result_state = ReadDoubleValueFromFile(fh, &result_value);

    EXPECT_EQ(-1, result_state) << "a '.' must be followed by at least one fraction digit";

    std::fclose(fh);
}

} // namespace
