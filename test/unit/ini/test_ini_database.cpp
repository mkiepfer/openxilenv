// Characterization / contract tests for the IniDataBase facade
// (Src/IniFileDataBase/IniDataBase.h). These pin the current read/write
// behaviour so the planned TOML-backed reimplementation (migration step M1)
// can be proven to behave identically.

#include <gtest/gtest.h>

#include <cstdio>
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

// Regression: the two-call "query size, then read into buffer" ReadByteImage pattern used to
// leak the (non-recursive) ini critical section on its size-query path (par_BufferSize<=0 &&
// ret_Buffer==NULL -> goto __OUT without INI_LEAVE_CS). The follow-up read then re-entered the
// CS and deadlocked. This test drives that exact sequence plus a trailing ini call; before the
// fix it hangs (CTest timeout), after the fix it round-trips cleanly. Blob is >512 bytes so it
// spans multiple stored chunks (the ImGui-layout case).
TEST_F(IniDataBaseTest, ReadByteImage_QuerySizeThenReadThenIniCall_NoCsLeakOrDeadlock)
{
    unsigned char pre_image[2000];
    for (int i = 0; i < static_cast<int>(sizeof(pre_image)); ++i)
    {
        pre_image[i] = static_cast<unsigned char>((i * 31 + 7) & 0xFF);
    }
    const int pre_type = 5;
    IniFileDataBaseWriteByteImage("BlobSection", "Blob2", pre_type, pre_image,
                                  static_cast<int>(sizeof(pre_image)), m_fd);

    // 1) Query needed size (buffer size 0, null buffer) — the path that used to leak the CS.
    int result_type = -1;
    const int needed = IniFileDataBaseReadByteImage("BlobSection", "Blob2", &result_type, nullptr, 0, m_fd);
    ASSERT_EQ(static_cast<int>(sizeof(pre_image)), needed);

    // 2) Read into the sized buffer — re-enters the CS; deadlocked before the fix.
    unsigned char result_image[2000]{};
    const int result_len =
        IniFileDataBaseReadByteImage("BlobSection", "Blob2", &result_type, result_image, needed, m_fd);
    EXPECT_EQ(static_cast<int>(sizeof(pre_image)), result_len);
    EXPECT_EQ(pre_type, result_type);
    EXPECT_EQ(0, std::memcmp(pre_image, result_image, sizeof(pre_image)));

    // 3) A further ini call proves the CS was released (also hangs if the size-query leaked it).
    IniFileDataBaseWriteString("AfterBlob", "Key", "ok", m_fd);
    char after_buffer[32]{};
    IniFileDataBaseReadString("AfterBlob", "Key", "X", after_buffer, static_cast<int>(sizeof(after_buffer)),
                              m_fd);
    EXPECT_STREQ("ok", after_buffer);
}

