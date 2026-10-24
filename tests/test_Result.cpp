/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <memory>
#include <string>

#include <common/Result.h>
#include <gtest/gtest.h>

TEST(Result, KeepsAValue)
{
	const ts4::Result<int> result(7);
	ASSERT_TRUE(result);
	EXPECT_EQ(*result, 7);
}

TEST(Result, KeepsAnError)
{
	const ts4::Result<std::string> result = ts4::MakeError("the file {} is not there", "a.txt");
	ASSERT_FALSE(result);
	EXPECT_EQ(result.GetError().message, "the file a.txt is not there");
}

TEST(Result, MovesAValueThatOnlyMoves)
{
	ts4::Result<std::unique_ptr<int>> result(std::make_unique<int>(3));
	const auto value = std::move(result).Value();
	EXPECT_EQ(*value, 3);
}

TEST(Result, GivesVoidResults)
{
	const ts4::Result<void> done;
	EXPECT_TRUE(done);
	const ts4::Result<void> failed = ts4::MakeError("no");
	EXPECT_FALSE(failed);
}
