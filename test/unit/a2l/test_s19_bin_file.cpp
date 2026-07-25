// Contract tests for ReadValueFromImage (Src/A2lParser/S19BinFile.c), which
// reads a typed scalar out of an in-memory program image (as loaded from an S19
// / binary file) at a given target address, honouring the image base Offset and
// bounds. It is pure over a caller-supplied BIN_FILE_IMAGE, so it is driven here
// with a stack-built image and no file IO.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

extern "C" {
#include "Blackboard.h"
#include "S19BinFile.h"
}

namespace {

TEST(S19BinImageTest, ReadUdword_LittleEndian_ReturnsValue)
{
    unsigned char pre_image[] = {0x2A, 0x00, 0x00, 0x00};
    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Offset = 0;
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_value{};
    const int result_state = ReadValueFromImage(&pre_bin, 0, 0, BB_UDWORD, &result_value);

    EXPECT_EQ(0, result_state);
    EXPECT_EQ(A2L_ELEM_TYPE_UINT, result_value.Type);
    EXPECT_EQ(int64_t{0x2A}, result_value.Value.Int);
}

TEST(S19BinImageTest, ReadByte_SignedInterpretation)
{
    unsigned char pre_image[] = {0xFF};
    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_value{};
    EXPECT_EQ(0, ReadValueFromImage(&pre_bin, 0, 0, BB_BYTE, &result_value));
    EXPECT_EQ(int64_t{-1}, result_value.Value.Int) << "BB_BYTE is a signed 8-bit read";
}

TEST(S19BinImageTest, ReadUbyteAndUword)
{
    unsigned char pre_image[] = {0xFF, 0x34, 0x12};
    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_ubyte{};
    EXPECT_EQ(0, ReadValueFromImage(&pre_bin, 0, 0, BB_UBYTE, &result_ubyte));
    EXPECT_EQ(int64_t{255}, result_ubyte.Value.Int);

    A2L_SINGLE_VALUE result_uword{};
    EXPECT_EQ(0, ReadValueFromImage(&pre_bin, 1, 0, BB_UWORD, &result_uword));
    EXPECT_EQ(int64_t{0x1234}, result_uword.Value.Int) << "little-endian 16-bit read";
}

TEST(S19BinImageTest, NonZeroOffset_AddressIsRelativeToBase)
{
    unsigned char pre_image[] = {0xAB, 0xCD};
    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Offset = 0x1000;
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_value{};
    // Address 0x1000 maps to Image[0].
    EXPECT_EQ(0, ReadValueFromImage(&pre_bin, 0x1000, 0, BB_UBYTE, &result_value));
    EXPECT_EQ(int64_t{0xAB}, result_value.Value.Int);
}

TEST(S19BinImageTest, ReadFloat_WidensToDouble)
{
    unsigned char pre_image[4];
    const float pre_float = 1.5f;
    std::memcpy(pre_image, &pre_float, sizeof(pre_float));

    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_value{};
    EXPECT_EQ(0, ReadValueFromImage(&pre_bin, 0, 0, BB_FLOAT, &result_value));
    EXPECT_EQ(A2L_ELEM_TYPE_DOUBLE, result_value.Type);
    EXPECT_DOUBLE_EQ(1.5, result_value.Value.Double);
}

TEST(S19BinImageTest, AddressBeyondImage_ReturnsError)
{
    unsigned char pre_image[] = {0x00, 0x00};
    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_value{};
    // A 4-byte read starting at offset 1 needs bytes [1..5) but Size is 2.
    EXPECT_EQ(-1, ReadValueFromImage(&pre_bin, 1, 0, BB_UDWORD, &result_value));
}

TEST(S19BinImageTest, AddressBelowOffset_ReturnsError)
{
    unsigned char pre_image[] = {0x00};
    BIN_FILE_IMAGE pre_bin{};
    pre_bin.Offset = 0x1000;
    pre_bin.Size = sizeof(pre_image);
    pre_bin.Image = pre_image;

    A2L_SINGLE_VALUE result_value{};
    EXPECT_EQ(-1, ReadValueFromImage(&pre_bin, 0x0FFF, 0, BB_UBYTE, &result_value));
}

} // namespace
