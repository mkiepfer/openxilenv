// Contract tests for the A2L string bump-allocator (Src/A2lParser/A2LBuffer.c).
// During A2L parsing, variable-length strings are packed into growable 1 MB
// blocks; AddStringToBuffer returns a stable pointer to a copied string and
// advances the block write cursor. The allocator uses only the core memory
// pool (my_malloc/my_realloc), so it is tested against an in-memory ASAP2_DATA
// with no file IO.

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "A2LData.h"
#include "A2LBuffer.h"
#include "MyMemory.h"
}

namespace {

// Frees the blocks an ASAP2_DATA accumulated so each test stays leak-free.
void FreeStringBuffers(ASAP2_DATA* p)
{
    for (int i = 0; i < p->StringBufferCounter; i++)
    {
        my_free(p->StringBuffers[i].Data);
    }
    my_free(p->StringBuffers);
    p->StringBuffers = nullptr;
    p->StringBufferCounter = 0;
}

TEST(A2LBufferTest, AddOneStringBuffer_AllocatesFirstBlock)
{
    ASAP2_DATA test_data{};

    const int result_state = AddOneStringBuffer(&test_data);

    EXPECT_EQ(0, result_state);
    EXPECT_EQ(1, test_data.StringBufferCounter);
    ASSERT_NE(nullptr, test_data.StringBuffers);
    EXPECT_EQ(0, test_data.StringBuffers[0].Pointer);

    FreeStringBuffers(&test_data);
}

TEST(A2LBufferTest, AddStringToBuffer_CopiesStringAndAdvancesCursor)
{
    ASAP2_DATA test_data{};
    ASSERT_EQ(0, AddOneStringBuffer(&test_data));

    char* result_ptr = AddStringToBuffer(&test_data, "hi");

    ASSERT_NE(nullptr, result_ptr);
    EXPECT_STREQ("hi", result_ptr);
    // "hi" + terminator == 3 bytes consumed.
    EXPECT_EQ(3, test_data.StringBuffers[0].Pointer);

    FreeStringBuffers(&test_data);
}

TEST(A2LBufferTest, AddStringToBuffer_TwoStrings_AreIndependentAndPacked)
{
    ASAP2_DATA test_data{};
    ASSERT_EQ(0, AddOneStringBuffer(&test_data));

    char* result_first = AddStringToBuffer(&test_data, "alpha");
    char* result_second = AddStringToBuffer(&test_data, "beta");

    ASSERT_NE(nullptr, result_first);
    ASSERT_NE(nullptr, result_second);
    EXPECT_STREQ("alpha", result_first);
    EXPECT_STREQ("beta", result_second);
    EXPECT_NE(result_first, result_second);
    // "alpha\0" (6) + "beta\0" (5) == 11 bytes packed contiguously.
    EXPECT_EQ(result_first + 6, result_second);
    EXPECT_EQ(11, test_data.StringBuffers[0].Pointer);

    FreeStringBuffers(&test_data);
}

} // namespace
