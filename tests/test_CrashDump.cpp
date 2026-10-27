/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstdint>

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include <gtest/gtest.h>
#include <mod_game/CrashDump.h>
#include <mod_game/Guard.h>
#include <windows.h>

using namespace ts4_mod;

namespace
{

/// A new empty folder in the temporary folder of the user. The destructor removes it.
class TemporaryFolder
{
public:
	explicit TemporaryFolder(const std::string& name)
	    : _path(std::filesystem::temp_directory_path() / (name + "-" + std::to_string(GetCurrentProcessId())))
	{
		std::error_code error;
		std::filesystem::remove_all(_path, error);
		std::filesystem::create_directories(_path, error);
	}
	~TemporaryFolder()
	{
		std::error_code error;
		std::filesystem::remove_all(_path, error);
	}
	TemporaryFolder(const TemporaryFolder&) = delete;
	TemporaryFolder& operator=(const TemporaryFolder&) = delete;

	[[nodiscard]] const std::filesystem::path& Path() const { return _path; }

private:
	std::filesystem::path _path;
};

void Fault(void* /*context*/)
{
	int* volatile address = nullptr;
	// NOLINTNEXTLINE(clang-analyzer-core.NullDereference): the test makes an access violation on purpose.
	*address = 1;
}

void Throw(void* /*context*/)
{
	throw std::runtime_error("bad state");
}

} // namespace

TEST(Guard, CatchesAFaultAndAnException)
{
	const auto fault = GuardedCall(&Fault, nullptr, &CatchCallbackFault);
	EXPECT_FALSE(fault.completed);
	EXPECT_EQ(fault.exceptionCode, static_cast<std::uint32_t>(EXCEPTION_ACCESS_VIOLATION));
	EXPECT_EQ(fault.Describe(), "an access violation (exception 0xC0000005)");

	const auto thrown = GuardedCall(&Throw, nullptr, &CatchCallbackFault);
	EXPECT_FALSE(thrown.completed);
	EXPECT_EQ(thrown.exceptionCode, 0u);
	EXPECT_EQ(thrown.Describe(), "a C++ exception: bad state");

	int value = 0;
	const auto good = GuardedCall([](void* context) { *static_cast<int*>(context) = 5; }, &value, &CatchCallbackFault);
	EXPECT_TRUE(good.completed);
	EXPECT_EQ(value, 5);
}

TEST(CrashDump, RefusesAFolderInTheGameFolder)
{
	const TemporaryFolder game("ts4-crash-dump-game");
	const std::array<std::filesystem::path, 1> gameFolders {game.Path()};
	const auto refused = InstallCrashDumpWriter(game.Path() / "logs", gameFolders);
	ASSERT_FALSE(refused);
	EXPECT_NE(refused.GetError().message.find("is in the game folder"), std::string::npos) << refused.GetError().message;
	EXPECT_FALSE(std::filesystem::exists(game.Path() / "logs"));
	EXPECT_FALSE(InstallCrashDumpWriter("logs", {}));
}

TEST(CrashDump, AGuardWritesADumpOfACaughtFault)
{
	if (CrashDumpWriterInstalled())
	{
		GTEST_SKIP() << "another test of this process installed the writer";
	}
	const TemporaryFolder logs("ts4-crash-dump-logs");
	const TemporaryFolder game("ts4-crash-dump-game");
	const std::array<std::filesystem::path, 1> gameFolders {game.Path()};
	const auto path = InstallCrashDumpWriter(logs.Path(), gameFolders, "ts4-test");
	ASSERT_TRUE(path) << path.GetError().message;
	EXPECT_TRUE(CrashDumpWriterInstalled());
	EXPECT_EQ(path->parent_path(), logs.Path());
	EXPECT_TRUE(path->filename().wstring().starts_with(L"ts4-test-"));
	EXPECT_FALSE(InstallCrashDumpWriter(logs.Path(), gameFolders));

	const auto fault = GuardedCall(&Fault, nullptr, &CatchCallbackFaultWithDump);
	EXPECT_FALSE(fault.completed);
	auto dump = *path;
	dump += L".dmp";
	ASSERT_TRUE(std::filesystem::exists(dump));
	EXPECT_GT(std::filesystem::file_size(dump), 0u);
	EXPECT_TRUE(std::filesystem::is_empty(game.Path()));
}
