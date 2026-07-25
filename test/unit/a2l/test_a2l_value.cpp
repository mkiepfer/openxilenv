// Contract tests for the A2L scalar-value conversion helpers
// (Src/A2lParser/A2LValue.c). These convert between the tagged-union
// A2L_SINGLE_VALUE and plain int/double, and clamp a double into a target
// storage width. They are pure (no global state) and sit on the A2L
// calibration read/write path, so their type dispatch, truncation and
// saturation are pinned here.

#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "A2LValue.h"
}

namespace {

TEST(A2LValueTest, ConvertRawValueToInt_TruncatesDoubleTowardZero)
{
    // Integer extraction uses a plain (int) cast -> truncation, not rounding.
    A2L_SINGLE_VALUE pre_value{};
    pre_value.Type = A2L_ELEM_TYPE_DOUBLE;
    pre_value.Value.Double = 3.9;

    EXPECT_EQ(3, ConvertRawValueToInt(&pre_value));
}

TEST(A2LValueTest, ConvertRawValueToInt_UintAndInt_PassThrough)
{
    A2L_SINGLE_VALUE pre_uint{};
    pre_uint.Type = A2L_ELEM_TYPE_UINT;
    pre_uint.Value.Uint = 42u;
    EXPECT_EQ(42, ConvertRawValueToInt(&pre_uint));

    A2L_SINGLE_VALUE pre_int{};
    pre_int.Type = A2L_ELEM_TYPE_INT;
    pre_int.Value.Int = -7;
    EXPECT_EQ(-7, ConvertRawValueToInt(&pre_int));
}

TEST(A2LValueTest, ConvertRawValueToInt_TextReplace_ReturnsZero)
{
    A2L_SINGLE_VALUE pre_value{};
    pre_value.Type = A2L_ELEM_TYPE_TEXT_REPLACE;

    EXPECT_EQ(0, ConvertRawValueToInt(&pre_value));
}

TEST(A2LValueTest, ConvertRawValueToDouble_WidensPerType)
{
    A2L_SINGLE_VALUE pre_uint{};
    pre_uint.Type = A2L_ELEM_TYPE_UINT;
    pre_uint.Value.Uint = 42u;
    EXPECT_DOUBLE_EQ(42.0, ConvertRawValueToDouble(&pre_uint));

    A2L_SINGLE_VALUE pre_double{};
    pre_double.Type = A2L_ELEM_TYPE_PHYS_DOUBLE;
    pre_double.Value.Double = 1.25;
    EXPECT_DOUBLE_EQ(1.25, ConvertRawValueToDouble(&pre_double));
}

TEST(A2LValueTest, ConvertDoubleToRawValue_Uint8_SaturatesAndTagsUint)
{
    A2L_SINGLE_VALUE result_value{};

    ConvertDoubleToRawValue(A2L_ELEM_TARGET_TYPE_UINT8, 300.0, &result_value);

    EXPECT_EQ(A2L_ELEM_TYPE_UINT, result_value.Type);
    EXPECT_EQ(A2L_ELEM_TARGET_TYPE_UINT8, result_value.TargetType);
    EXPECT_EQ(255u, result_value.Value.Uint);
}

TEST(A2LValueTest, ConvertDoubleToRawValue_Uint8_RoundsToNearest)
{
    A2L_SINGLE_VALUE result_value{};

    ConvertDoubleToRawValue(A2L_ELEM_TARGET_TYPE_UINT8, 3.7, &result_value);

    EXPECT_EQ(4u, result_value.Value.Uint);
}

TEST(A2LValueTest, ConvertDoubleToRawValue_Int8_SaturatesToMin)
{
    A2L_SINGLE_VALUE result_value{};

    ConvertDoubleToRawValue(A2L_ELEM_TARGET_TYPE_INT8, -200.0, &result_value);

    EXPECT_EQ(A2L_ELEM_TYPE_INT, result_value.Type);
    EXPECT_EQ(-128, result_value.Value.Int);
}

TEST(A2LValueTest, ConvertDoubleToRawValue_UnknownTargetType_TagsError)
{
    A2L_SINGLE_VALUE result_value{};

    ConvertDoubleToRawValue(A2L_ELEM_TARGET_TYPE_NO_TYPE, 1.0, &result_value);

    EXPECT_EQ(A2L_ELEM_TYPE_ERROR, result_value.Type);
}

TEST(A2LValueTest, ConvertDoubleToPhysValue_TagsPhysDoubleAndStoresValue)
{
    A2L_SINGLE_VALUE result_value{};

    ConvertDoubleToPhysValue(A2L_ELEM_TARGET_TYPE_UINT16, 12.5, &result_value);

    EXPECT_EQ(A2L_ELEM_TYPE_PHYS_DOUBLE, result_value.Type);
    EXPECT_EQ(A2L_ELEM_TARGET_TYPE_UINT16, result_value.TargetType);
    EXPECT_DOUBLE_EQ(12.5, result_value.Value.Double);
    EXPECT_EQ(static_cast<int32_t>(sizeof(A2L_SINGLE_VALUE)), result_value.SizeOfStruct);
}

TEST(A2LValueTest, StringCopyToValue_ShortString_CopiesAndSetsStructSize)
{
    A2L_SINGLE_VALUE result_value{};

    StringCopyToValue(&result_value, "AB", 256);

    EXPECT_STREQ("AB", result_value.Value.String);
    // A short string floors SizeOfStruct to the full fixed struct size.
    EXPECT_EQ(static_cast<int32_t>(sizeof(A2L_SINGLE_VALUE)), result_value.SizeOfStruct);
}

TEST(A2LValueTest, StringCopyToValue_MaxSize_TruncatesSource)
{
    A2L_SINGLE_VALUE result_value{};

    // MaxSize bounds the character count including the terminator slot.
    StringCopyToValue(&result_value, "abcdef", 4);

    EXPECT_STREQ("abc", result_value.Value.String);
}

TEST(A2LValueTest, ValueCopy_DuplicatesSourceValue)
{
    A2L_SINGLE_VALUE pre_src{};
    pre_src.SizeOfStruct = sizeof(A2L_SINGLE_VALUE);
    pre_src.Type = A2L_ELEM_TYPE_UINT;
    pre_src.Value.Uint = 0xABCDu;

    A2L_SINGLE_VALUE result_dst{};
    ValueCopy(&result_dst, &pre_src);

    EXPECT_EQ(A2L_ELEM_TYPE_UINT, result_dst.Type);
    EXPECT_EQ(0xABCDu, result_dst.Value.Uint);
}

} // namespace