// Real file persistence round-trip through the TOML backend: write values, save
// to a file (TOML), close, reopen (parse TOML), read back. This is what actually
// exercises the toml++ serializer/parser and its quoting — including values that
// contain '=', quotes, backslashes and spaces, section names with dots, and a
// binary ByteImage blob.
TEST(IniDataBaseTomlPersistenceTest, SaveClose_Reopen_RoundTripsValuesAndBlob)
{
    const char* pre_path = "xilenv_toml_roundtrip.toml";

    IniFileDataBaseInit();
    int test_fd = IniFileDataBaseCreateAndOpenNewIniFile(pre_path);
    ASSERT_GE(test_fd, 0);

    IniFileDataBaseWriteString("Section.With.Dots", "PlainKey", "plain value", test_fd);
    IniFileDataBaseWriteString("Special", "WinPath", "C:\\temp\\a b\\x.dat", test_fd);
    IniFileDataBaseWriteString("Special", "TrickyValue", "he said \"hi\" = ok", test_fd);
    IniFileDataBaseWriteInt("Nums", "Int", 12345, test_fd);
    IniFileDataBaseWriteFloat("Nums", "Float", 2.5, test_fd);
    IniFileDataBaseWriteYesNo("Nums", "Flag", 1, test_fd);

    unsigned char pre_blob[20];
    for (int i = 0; i < 20; ++i)
    {
        pre_blob[i] = static_cast<unsigned char>(i * 11 + 3);
    }
    IniFileDataBaseWriteByteImage("Blob", "Img", 7, pre_blob, static_cast<int>(sizeof(pre_blob)), test_fd);

    ASSERT_EQ(0, IniFileDataBaseSave(test_fd, nullptr, INIFILE_DATABAE_OPERATION_WRITE_ONLY));
    IniFileDataBaseClose(test_fd);

    // Reopen from disk -> forces a real TOML parse.
    int result_fd = IniFileDataBaseOpen(pre_path);
    ASSERT_GE(result_fd, 0);

    char result_buffer[256]{};
    IniFileDataBaseReadString("Section.With.Dots", "PlainKey", "X", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), result_fd);
    EXPECT_STREQ("plain value", result_buffer);

    IniFileDataBaseReadString("Special", "WinPath", "X", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), result_fd);
    EXPECT_STREQ("C:\\temp\\a b\\x.dat", result_buffer);

    IniFileDataBaseReadString("Special", "TrickyValue", "X", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), result_fd);
    EXPECT_STREQ("he said \"hi\" = ok", result_buffer);

    EXPECT_EQ(12345, IniFileDataBaseReadInt("Nums", "Int", -1, result_fd));
    EXPECT_DOUBLE_EQ(2.5, IniFileDataBaseReadFloat("Nums", "Float", 0.0, result_fd));
    EXPECT_NE(0, IniFileDataBaseReadYesNo("Nums", "Flag", 0, result_fd));

    int result_type = -1;
    unsigned char result_blob[20]{};
    const int result_len = IniFileDataBaseReadByteImage(
        "Blob", "Img", &result_type, result_blob, static_cast<int>(sizeof(result_blob)), result_fd);
    EXPECT_EQ(20, result_len);
    EXPECT_EQ(7, result_type);
    EXPECT_EQ(0, std::memcmp(pre_blob, result_blob, sizeof(pre_blob)));

    IniFileDataBaseClose(result_fd);
    IniFileDataBaseTerminate();
    std::remove(pre_path);
}

// Engine/layout split: once a layout descriptor is set, "GUI/"-prefixed sections
// addressed through the (engine) main descriptor are transparently redirected to
// the layout file, while engine sections stay in the engine file. Verified by
// turning routing off and inspecting the physical placement in each file.
TEST(IniDataBaseLayoutSplitTest, GuiSectionsRouteToLayoutDescriptor)
{
    const char* pre_engine_path = "xilenv_split_engine.toml";
    const char* pre_layout_path = "xilenv_split_layout.toml";

    IniFileDataBaseInit();
    int test_engineFd = IniFileDataBaseCreateAndOpenNewIniFile(pre_engine_path);
    int test_layoutFd = IniFileDataBaseCreateAndOpenNewIniFile(pre_layout_path);
    ASSERT_GE(test_engineFd, 0);
    ASSERT_GE(test_layoutFd, 0);

    SetMainFileDescriptor(test_engineFd);
    SetLayoutFileDescriptor(test_layoutFd);

    // Written through the ENGINE descriptor; the GUI/ section must be redirected.
    IniFileDataBaseWriteString("GUI/Widgets/Tacho 1", "Variable", "Speed", test_engineFd);
    IniFileDataBaseWriteInt("Scheduler", "Period", 10, test_engineFd);

    // With routing ON, reading the GUI/ section via the engine fd still finds it.
    char result_buffer[64]{};
    IniFileDataBaseReadString("GUI/Widgets/Tacho 1", "Variable", "X", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), test_engineFd);
    EXPECT_STREQ("Speed", result_buffer);

    // Turn routing OFF and inspect the PHYSICAL placement in each file.
    SetLayoutFileDescriptor(-1);
    EXPECT_EQ(0, IniFileDataBaseLookIfSectionExist("GUI/Widgets/Tacho 1", test_engineFd));
    EXPECT_NE(0, IniFileDataBaseLookIfSectionExist("GUI/Widgets/Tacho 1", test_layoutFd));
    EXPECT_NE(0, IniFileDataBaseLookIfSectionExist("Scheduler", test_engineFd));
    EXPECT_EQ(0, IniFileDataBaseLookIfSectionExist("Scheduler", test_layoutFd));

    SetMainFileDescriptor(-1);
    IniFileDataBaseClose(test_engineFd);
    IniFileDataBaseClose(test_layoutFd);
    IniFileDataBaseTerminate();
    std::remove(pre_engine_path);
    std::remove(pre_layout_path);
}

