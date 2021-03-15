#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mockFactory.hpp"
#include "parser/registrar.hpp"

using ::testing::AtLeast;

namespace ablateTesting::parser {

using namespace ablate::parser;

TEST(FactoryTests, GetByNameShouldCallGetWithCorrectArguments) {
    // arrange
    MockFactory mockFactory;
    EXPECT_CALL(mockFactory, Get(ArgumentIdentifier<std::string>{.inputName = "input123"})).Times(::testing::Exactly(1)).WillOnce(::testing::Return("result 123"));

    // act
    auto result = mockFactory.GetByName<std::string>("input123");

    // assert
    ASSERT_EQ("result 123", result);
}

TEST(FactoryTests, GetByNameShouldReturnCorrectValue) {
    // arrange
    MockFactory mockFactory;
    EXPECT_CALL(mockFactory, Contains("input123")).Times(::testing::Exactly(1)).WillOnce(::testing::Return(true));
    EXPECT_CALL(mockFactory, Get(ArgumentIdentifier<std::string>{.inputName = "input123"})).Times(::testing::Exactly(1)).WillOnce(::testing::Return("result 123"));

    // act
    auto result = mockFactory.GetByName<std::string, std::string>("input123", "default 123");

    // assert
    ASSERT_EQ("result 123", result);
}

TEST(FactoryTests, GetByNameShouldReturnDefaultValue) {
    // arrange
    MockFactory mockFactory;
    EXPECT_CALL(mockFactory, Contains("input123")).Times(::testing::Exactly(1)).WillOnce(::testing::Return(false));

    // act
    auto result = mockFactory.GetByName<std::string, std::string>("input123", "default 123");

    // assert
    ASSERT_EQ("default 123", result);
}

class DefaultMockClass {
   public:
    std::string name;
    DefaultMockClass(std::string name):name(name){};
};

TEST(FactoryTests, GetByNameShouldReturnDefaultValueClass) {
    // arrange
    MockFactory mockFactory;
    EXPECT_CALL(mockFactory, Contains("input123")).Times(::testing::Exactly(1)).WillOnce(::testing::Return(false));

    // act
    auto result = mockFactory.GetByName<DefaultMockClass, std::shared_ptr<DefaultMockClass>>("input123", std::make_shared<DefaultMockClass>("default 123"));

    // assert
    ASSERT_EQ("default 123", result->name);
}

TEST(FactoryTests, GetByNameShouldReturnDefaultValueWithList) {
    // arrange
    MockFactory mockFactory;
    EXPECT_CALL(mockFactory, Contains("input123")).Times(::testing::Exactly(1)).WillOnce(::testing::Return(false));

    // act
    auto result = mockFactory.GetByName<std::vector<DefaultMockClass>, std::vector<std::shared_ptr<DefaultMockClass>>>("input123", std::vector<std::shared_ptr<DefaultMockClass>>{std::make_shared<DefaultMockClass>("default 123")});

    // assert
    ASSERT_EQ(1, result.size());
    ASSERT_EQ("default 123", result[0]->name);
}

TEST(FactoryTests, GetByNameShouldReturnDefaultValueWithEmptyList) {
    // arrange
    MockFactory mockFactory;
    EXPECT_CALL(mockFactory, Contains("input123")).Times(::testing::Exactly(1)).WillOnce(::testing::Return(false));

    // act
    auto result = mockFactory.GetByName<std::vector<DefaultMockClass>, std::vector<std::shared_ptr<DefaultMockClass>>>("input123", std::vector<std::shared_ptr<DefaultMockClass>>());

    // assert
    ASSERT_EQ(0, result.size());
}

}  // namespace ablateTesting::parser