// Contract tests for pure CAN-database helpers (Src/CanDataBase).
//
// - CheckCanFdPossibleMessageSize rounds an arbitrary payload byte count up to
//   the next legal CAN-FD DLC (8/12/16/20/24/32/48/64).
// - GetDtypeString splits a "<keyword> <name>" token out of a mutable config
//   buffer in place.
//
// Both are pure (no global state). CheckCanFdPossibleMessageSize has external
// linkage but no header prototype, so it is declared locally.

#include <gtest/gtest.h>

extern "C" {
int CheckCanFdPossibleMessageSize(int par_Size);
int GetDtypeString(char* Dtype, char** pTxt, char** pDtypstr, char** pName);
}

namespace {

TEST(CanFdMessageSizeTest, UpToEight_ReturnedUnchanged)
{
    EXPECT_EQ(0, CheckCanFdPossibleMessageSize(0));
    EXPECT_EQ(1, CheckCanFdPossibleMessageSize(1));
    EXPECT_EQ(8, CheckCanFdPossibleMessageSize(8));
}

TEST(CanFdMessageSizeTest, BetweenDlcSteps_RoundsUpToNextLegalDlc)
{
    EXPECT_EQ(12, CheckCanFdPossibleMessageSize(9));
    EXPECT_EQ(12, CheckCanFdPossibleMessageSize(12));
    EXPECT_EQ(16, CheckCanFdPossibleMessageSize(13));
    EXPECT_EQ(20, CheckCanFdPossibleMessageSize(17));
    EXPECT_EQ(24, CheckCanFdPossibleMessageSize(21));
    EXPECT_EQ(32, CheckCanFdPossibleMessageSize(25));
    EXPECT_EQ(48, CheckCanFdPossibleMessageSize(33));
    EXPECT_EQ(64, CheckCanFdPossibleMessageSize(49));
}

TEST(CanFdMessageSizeTest, AboveMax_SaturatesTo64)
{
    EXPECT_EQ(64, CheckCanFdPossibleMessageSize(64));
    EXPECT_EQ(64, CheckCanFdPossibleMessageSize(100));
}

TEST(GetDtypeStringTest, KeywordMatches_ExtractsKeywordAndName)
{
    char pre_buffer[] = "SINT myname;rest";
    char* pos = pre_buffer;
    char* result_dtypstr = nullptr;
    char* result_name = nullptr;

    const int result_state = GetDtypeString("SINT", &pos, &result_dtypstr, &result_name);

    EXPECT_EQ(1, result_state);
    EXPECT_STREQ("SINT", result_dtypstr);
    EXPECT_STREQ("myname", result_name);
}

TEST(GetDtypeStringTest, KeywordDoesNotMatch_ReturnsZero)
{
    char pre_buffer[] = "SINT x;";
    char* pos = pre_buffer;
    char* result_dtypstr = nullptr;
    char* result_name = nullptr;

    const int result_state = GetDtypeString("UINT", &pos, &result_dtypstr, &result_name);

    EXPECT_EQ(0, result_state);
}

TEST(GetDtypeStringTest, LeadingWhitespace_IsSkippedBeforeMatching)
{
    char pre_buffer[] = "   UINT signal;";
    char* pos = pre_buffer;
    char* result_dtypstr = nullptr;
    char* result_name = nullptr;

    const int result_state = GetDtypeString("UINT", &pos, &result_dtypstr, &result_name);

    EXPECT_EQ(1, result_state);
    EXPECT_STREQ("UINT", result_dtypstr);
    EXPECT_STREQ("signal", result_name);
}

} // namespace