// Dual-read backward compatibility: a legacy INI file (';' comments, unquoted
// values) is not valid TOML, so IniFileDataBaseOpen must fall back to the INI
// parser and still load it. Writing stays TOML; this only keeps old configs
// (and history/MRU entries pointing at them) readable.
TEST(IniDataBaseDualReadTest, LoadsLegacyIniFileViaFallback)
{
    const char* pre_path = "xilenv_legacy_format.ini";

    std::FILE* pre_file = std::fopen(pre_path, "wt");
    ASSERT_NE(nullptr, pre_file);
    std::fputs("; legacy OpenXilEnv ini comment\n"
               "[BasicSettings]\n"
               "  ProjectName=LegacyProject\n"
               "  BlackBoardSize=4096\n"
               "[Scheduler]\n"
               "  Period=0.01\n",
               pre_file);
    std::fclose(pre_file);

    IniFileDataBaseInit();
    int result_fd = IniFileDataBaseOpen(pre_path);
    ASSERT_GE(result_fd, 0) << "legacy INI must load through the fallback parser";

    char result_buffer[128]{};
    IniFileDataBaseReadString("BasicSettings", "ProjectName", "X", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), result_fd);
    EXPECT_STREQ("LegacyProject", result_buffer);
    EXPECT_EQ(4096, IniFileDataBaseReadInt("BasicSettings", "BlackBoardSize", -1, result_fd));
    EXPECT_DOUBLE_EQ(0.01, IniFileDataBaseReadFloat("Scheduler", "Period", 0.0, result_fd));

    IniFileDataBaseClose(result_fd);
    IniFileDataBaseTerminate();
    std::remove(pre_path);
}

// Legacy combined config: GUI/ sections loaded into the engine descriptor must be
// moved to the layout descriptor at startup so the GUI (which reads GUI/ via the
// layout routing) finds its windows. Engine sections stay put.
TEST(IniDataBaseLayoutSplitTest, MoveGuiSectionsToLayout_LegacyCombined)
{
    const char* pre_enginePath = "xilenv_migrate_engine.toml";
    const char* pre_layoutPath = "xilenv_migrate_layout.toml";

    IniFileDataBaseInit();
    int test_engineFd = IniFileDataBaseCreateAndOpenNewIniFile(pre_enginePath);
    int test_layoutFd = IniFileDataBaseCreateAndOpenNewIniFile(pre_layoutPath);
    ASSERT_GE(test_engineFd, 0);
    ASSERT_GE(test_layoutFd, 0);

    // Combined config: GUI/ + engine sections all in the engine fd, written before
    // the layout fd is set (so no routing yet).
    SetMainFileDescriptor(test_engineFd);
    IniFileDataBaseWriteString("GUI/Widgets/W1", "type", "Text", test_engineFd);
    IniFileDataBaseWriteString("GUI/AllTextWindows", "W0", "W1", test_engineFd);
    IniFileDataBaseWriteString("Scheduler", "Period", "0.01", test_engineFd);

    SetLayoutFileDescriptor(test_layoutFd);
    IniFileDataBaseMoveGuiSectionsToLayout();

    // Inspect physical placement with routing off.
    SetLayoutFileDescriptor(-1);
    EXPECT_EQ(0, IniFileDataBaseLookIfSectionExist("GUI/Widgets/W1", test_engineFd));
    EXPECT_NE(0, IniFileDataBaseLookIfSectionExist("GUI/Widgets/W1", test_layoutFd));
    EXPECT_NE(0, IniFileDataBaseLookIfSectionExist("GUI/AllTextWindows", test_layoutFd));
    EXPECT_NE(0, IniFileDataBaseLookIfSectionExist("Scheduler", test_engineFd));
    EXPECT_EQ(0, IniFileDataBaseLookIfSectionExist("Scheduler", test_layoutFd));

    char result_buffer[64]{};
    IniFileDataBaseReadString("GUI/Widgets/W1", "type", "X", result_buffer,
                              static_cast<int>(sizeof(result_buffer)), test_layoutFd);
    EXPECT_STREQ("Text", result_buffer);

    SetMainFileDescriptor(-1);
    IniFileDataBaseClose(test_engineFd);
    IniFileDataBaseClose(test_layoutFd);
    IniFileDataBaseTerminate();
    std::remove(pre_enginePath);
    std::remove(pre_layoutPath);
}

} // namespace
