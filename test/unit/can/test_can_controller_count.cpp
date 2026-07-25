// Contract tests for GetCanControllerCountFromIni (Src/CanDataBase/CanDataBase.c),
// which resolves the configured CAN controller count from an ini section. It
// prefers the "..._ext" entry only when it is >= 4, otherwise the plain entry,
// and clamps the result to MAX_CAN_CHANNELS (16). Driven through the in-memory
// ini facade fixture (no file IO).

#include <gtest/gtest.h>

extern "C" {
#include "IniDataBase.h"
int GetCanControllerCountFromIni(char* par_Section, int par_Fd);
}

namespace {

constexpr int MAX_CAN_CHANNELS{16};

class CanControllerCountTest : public ::testing::Test
{
protected:
    int m_fd{-1};
    char m_section[8]{"CAN"};

    void SetUp() override
    {
        IniFileDataBaseInit();
        m_fd = IniFileDataBaseCreateAndOpenNewIniFile("xilenv_can_unittest.ini");
        ASSERT_GE(m_fd, 0);
    }

    void TearDown() override
    {
        if (0 <= m_fd)
        {
            IniFileDataBaseClose(m_fd);
            m_fd = -1;
        }
        IniFileDataBaseTerminate();
    }
};

TEST_F(CanControllerCountTest, NoEntries_ReturnsZero)
{
    EXPECT_EQ(0, GetCanControllerCountFromIni(m_section, m_fd));
}

TEST_F(CanControllerCountTest, PlainEntryOnly_IsUsed)
{
    IniFileDataBaseWriteInt(m_section, "can_controller_count", 2, m_fd);

    EXPECT_EQ(2, GetCanControllerCountFromIni(m_section, m_fd));
}

TEST_F(CanControllerCountTest, ExtEntryAtLeastFour_OverridesPlain)
{
    IniFileDataBaseWriteInt(m_section, "can_controller_count", 2, m_fd);
    IniFileDataBaseWriteInt(m_section, "can_controller_count_ext", 6, m_fd);

    EXPECT_EQ(6, GetCanControllerCountFromIni(m_section, m_fd));
}

TEST_F(CanControllerCountTest, ExtEntryBelowFour_IsIgnored)
{
    IniFileDataBaseWriteInt(m_section, "can_controller_count", 2, m_fd);
    IniFileDataBaseWriteInt(m_section, "can_controller_count_ext", 3, m_fd);

    EXPECT_EQ(2, GetCanControllerCountFromIni(m_section, m_fd))
        << "the _ext value is only honoured when it is >= 4";
}

TEST_F(CanControllerCountTest, ExcessiveCount_ClampsToMax)
{
    IniFileDataBaseWriteInt(m_section, "can_controller_count_ext", 100, m_fd);

    EXPECT_EQ(MAX_CAN_CHANNELS, GetCanControllerCountFromIni(m_section, m_fd));
}

} // namespace
