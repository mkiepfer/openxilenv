// Contract tests for the bounded string helpers in Src/Utilities/StringMaxChar.c.
// These functions are pure (no global state) and are used all over the core to
// copy/append into fixed-size buffers without overflowing, so their truncation
// and null-termination contract is pinned here before any refactor touches them.

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "StringMaxChar.h"
}

namespace {

// StringCopyMaxCharTruncate: copy par_Src into par_Dst, never writing more than
// par_MaxChar bytes (including the terminating '\0').

TEST(StringMaxCharTest, CopyMaxChar_SourceFitsExactly_CopiesFullStringWithTerminator)
{
    // "abc" needs 4 bytes (3 chars + '\0'); a buffer of exactly 4 must fit it.
    char result_buffer[8] = "XXXXXXX";

    StringCopyMaxCharTruncate(result_buffer, "abc", 4);

    EXPECT_STREQ("abc", result_buffer);
}

TEST(StringMaxCharTest, CopyMaxChar_SourceTooLong_TruncatesAndNullTerminates)
{
    // par_MaxChar == 4 -> at most 3 chars written, then '\0' at index 3.
    char result_buffer[8] = "XXXXXXX";

    StringCopyMaxCharTruncate(result_buffer, "abcdef", 4);

    EXPECT_STREQ("abc", result_buffer);
    EXPECT_EQ('\0', result_buffer[3]);
}

TEST(StringMaxCharTest, CopyMaxChar_MaxCharOne_WritesOnlyTerminator)
{
    char result_buffer[8] = "XXXXXXX";

    StringCopyMaxCharTruncate(result_buffer, "abc", 1);

    EXPECT_STREQ("", result_buffer);
}

TEST(StringMaxCharTest, CopyMaxChar_MaxCharZero_LeavesDestinationUntouched)
{
    // par_MaxChar <= 0 must not write anything (no room even for a terminator).
    char result_buffer[8] = "keep";

    StringCopyMaxCharTruncate(result_buffer, "abc", 0);

    EXPECT_STREQ("keep", result_buffer);
}

TEST(StringMaxCharTest, CopyMaxChar_NullSource_LeavesDestinationUntouched)
{
    char result_buffer[8] = "keep";

    StringCopyMaxCharTruncate(result_buffer, nullptr, 8);

    EXPECT_STREQ("keep", result_buffer);
}

TEST(StringMaxCharTest, CopyMaxChar_NullDestination_DoesNotCrash)
{
    // Pure robustness contract: a null destination must be a safe no-op.
    StringCopyMaxCharTruncate(nullptr, "abc", 8);
    SUCCEED();
}

// StringAppendMaxCharTruncate: append par_Src onto the existing contents of
// par_Dst, keeping the total (including '\0') within par_MaxChar.

TEST(StringMaxCharTest, AppendMaxChar_ResultFits_ConcatenatesFully)
{
    char result_buffer[16] = "foo";

    StringAppendMaxCharTruncate(result_buffer, "bar", 16);

    EXPECT_STREQ("foobar", result_buffer);
}

TEST(StringMaxCharTest, AppendMaxChar_ResultTooLong_TruncatesAppendedPart)
{
    // "foo" already uses 3 chars; par_MaxChar == 5 leaves room for 1 more char
    // plus the terminator -> "foob".
    char result_buffer[16] = "foo";

    StringAppendMaxCharTruncate(result_buffer, "bar", 5);

    EXPECT_STREQ("foob", result_buffer);
    EXPECT_EQ('\0', result_buffer[4]);
}

TEST(StringMaxCharTest, AppendMaxChar_DestinationAlreadyFull_LeavesDestinationUntouched)
{
    // Len1 + 1 (== 4) is not < par_MaxChar (== 4): no room to append, dst kept.
    char result_buffer[16] = "foo";

    StringAppendMaxCharTruncate(result_buffer, "bar", 4);

    EXPECT_STREQ("foo", result_buffer);
}

TEST(StringMaxCharTest, AppendMaxChar_NullSource_LeavesDestinationUntouched)
{
    char result_buffer[16] = "foo";

    StringAppendMaxCharTruncate(result_buffer, nullptr, 16);

    EXPECT_STREQ("foo", result_buffer);
}

// StringMalloc / StringFree: duplicate a C string onto the core heap.

TEST(StringMaxCharTest, StringMalloc_ValidSource_ReturnsIndependentCopy)
{
    const char* pre_source = "duplicate me";

    char* result_copy = StringMalloc(pre_source);

    ASSERT_NE(nullptr, result_copy);
    EXPECT_STREQ(pre_source, result_copy);
    EXPECT_NE(pre_source, result_copy) << "must be a distinct allocation, not the same pointer";

    StringFree(result_copy);
}

TEST(StringMaxCharTest, StringMalloc_NullSource_ReturnsNull)
{
    EXPECT_EQ(nullptr, StringMalloc(nullptr));
}

} // namespace
