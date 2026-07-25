// Characterization tests for the arithmetic equation evaluator
// (Src/Blackboard/EquationParser.c). direct_solve_equation() parses a formula
// string and evaluates it immediately to a double. It is the engine behind
// signal conversions, stimulus ramps and script math, so its operator
// precedence, parentheses and built-in functions are pinned here.
//
// Only pure-numeric expressions are used (no blackboard variable names), so the
// evaluator's local execution stack is exercised without requiring blackboard
// bring-up.

#include <gtest/gtest.h>

#include <cmath>

extern "C" {
#include "EquationParser.h"
}

namespace {

constexpr double EPS{1e-9};

TEST(EquationParserTest, DirectSolve_AdditionAndMultiplication_RespectsPrecedence)
{
    // '*' binds tighter than '+': 1 + (2 * 3) == 7, not (1 + 2) * 3 == 9.
    EXPECT_NEAR(7.0, direct_solve_equation("1+2*3"), EPS);
}

TEST(EquationParserTest, DirectSolve_Parentheses_OverridePrecedence)
{
    EXPECT_NEAR(9.0, direct_solve_equation("(1+2)*3"), EPS);
}

TEST(EquationParserTest, DirectSolve_IntegerLiteralDivision_UsesIntegerArithmetic)
{
    // Characterization: integer literals are evaluated with 64-bit integer
    // arithmetic, so '/' between two integer literals truncates (10/4 == 2, not
    // 2.5). This is a deliberate, non-obvious contract of the direct solver.
    EXPECT_NEAR(2.0, direct_solve_equation("10/4"), EPS);
}

TEST(EquationParserTest, DirectSolve_FloatLiteralDivision_PromotesToTrueDivision)
{
    // A floating-point literal on either side promotes the operation to double
    // arithmetic, yielding the fractional result.
    EXPECT_NEAR(2.5, direct_solve_equation("10.0/4"), EPS);
}

TEST(EquationParserTest, DirectSolve_UnaryMinus_NegatesOperand)
{
    EXPECT_NEAR(-3.0, direct_solve_equation("2-5"), EPS);
    EXPECT_NEAR(7.0, direct_solve_equation("10+-3"), EPS);
}

TEST(EquationParserTest, DirectSolve_Whitespace_IsIgnored)
{
    EXPECT_NEAR(6.0, direct_solve_equation("  2  *  3  "), EPS);
}

TEST(EquationParserTest, DirectSolve_Sqrt_ReturnsSquareRoot)
{
    EXPECT_NEAR(4.0, direct_solve_equation("sqrt(16)"), EPS);
}

TEST(EquationParserTest, DirectSolve_Abs_ReturnsMagnitude)
{
    EXPECT_NEAR(5.0, direct_solve_equation("abs(0-5)"), EPS);
}

TEST(EquationParserTest, DirectSolve_Pow_RaisesToExponent)
{
    EXPECT_NEAR(1024.0, direct_solve_equation("pow(2,10)"), EPS);
}

TEST(EquationParserTest, DirectSolve_MinMax_SelectExtremes)
{
    EXPECT_NEAR(3.0, direct_solve_equation("min(3,7)"), EPS);
    EXPECT_NEAR(7.0, direct_solve_equation("max(3,7)"), EPS);
}

TEST(EquationParserTest, DirectSolve_Sin_OfZero_IsZero)
{
    EXPECT_NEAR(0.0, direct_solve_equation("sin(0)"), EPS);
}

TEST(EquationParserTest, DirectSolve_NestedFunctionsAndArithmetic_Combine)
{
    // sqrt(pow(3,2) + pow(4,2)) == sqrt(9 + 16) == sqrt(25) == 5 (Pythagoras).
    EXPECT_NEAR(5.0, direct_solve_equation("sqrt(pow(3,2)+pow(4,2))"), EPS);
}

// direct_solve_equation_err_string() reports an error state (0 == success) and
// writes the result into *erg.

TEST(EquationParserTest, DirectSolveErrString_ValidEquation_ReturnsSuccessAndValue)
{
    double result_value{0.0};
    char* result_errstring{nullptr};

    const int result_state = direct_solve_equation_err_string("2*21", &result_value, &result_errstring);

    EXPECT_EQ(0, result_state) << "a well-formed equation must not report an error";
    EXPECT_NEAR(42.0, result_value, EPS);

    FreeErrStringBuffer(&result_errstring);
}

TEST(EquationParserTest, DirectSolveErrString_UnbalancedParenthesis_ReportsError)
{
    double result_value{0.0};
    char* result_errstring{nullptr};

    const int result_state = direct_solve_equation_err_string("(1+2", &result_value, &result_errstring);

    EXPECT_NE(0, result_state) << "a syntactically invalid equation must report an error state";

    FreeErrStringBuffer(&result_errstring);
}

// --- Bitwise operators and bit-field functions --------------------------------

TEST(EquationParserTest, DirectSolve_BitwiseOrXorOperators)
{
    EXPECT_NEAR(15.0, direct_solve_equation("12|3"), EPS);
    EXPECT_NEAR(6.0, direct_solve_equation("12^10"), EPS);
}

TEST(EquationParserTest, DirectSolve_BitwiseAndFunction_ComputesBitwiseAnd)
{
    // The and()/or()/xor() built-in functions perform true bitwise operations.
    EXPECT_NEAR(8.0, direct_solve_equation("and(12,10)"), EPS);
    EXPECT_NEAR(15.0, direct_solve_equation("or(12,3)"), EPS);
    EXPECT_NEAR(6.0, direct_solve_equation("xor(12,10)"), EPS);
}

// CHARACTERIZATION OF A KNOWN BUG: the infix '&' operator tokenizes as
// EQU_OP_BITWISE_AND, but and_bits() (EquationParser.c) dispatches
// EQU_OP_LOGICAL_AND instead, so "12&10" evaluates as a *logical* and (both
// operands truthy -> 1) rather than the bitwise 8. This pins the current
// behaviour so the migration stays equivalent; use and(a,b) for real bitwise
// and. '|' and '^' are unaffected and remain bitwise (see above).
TEST(EquationParserTest, DirectSolve_InfixAmpersand_IsLogicalAndDueToKnownBug)
{
    EXPECT_NEAR(1.0, direct_solve_equation("12&10"), EPS);
    EXPECT_NEAR(0.0, direct_solve_equation("12&0"), EPS);
}

TEST(EquationParserTest, DirectSolve_ShiftOperators_LeftAndRight)
{
    EXPECT_NEAR(16.0, direct_solve_equation("1<<4"), EPS);
    EXPECT_NEAR(16.0, direct_solve_equation("256>>4"), EPS);
}

TEST(EquationParserTest, DirectSolve_GetBits_ExtractsBitField)
{
    // getbits(value, start, size): (value >> start) masked to `size` bits.
    EXPECT_NEAR(15.0, direct_solve_equation("getbits(240,4,4)"), EPS);
    EXPECT_NEAR(15.0, direct_solve_equation("getbits(255,0,4)"), EPS);
}

TEST(EquationParserTest, DirectSolve_SetBits_ReplacesBitField)
{
    EXPECT_NEAR(255.0, direct_solve_equation("setbits(0,0,8,255)"), EPS);
    EXPECT_NEAR(240.0, direct_solve_equation("setbits(255,0,4,0)"), EPS);
}

TEST(EquationParserTest, DirectSolve_AndOrXorBits_MaskWindowedValue)
{
    EXPECT_NEAR(240.0, direct_solve_equation("andbits(255,0,4,0)"), EPS);
    EXPECT_NEAR(255.0, direct_solve_equation("orbits(240,0,4,15)"), EPS);
    EXPECT_NEAR(240.0, direct_solve_equation("xorbits(255,0,4,15)"), EPS);
}

// --- Comparison and logical operators (yield 1.0 / 0.0) -----------------------

TEST(EquationParserTest, DirectSolve_ComparisonOperators_YieldBoolean)
{
    EXPECT_NEAR(1.0, direct_solve_equation("5==5"), EPS);
    EXPECT_NEAR(0.0, direct_solve_equation("5==6"), EPS);
    EXPECT_NEAR(1.0, direct_solve_equation("3<7"), EPS);
    EXPECT_NEAR(0.0, direct_solve_equation("7<3"), EPS);
    EXPECT_NEAR(1.0, direct_solve_equation("3!=4"), EPS);
}

TEST(EquationParserTest, DirectSolve_LogicalOperators_OperateOnTruthiness)
{
    EXPECT_NEAR(0.0, direct_solve_equation("2&&0"), EPS);
    EXPECT_NEAR(1.0, direct_solve_equation("2&&3"), EPS);
    EXPECT_NEAR(0.0, direct_solve_equation("0||0"), EPS);
    EXPECT_NEAR(1.0, direct_solve_equation("5||0"), EPS);
}

// --- Integer cast / clamp functions ------------------------------------------

TEST(EquationParserTest, DirectSolve_Uint64AndInt64Casts)
{
    EXPECT_NEAR(255.0, direct_solve_equation("uint64(255)"), EPS);
    EXPECT_NEAR(-3.0, direct_solve_equation("int64(0-3)"), EPS);
}

TEST(EquationParserTest, DirectSolve_Uint8Cast_ClampsToByteRange)
{
    EXPECT_NEAR(255.0, direct_solve_equation("uint8(300)"), EPS);
}

} // namespace
