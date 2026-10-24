/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <mod_sdk/ModHost.h>

namespace
{

/// A fake extender: it keeps each line of the log.
struct FakeHost
{
	std::vector<std::pair<Ts4LogLevel, std::string>> lines;
	Ts4HostApi api {};

	FakeHost()
	{
		api.apiVersion = TS4_MOD_API_VERSION;
		api.size = sizeof(Ts4HostApi);
		api.context = this;
		api.loaderVersion = "0.1.0";
		api.gameVersion = "1.128.90.1030";
		api.gameFolder = "F:/Game";
		api.userFolder = "C:/User";
		api.settingsFolder = "C:/User/Settings";
		api.logFolder = "C:/User/Logs";
		api.log = [](void* context, Ts4LogLevel level, const char* text) {
			static_cast<FakeHost*>(context)->lines.emplace_back(level, text);
		};
		api.postTask = [](void*, Ts4TaskFn, void*) -> uint32_t { return 1; };
	}
};

} // namespace

TEST(ModHost, AcceptsACorrectTable)
{
	FakeHost fake;
	const auto host = ts4_mod::sdk::ModHost::Create(&fake.api);
	ASSERT_TRUE(host) << host.GetError().message;
	EXPECT_EQ((*host)->Version().minor, 128u);
	EXPECT_EQ((*host)->UserFolder(), "C:/User");

	(*host)->Info("the mod starts on {}", "1.128.90.1030");
	ASSERT_EQ(fake.lines.size(), 1u);
	EXPECT_EQ(fake.lines[0].first, TS4_LOG_INFO);
	EXPECT_EQ(fake.lines[0].second, "the mod starts on 1.128.90.1030");
}

TEST(ModHost, RefusesAnotherVersion)
{
	FakeHost fake;
	fake.api.apiVersion = TS4_MOD_API_VERSION + 1;
	EXPECT_FALSE(ts4_mod::sdk::ModHost::Create(&fake.api));
}

TEST(ModHost, RefusesASmallTable)
{
	FakeHost fake;
	fake.api.size = TS4_HOST_API_FIRST_SIZE - 1;
	EXPECT_FALSE(ts4_mod::sdk::ModHost::Create(&fake.api));
}

TEST(ModHost, RefusesAMissingFunction)
{
	FakeHost fake;
	fake.api.log = nullptr;
	EXPECT_FALSE(ts4_mod::sdk::ModHost::Create(&fake.api));
}

TEST(ModHost, RefusesABadGameVersion)
{
	FakeHost fake;
	fake.api.gameVersion = "unknown";
	EXPECT_FALSE(ts4_mod::sdk::ModHost::Create(&fake.api));
	EXPECT_FALSE(ts4_mod::sdk::ModHost::Create(nullptr));
}
