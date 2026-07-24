// Characterization / contract tests for the Blackboard signal database
// (Src/Blackboard/BlackboardAccess.h + Blackboard.h). The blackboard is the
// central data model the GUI reads/writes every frame, so these pin its
// add/read/write/remove/type contract before the Qt -> ImGui migration.

#include <gtest/gtest.h>

extern "C" {
#include "Blackboard.h"
#include "BlackboardAccess.h"
}

namespace {

// The blackboard keeps global C state that is NOT cleanly re-initializable:
// a close_blackboard() + init_blackboard() cycle corrupts it (a fresh
// add_bbvari then fails with a negative error code and later crashes). So the
// blackboard is initialized ONCE per test suite; individual tests stay
// isolated by using unique variable names.
class BlackboardTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        // (bbvari_count, CopyBB2ProcOnlyIfWrFlagSet, AllowBBVarsWithSameAddr,
        //  conv_error2message)
        init_blackboard(4096, 0, 0, 0);
    }

    static void TearDownTestSuite()
    {
        close_blackboard();
    }
};

// Removing the single (last) variable empties the blackboard. The name lookup
// must then report an invalid VID instead of crashing. This is the regression
// test for the emptied-hash-block bug fixed in BlackboardHashIndex.c
// (RemoveBinaryHashKey used to leave a stale empty hash block whose Hash[0]
// still matched the removed name, so the lookup dereferenced a dangling
// blackboard index -> SIGSEGV). It only exercises that path while the
// blackboard is truly empty, so it is defined first to run before any other
// test adds a variable; the leading ASSERT pins that precondition and fails
// loudly (rather than silently degrading to a trivial contract check) if the
// suite is ever reordered.
TEST_F(BlackboardTest, RemoveLastBbvari_ThenLookupByName_ReturnsInvalidOnEmptyBlackboard)
{
    ASSERT_EQ(get_num_of_bbvaris(), 0u) << "must run first: blackboard must be empty";

    const VID pre_vid = add_bbvari("Test.Signal.Remove", BB_DOUBLE, "");
    ASSERT_GT(pre_vid, 0);

    remove_bbvari(pre_vid);
    ASSERT_EQ(get_num_of_bbvaris(), 0u);

    EXPECT_LE(get_bbvarivid_by_name("Test.Signal.Remove"), 0);
}

TEST_F(BlackboardTest, AddBbvari_ThenLookupByName_ReturnsSameVid)
{
    const VID pre_vid = add_bbvari("Test.Signal.A", BB_DOUBLE, "km/h");
    ASSERT_GT(pre_vid, 0);

    const VID result_vid = get_bbvarivid_by_name("Test.Signal.A");

    EXPECT_EQ(pre_vid, result_vid);
}

TEST_F(BlackboardTest, GetBbvariType_ReturnsRegisteredType)
{
    const VID pre_vid = add_bbvari("Test.Signal.Typed", BB_UDWORD, "");
    ASSERT_GT(pre_vid, 0);

    EXPECT_EQ(BB_UDWORD, get_bbvaritype(pre_vid));
}

TEST_F(BlackboardTest, WriteReadDouble_RoundTrip_ReturnsSameValue)
{
    const VID pre_vid = add_bbvari("Test.Signal.Double", BB_DOUBLE, "");
    ASSERT_GT(pre_vid, 0);

    write_bbvari_double(pre_vid, 123.456);

    EXPECT_DOUBLE_EQ(123.456, read_bbvari_double(pre_vid));
}

TEST_F(BlackboardTest, WriteReadUdword_RoundTrip_ReturnsSameValue)
{
    const VID pre_vid = add_bbvari("Test.Signal.Udword", BB_UDWORD, "");
    ASSERT_GT(pre_vid, 0);

    write_bbvari_udword(pre_vid, 0xDEADBEEFu);

    EXPECT_EQ(0xDEADBEEFu, read_bbvari_udword(pre_vid));
}

// A signal without an explicit conversion converts raw==phys, so a value
// written raw reads back unchanged through the physical-conversion path.
TEST_F(BlackboardTest, ReadConvertDouble_NoConversion_EqualsRawValue)
{
    const VID pre_vid = add_bbvari("Test.Signal.Conv", BB_DOUBLE, "");
    ASSERT_GT(pre_vid, 0);

    write_bbvari_double(pre_vid, 42.0);

    EXPECT_DOUBLE_EQ(42.0, read_bbvari_convert_double(pre_vid));
}

// Registering the same name twice hands out the same VID (attach semantics),
// not a second independent variable.
TEST_F(BlackboardTest, AddBbvari_SameNameTwice_ReturnsSameVid)
{
    const VID pre_first = add_bbvari("Test.Signal.Shared", BB_DOUBLE, "");
    const VID pre_second = add_bbvari("Test.Signal.Shared", BB_DOUBLE, "");

    ASSERT_GT(pre_first, 0);
    EXPECT_EQ(pre_first, pre_second);
}

} // namespace
