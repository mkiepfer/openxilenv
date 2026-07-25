// Contract tests for the bounded formatted-print helper in
// Src/Utilities/PrintFormatToString.c. It wraps (v)snprintf so the core can
// format into a fixed-size buffer without overflow. The non-obvious part is the
// return value: on truncation it reports the number of bytes actually written
// (par_SizeOfDestBuffer - 1), NOT snprintf's "would-be" length.

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "PrintFormatToString.h"
}

namespace {

TEST(PrintFormatToStringTest, PlainFormat_FitsBuffer_WritesStringAndReturnsLength)
{
    char result_buffer[32];

    const int result_len = PrintFormatToString(result_buffer, sizeof(result_buffer), "value=%d", 42);

    EXPECT_STREQ("value=42", result_buffer);
    EXPECT_EQ(8, result_len) << "returns the number of bytes written, excluding the terminator";
}

TEST(PrintFormatToStringTest, MixedFormat_FitsBuffer_FormatsAllArguments)
{
    char result_buffer[64];

    PrintFormatToString(result_buffer, sizeof(result_buffer), "%s=%.2f", "gain", 1.5);

    EXPECT_STREQ("gain=1.50", result_buffer);
}

TEST(PrintFormatToStringTest, OutputTooLong_TruncatesAndNullTerminates)
{
    // "abcdefgh" needs 8+1 bytes; a 5-byte buffer holds "abcd" + '\0'.
    char result_buffer[5];

    const int result_len = PrintFormatToString(result_buffer, sizeof(result_buffer), "%s", "abcdefgh");

    EXPECT_STREQ("abcd", result_buffer);
    EXPECT_EQ('\0', result_buffer[4]);
    EXPECT_EQ(4, result_len) << "on truncation the return is capped to par_SizeOfDestBuffer - 1";
}

TEST(PrintFormatToStringTest, BufferSizeOne_WritesOnlyTerminator_ReturnsZero)
{
    char result_buffer[1] = {'X'};

    const int result_len = PrintFormatToString(result_buffer, 1, "%s", "abc");

    EXPECT_STREQ("", result_buffer);
    EXPECT_EQ(0, result_len);
}

TEST(PrintFormatToStringTest, BufferSizeZero_WritesNothing_ReturnsZero)
{
    char result_buffer[8] = "keep";

    const int result_len = PrintFormatToString(result_buffer, 0, "%s", "abc");

    EXPECT_STREQ("keep", result_buffer) << "size 0 must not write into the buffer";
    EXPECT_EQ(0, result_len);
}

TEST(PrintFormatToStringTest, NullFormat_ReturnsZero)
{
    char result_buffer[16] = "keep";

    const int result_len = PrintFormatToString(result_buffer, sizeof(result_buffer), nullptr);

    EXPECT_EQ(0, result_len);
}

} // namespace
