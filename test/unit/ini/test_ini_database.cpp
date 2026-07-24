// Characterization / contract tests for the IniDataBase facade
// (Src/IniFileDataBase/IniDataBase.h). These pin the current read/write
// behaviour so the planned TOML-backed reimplementation (migration step M1)
// can be proven to behave identically.

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "IniDataBase.h"
}

namespace {

// Fixture: fresh in-memory INI database per test.
class IniDataBaseTest : public ::testing::Test
{
protected:
    int m_fd{-1};

    void SetUp() override
    {
        IniFileDataBaseInit();
        m_fd = IniFileDataBaseCreateAndOpenNewIniFile("xilenv_unittest.ini");
        ASSERT_GE(m_fd, 0) << "could not create in-memory ini file";
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

TEST_F(IniDataBaseTest, WriteReadString_RoundTrip_ReturnsSameText)
{
    const char* pre_section = "Section1";
    const char* pre_entry = "MyString";
    const char* pre_value = "Hello XilEnv";

    IniFileDataBaseWriteString(pre_section, pre_entry, pre_value, m_fd);

    char result_buffer[256]{};
    IniFileDataBaseReadString(pre_section, pre_entry, "DEFAULT", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), m_fd);

    EXPECT_STREQ(pre_value, result_buffer);
}

TEST_F(IniDataBaseTest, ReadString_MissingEntry_ReturnsDefault)
{
    char result_buffer[256]{};
    IniFileDataBaseReadString("NoSuchSection", "NoSuchEntry", "the-default",
                              result_buffer, static_cast<int>(sizeof(result_buffer)), m_fd);

    EXPECT_STREQ("the-default", result_buffer);
}

TEST_F(IniDataBaseTest, WriteReadInt_RoundTrip_ReturnsSameValue)
{
    const int pre_value = 424242;
    IniFileDataBaseWriteInt("Section1", "MyInt", pre_value, m_fd);

    const int result_value = IniFileDataBaseReadInt("Section1", "MyInt", -1, m_fd);

    EXPECT_EQ(pre_value, result_value);
}

TEST_F(IniDataBaseTest, ReadInt_MissingEntry_ReturnsDefault)
{
    const int result_value = IniFileDataBaseReadInt("Section1", "Absent", 777, m_fd);
    EXPECT_EQ(777, result_value);
}

TEST_F(IniDataBaseTest, WriteReadFloat_RoundTrip_ReturnsSameValue)
{
    const double pre_value = 3.140000;
    IniFileDataBaseWriteFloat("Section1", "MyFloat", pre_value, m_fd);

    const double result_value = IniFileDataBaseReadFloat("Section1", "MyFloat", 0.0, m_fd);

    EXPECT_DOUBLE_EQ(pre_value, result_value);
}

TEST_F(IniDataBaseTest, WriteReadYesNo_RoundTrip_PreservesFlag)
{
    IniFileDataBaseWriteYesNo("Section1", "FlagOn", 1, m_fd);
    IniFileDataBaseWriteYesNo("Section1", "FlagOff", 0, m_fd);

    EXPECT_NE(0, IniFileDataBaseReadYesNo("Section1", "FlagOn", 0, m_fd));
    EXPECT_EQ(0, IniFileDataBaseReadYesNo("Section1", "FlagOff", 1, m_fd));
}

TEST_F(IniDataBaseTest, LookIfSectionExist_AfterWrite_ReportsPresence)
{
    EXPECT_EQ(0, IniFileDataBaseLookIfSectionExist("FreshSection", m_fd));

    IniFileDataBaseWriteString("FreshSection", "Entry", "value", m_fd);

    EXPECT_NE(0, IniFileDataBaseLookIfSectionExist("FreshSection", m_fd));
}

// Byte-image blobs are the trickiest part of the facade to reproduce in TOML
// (they need base64 encoding), so pin their round-trip explicitly.
TEST_F(IniDataBaseTest, WriteReadByteImage_RoundTrip_PreservesBytes)
{
    unsigned char pre_image[16];
    for (int i = 0; i < 16; ++i)
    {
        pre_image[i] = static_cast<unsigned char>(i * 7 + 1);
    }
    const int pre_type = 42;

    IniFileDataBaseWriteByteImage("BlobSection", "Blob", pre_type, pre_image,
                                  static_cast<int>(sizeof(pre_image)), m_fd);

    int result_type = -1;
    unsigned char result_image[16]{};
    const int result_len = IniFileDataBaseReadByteImage(
        "BlobSection", "Blob", &result_type, result_image,
        static_cast<int>(sizeof(result_image)), m_fd);

    EXPECT_EQ(static_cast<int>(sizeof(pre_image)), result_len);
    EXPECT_EQ(pre_type, result_type);
    EXPECT_EQ(0, std::memcmp(pre_image, result_image, sizeof(pre_image)));
}

} // namespace
