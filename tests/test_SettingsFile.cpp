/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mod_core/Folders.h>
#include <mod_core/SettingsFile.h>
#include <nlohmann/json.hpp>

using namespace ts4_mod;

namespace
{

/// A folder for the files of one test. The destructor removes it.
class TemporaryFolder
{
public:
	TemporaryFolder()
	    : _path(std::filesystem::temp_directory_path() /
	            ("ts4-common-settings-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name())))
	{
		std::filesystem::remove_all(_path);
		std::filesystem::create_directories(_path);
	}
	~TemporaryFolder()
	{
		std::error_code ignored;
		std::filesystem::remove_all(_path, ignored);
	}
	TemporaryFolder(const TemporaryFolder&) = delete;
	TemporaryFolder& operator=(const TemporaryFolder&) = delete;

	[[nodiscard]] const std::filesystem::path& Path() const { return _path; }

private:
	std::filesystem::path _path;
};

void WriteFile(const std::filesystem::path& path, const std::string& text)
{
	std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

} // namespace

TEST(SettingsFile, PatchKeepsTheOtherFields)
{
	const auto text = PatchSettingsText(R"({"note": "mine", "camera": {"enabled": false, "zoomSmoothing": 0.5}})",
	                                    {{"camera", {{"enabled", true}}}});
	ASSERT_TRUE(text) << text.GetError().message;
	const auto root = nlohmann::json::parse(*text);
	EXPECT_EQ(root["note"], "mine");
	EXPECT_EQ(root["camera"]["enabled"], true);
	EXPECT_EQ(root["camera"]["zoomSmoothing"], 0.5);
}

TEST(SettingsFile, PatchOfAnEmptyFileMakesAnObject)
{
	const auto text = PatchSettingsText("  \n", {{"needs", {{"enabled", true}}}});
	ASSERT_TRUE(text);
	EXPECT_EQ(nlohmann::json::parse(*text)["needs"]["enabled"], true);
}

TEST(SettingsFile, PatchRefusesABadFile)
{
	EXPECT_FALSE(PatchSettingsText("{ not json", {{"camera", {{"enabled", true}}}}));
	EXPECT_FALSE(PatchSettingsText("[1, 2]", {{"camera", {{"enabled", true}}}}));
}

TEST(SettingsFile, AtomicWriteReplacesTheFileAndLeavesNoTemporaryFile)
{
	const TemporaryFolder folder;
	const auto path = folder.Path() / "mods.json";
	ASSERT_TRUE(WriteTextFileAtomically(path, "{\"first\": 1}\n"));
	ASSERT_TRUE(WriteTextFileAtomically(path, "{\"second\": 2}\n"));
	EXPECT_EQ(ReadTextFile(path), "{\"second\": 2}\n");
	auto temporary = path;
	temporary += ".tmp";
	EXPECT_FALSE(std::filesystem::exists(temporary));
}

TEST(SettingsFile, AtomicWriteFailsInAFolderThatDoesNotExist)
{
	const TemporaryFolder folder;
	EXPECT_FALSE(WriteTextFileAtomically(folder.Path() / "missing" / "mods.json", "{}"));
}

TEST(SettingsFile, TheMonitorFindsAChangeOneTime)
{
	const TemporaryFolder folder;
	const auto path = folder.Path() / "mods.json";
	FileChangeMonitor monitor(path);
	EXPECT_FALSE(monitor.Changed());
	WriteFile(path, "{}");
	EXPECT_TRUE(monitor.Changed());
	EXPECT_FALSE(monitor.Changed());
	WriteFile(path, R"({"overlay": true})");
	EXPECT_TRUE(monitor.Changed());
	std::filesystem::remove(path);
	EXPECT_TRUE(monitor.Changed());
	EXPECT_FALSE(ReadFileStamp(path).exists);
}

TEST(Folders, FindsAPathInAFolderWithAnyCaseAndSeparator)
{
	EXPECT_TRUE(IsInsideFolder("F:/Origin Games/The Sims 4/Game/Bin/mods.json", "F:\\Origin Games\\The Sims 4"));
	EXPECT_TRUE(IsInsideFolder("f:\\ORIGIN GAMES\\THE SIMS 4", "F:/Origin Games/The Sims 4/"));
	EXPECT_TRUE(IsInsideFolder("F:/Origin Games/The Sims 4/a/../b", "F:/Origin Games/The Sims 4"));
	EXPECT_FALSE(IsInsideFolder("F:/Origin Games/The Sims 4 Mods", "F:/Origin Games/The Sims 4"));
	EXPECT_FALSE(IsInsideFolder("C:/Users/Sim/AppData/Local/Sims4Mods", "F:/Origin Games/The Sims 4"));
}

TEST(Folders, PutsTheSettingsInTheLocalApplicationData)
{
	const std::vector<std::filesystem::path> games {"F:/Origin Games/The Sims 4"};
	const auto folders = MakeModFolders({.localAppData = "C:/Users/Sim/AppData/Local"}, games);
	ASSERT_TRUE(folders) << folders.GetError().message;
	EXPECT_EQ(folders->settingsFolder, std::filesystem::path("C:/Users/Sim/AppData/Local/Sims4Mods"));
	EXPECT_EQ(folders->settingsFile, std::filesystem::path("C:/Users/Sim/AppData/Local/Sims4Mods/mods.json"));
	EXPECT_EQ(folders->logFolder, std::filesystem::path("C:/Users/Sim/AppData/Local/Sims4Mods/logs"));
	EXPECT_EQ(folders->startupMarker, std::filesystem::path("C:/Users/Sim/AppData/Local/Sims4Mods/startup-marker.json"));
}

TEST(Folders, UsesThePathsThatTheUserGives)
{
	const std::vector<std::filesystem::path> games {"F:/Origin Games/The Sims 4"};
	const auto folders = MakeModFolders(
	    {.localAppData = "C:/Local", .settingsFile = "E:/runs/test/mods.json", .logFolder = "E:/runs/test/logs"}, games);
	ASSERT_TRUE(folders) << folders.GetError().message;
	EXPECT_EQ(folders->settingsFile, std::filesystem::path("E:/runs/test/mods.json"));
	EXPECT_EQ(folders->logFolder, std::filesystem::path("E:/runs/test/logs"));
	EXPECT_EQ(folders->startupMarker, std::filesystem::path("E:/runs/test/startup-marker.json"));
}

TEST(Folders, RefusesAFolderInTheGameFolder)
{
	const std::vector<std::filesystem::path> games {"F:/Origin Games/The Sims 4"};
	EXPECT_FALSE(MakeModFolders({.localAppData = "F:/Origin Games/The Sims 4/Local"}, games));
	EXPECT_FALSE(
	    MakeModFolders({.localAppData = "C:/Local", .settingsFile = "f:\\origin games\\the sims 4\\mods.json"}, games));
	EXPECT_FALSE(MakeModFolders({.localAppData = "C:/Local", .logFolder = "F:/Origin Games/The Sims 4/Game/Bin/logs"}, games));
}

TEST(Folders, RefusesAMissingOrRelativeFolder)
{
	EXPECT_FALSE(MakeModFolders({}, {}));
	EXPECT_FALSE(MakeModFolders({.localAppData = "Local"}, {}));
	EXPECT_FALSE(MakeModFolders({.localAppData = "C:/Local", .logFolder = "logs"}, {}));
}
