// Contract test for CANDB_GetAllBusMembers (Src/CanDataBase/ImportDbc.c), which
// scans a DBC file for its "BU_:" (bus members) line and returns the members as
// a packed, double-NUL-terminated list. Only the happy path is exercised (the
// error paths route through ThrowError). Driven through a small temp .dbc file.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

extern "C" {
int CANDB_GetAllBusMembers(const char* FileName, char** MemberNames);
void CANDB_FreeMembersList(char* MemberNames);
}

namespace {

// Writes DBC content to a unique temp file and returns its path.
std::string WriteTempDbc(const char* content)
{
    const std::string path = "./xilenv_busmembers_unittest.dbc";
    FILE* fh = std::fopen(path.c_str(), "wt");
    if (fh != nullptr)
    {
        std::fputs(content, fh);
        std::fclose(fh);
    }
    return path;
}

TEST(DbcBusMembersTest, BuSection_ReturnsMemberCountAndPackedNames)
{
    // The trailing newline makes the reader stop at end-of-line after ECU2.
    const std::string pre_path = WriteTempDbc("VERSION \"\"\nBU_: ECU1 ECU2\n");

    char* result_members = nullptr;
    const int result_count = CANDB_GetAllBusMembers(pre_path.c_str(), &result_members);

    EXPECT_EQ(2, result_count);
    ASSERT_NE(nullptr, result_members);
    // Packed as consecutive NUL-terminated strings.
    EXPECT_STREQ("ECU1", result_members);
    const char* second = result_members + std::string("ECU1").size() + 1;
    EXPECT_STREQ("ECU2", second);

    CANDB_FreeMembersList(result_members);
    std::remove(pre_path.c_str());
}

} // namespace
