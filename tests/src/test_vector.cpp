// Created by wuxianggujun on 2024/7/18.
//
#include <gtest/gtest.h>
#include "math/Vector.hpp"

namespace Tina
{
    class VectorTest : public ::testing::Test
    {
    protected:
        Vector2f vec1;
        Vector2f vec2;
        Vector2f vec3;

        void SetUp() override
        {
            vec1 = Vector2f(1.0f, 2.0f);
            vec2 = Vector2f(3.0f, 4.0f);
            vec3 = Vector2f(0.0f, 0.0f);
        }
    };

    TEST_F(VectorTest, DefaultConstructor)
    {
        Vector2f defaultVec;
        EXPECT_FLOAT_EQ(defaultVec.x, 0.0f);
        EXPECT_FLOAT_EQ(defaultVec.y, 0.0f);
    }

    TEST_F(VectorTest, ParameterizedConstructor)
    {
        Vector2f vec(3.0f, 4.0f);
        EXPECT_FLOAT_EQ(vec.x, 3.0f);
        EXPECT_FLOAT_EQ(vec.y, 4.0f);
    }

    TEST_F(VectorTest, FromGLMVec2Constructor)
    {
        glm::vec2 glmVec(5.0f, 6.0f);
        Vector2f vec(glmVec);
        EXPECT_FLOAT_EQ(vec.x, 5.0f);
        EXPECT_FLOAT_EQ(vec.y, 6.0f);
    }

    TEST_F(VectorTest, ElementAccess)
    {
        EXPECT_FLOAT_EQ(vec1[0], 1.0f);
        EXPECT_FLOAT_EQ(vec1[1], 2.0f);
    }

    TEST_F(VectorTest, AdditionOperator)
    {
        Vector2f result = vec1 + vec2;
        EXPECT_FLOAT_EQ(result.x, 4.0f);
        EXPECT_FLOAT_EQ(result.y, 6.0f);

        result = vec1 + 2.0f;
        EXPECT_FLOAT_EQ(result.x, 3.0f);
        EXPECT_FLOAT_EQ(result.y, 4.0f);
    }

    TEST_F(VectorTest, SubtractionOperator)
    {
        Vector2f result = vec2 - vec1;
        EXPECT_FLOAT_EQ(result.x, 2.0f);
        EXPECT_FLOAT_EQ(result.y, 2.0f);

        result = vec2 - 2.0f;
        EXPECT_FLOAT_EQ(result.x, 1.0f);
        EXPECT_FLOAT_EQ(result.y, 2.0f);
    }

    TEST_F(VectorTest, MultiplicationOperator)
    {
        Vector2f result = vec1 * vec2;
        EXPECT_FLOAT_EQ(result.x, 3.0f);
        EXPECT_FLOAT_EQ(result.y, 8.0f);

        result = vec1 * 2.0f;
        EXPECT_FLOAT_EQ(result.x, 2.0f);
        EXPECT_FLOAT_EQ(result.y, 4.0f);
    }

    TEST_F(VectorTest, DivisionOperator)
    {
        Vector2f result = vec2 / 2.0f;
        EXPECT_FLOAT_EQ(result.x, 1.5f);
        EXPECT_FLOAT_EQ(result.y, 2.0f);
    }

    TEST_F(VectorTest, AdditionAssignmentOperator)
    {
        vec1 += vec2;
        EXPECT_FLOAT_EQ(vec1.x, 4.0f);
        EXPECT_FLOAT_EQ(vec1.y, 6.0f);

        vec1 += 2.0f;
        EXPECT_FLOAT_EQ(vec1.x, 6.0f);
        EXPECT_FLOAT_EQ(vec1.y, 8.0f);
    }

    TEST_F(VectorTest, SubtractionAssignmentOperator)
    {
        vec2 -= vec1;
        EXPECT_FLOAT_EQ(vec2.x, 2.0f);
        EXPECT_FLOAT_EQ(vec2.y, 2.0f);

        vec2 -= 2.0f;
        EXPECT_FLOAT_EQ(vec2.x, 0.0f);
        EXPECT_FLOAT_EQ(vec2.y, 0.0f);
    }

    TEST_F(VectorTest, MultiplicationAssignmentOperator)
    {
        vec1 *= vec2;
        EXPECT_FLOAT_EQ(vec1.x, 3.0f);
        EXPECT_FLOAT_EQ(vec1.y, 8.0f);
    }

    TEST_F(VectorTest, EqualityOperator)
    {
        Vector2f vecA(1.0f, 2.0f);
        Vector2f vecB(1.0f, 2.0f);
        Vector2f vecC(3.0f, 4.0f);

        EXPECT_TRUE(vecA == vecB);
        EXPECT_FALSE(vecA == vecC);
    }

    TEST_F(VectorTest, InequalityOperator)
    {
        Vector2f vecA(1.0f, 2.0f);
        Vector2f vecB(1.0f, 2.0f);
        Vector2f vecC(3.0f, 4.0f);

        EXPECT_FALSE(vecA != vecB);
        EXPECT_TRUE(vecA != vecC);
    }

    TEST_F(VectorTest, LessThanOperator)
    {
        Vector2f vecA(1.0f, 2.0f);
        Vector2f vecB(2.0f, 3.0f);
        EXPECT_TRUE(vecA < vecB);
        EXPECT_FALSE(vecB < vecA);
    }

    TEST_F(VectorTest, GreaterThanOperator)
    {
        Vector2f vecA(2.0f, 3.0f);
        Vector2f vecB(1.0f, 2.0f);
        EXPECT_TRUE(vecA > vecB);
        EXPECT_FALSE(vecB > vecA);
    }

    TEST_F(VectorTest, ToString)
    {
        EXPECT_EQ(vec1.toString(), "[ x: 1, y: 2 ]");
        EXPECT_EQ(vec2.toString(), "[ x: 3, y: 4 ]");
    }

    TEST_F(VectorTest, ToStdout)
    {
        testing::internal::CaptureStdout();
        vec1.toStdout();
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_EQ(output, "[ x: 1, y: 2 ]\n");
    }

    TEST_F(VectorTest, MemoryUsage)
    {
        EXPECT_EQ(sizeof(Vector2f), sizeof(float) * 2);
    }
}


int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
