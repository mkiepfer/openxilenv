// Contract tests for the saturating type converters in
// Src/Blackboard/BlackboardConvertFromTo.h. These pure static-inline helpers
// clamp 64-bit integer / double source values into the narrower blackboard
// storage types without wrapping around, and are used on every typed
// read/write. Their saturation boundaries and rounding are pinned here.

#include <gtest/gtest.h>

#include <cstdint>

extern "C" {
#include "BlackboardConvertFromTo.h"
}

namespace {

// sc_convert_qword2byte: int64 -> int8, clamped to [-128, 127].

TEST(BlackboardConvertTest, Qword2Byte_InRange_PassesThrough)
{
    EXPECT_EQ(int8_t{42}, sc_convert_qword2byte(42));
    EXPECT_EQ(int8_t{-42}, sc_convert_qword2byte(-42));
}

TEST(BlackboardConvertTest, Qword2Byte_AboveMax_SaturatesToMax)
{
    EXPECT_EQ(int8_t{127}, sc_convert_qword2byte(127));
    EXPECT_EQ(int8_t{127}, sc_convert_qword2byte(128));
    EXPECT_EQ(int8_t{127}, sc_convert_qword2byte(1000000));
}

TEST(BlackboardConvertTest, Qword2Byte_BelowMin_SaturatesToMin)
{
    EXPECT_EQ(int8_t{-128}, sc_convert_qword2byte(-128));
    EXPECT_EQ(int8_t{-128}, sc_convert_qword2byte(-129));
    EXPECT_EQ(int8_t{-128}, sc_convert_qword2byte(-1000000));
}

// sc_convert_uqword2ubyte: uint64 -> uint8, clamped to [0, 255].

TEST(BlackboardConvertTest, Uqword2Ubyte_InRange_PassesThrough)
{
    EXPECT_EQ(uint8_t{0}, sc_convert_uqword2ubyte(0));
    EXPECT_EQ(uint8_t{200}, sc_convert_uqword2ubyte(200));
}

TEST(BlackboardConvertTest, Uqword2Ubyte_AboveMax_SaturatesTo255)
{
    EXPECT_EQ(uint8_t{255}, sc_convert_uqword2ubyte(255));
    EXPECT_EQ(uint8_t{255}, sc_convert_uqword2ubyte(256));
    EXPECT_EQ(uint8_t{255}, sc_convert_uqword2ubyte(70000));
}

// sc_convert_qword2word: int64 -> int16, clamped to [-32768, 32767].

TEST(BlackboardConvertTest, Qword2Word_Boundaries_Saturate)
{
    EXPECT_EQ(int16_t{32767}, sc_convert_qword2word(32767));
    EXPECT_EQ(int16_t{32767}, sc_convert_qword2word(32768));
    EXPECT_EQ(int16_t{-32768}, sc_convert_qword2word(-32768));
    EXPECT_EQ(int16_t{-32768}, sc_convert_qword2word(-32769));
}

// sc_convert_qword2dword: int64 -> int32, clamped to [INT32_MIN, INT32_MAX].

TEST(BlackboardConvertTest, Qword2Dword_Boundaries_Saturate)
{
    EXPECT_EQ(INT32_MAX, sc_convert_qword2dword(static_cast<int64_t>(INT32_MAX)));
    EXPECT_EQ(INT32_MAX, sc_convert_qword2dword(static_cast<int64_t>(INT32_MAX) + 1));
    EXPECT_EQ(INT32_MIN, sc_convert_qword2dword(static_cast<int64_t>(INT32_MIN)));
    EXPECT_EQ(INT32_MIN, sc_convert_qword2dword(static_cast<int64_t>(INT32_MIN) - 1));
}

// sc_convert_double2byte: double -> int8, clamped to [-128, 127] with rounding.

TEST(BlackboardConvertTest, Double2Byte_Rounds_ToNearest)
{
    EXPECT_EQ(int8_t{2}, sc_convert_double2byte(2.4));
    EXPECT_EQ(int8_t{3}, sc_convert_double2byte(2.6));
}

TEST(BlackboardConvertTest, Double2Byte_HalfValues_RoundAwayFromZero)
{
    // round() rounds halves away from zero (0.5 -> 1, -0.5 -> -1).
    EXPECT_EQ(int8_t{1}, sc_convert_double2byte(0.5));
    EXPECT_EQ(int8_t{-1}, sc_convert_double2byte(-0.5));
    EXPECT_EQ(int8_t{3}, sc_convert_double2byte(2.5));
}

TEST(BlackboardConvertTest, Double2Byte_OutOfRange_Saturates)
{
    EXPECT_EQ(int8_t{127}, sc_convert_double2byte(500.0));
    EXPECT_EQ(int8_t{-128}, sc_convert_double2byte(-500.0));
}

// sc_convert_double2ubyte: double -> uint8, clamped to [0, 255] with rounding.

TEST(BlackboardConvertTest, Double2Ubyte_Negative_SaturatesToZero)
{
    EXPECT_EQ(uint8_t{0}, sc_convert_double2ubyte(-0.4));
    EXPECT_EQ(uint8_t{0}, sc_convert_double2ubyte(-100.0));
}

TEST(BlackboardConvertTest, Double2Ubyte_AboveMax_SaturatesTo255)
{
    EXPECT_EQ(uint8_t{255}, sc_convert_double2ubyte(300.0));
}

TEST(BlackboardConvertTest, Double2Ubyte_Rounds_ToNearest)
{
    EXPECT_EQ(uint8_t{100}, sc_convert_double2ubyte(99.6));
}

// sc_convert_double2float: double -> float, a plain narrowing cast.

TEST(BlackboardConvertTest, Double2Float_NarrowsToFloatPrecision)
{
    EXPECT_FLOAT_EQ(1.5f, sc_convert_double2float(1.5));
    EXPECT_FLOAT_EQ(0.1f, sc_convert_double2float(0.1));
}

} // namespace
