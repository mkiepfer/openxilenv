// Characterization test for the A2L lexer (Src/A2lParser/A2LTokenizer.c).
// ParseNextString returns the next whitespace/quote-delimited token, skipping
// C-style block and line comments. This pins the tokenization contract that the
// whole A2L parser is built on. Driven through a small temp .a2l file.

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

extern "C" {
#include "A2LData.h"
#include "A2LTokenizer.h"
}

namespace {

std::string WriteTempA2l(const char* content)
{
    const std::string path = "./xilenv_tokenizer_unittest.a2l";
    FILE* fh = std::fopen(path.c_str(), "wt");
    if (fh != nullptr)
    {
        std::fputs(content, fh);
        std::fclose(fh);
    }
    return path;
}

TEST(A2LTokenizerTest, SkipsCommentsAndWhitespace_ReturnsTokensInOrder)
{
    const std::string pre_path =
        WriteTempA2l("  ABC  /* block comment */ DEF // line comment\n GHI");

    ASAP2_PARSER* parser = InitASAP2Parser(pre_path.c_str(), 0);
    ASSERT_NE(nullptr, parser);

    EXPECT_STREQ("ABC", ParseNextString(parser, 0));
    EXPECT_STREQ("DEF", ParseNextString(parser, 0));
    EXPECT_STREQ("GHI", ParseNextString(parser, 0));

    std::remove(pre_path.c_str());
}

TEST(A2LTokenizerTest, QuotedString_IsReturnedWithoutQuotes)
{
    const std::string pre_path = WriteTempA2l("\"quoted token\" TAIL");

    ASAP2_PARSER* parser = InitASAP2Parser(pre_path.c_str(), 0);
    ASSERT_NE(nullptr, parser);

    EXPECT_STREQ("quoted token", ParseNextString(parser, 0));
    EXPECT_STREQ("TAIL", ParseNextString(parser, 0));

    std::remove(pre_path.c_str());
}

TEST(A2LTokenizerTest, EndOfInput_IsReportedByIsEndOfParsing)
{
    const std::string pre_path = WriteTempA2l("ONLYONE");

    ASAP2_PARSER* parser = InitASAP2Parser(pre_path.c_str(), 0);
    ASSERT_NE(nullptr, parser);

    EXPECT_STREQ("ONLYONE", ParseNextString(parser, 0));
    ParseNextString(parser, 0); // consume past the last token
    EXPECT_NE(0, IsEndOfParsing(parser));

    std::remove(pre_path.c_str());
}

} // namespace
