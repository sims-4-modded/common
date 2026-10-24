/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <array>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <mod_core/SettingsFields.h>

using namespace ts4_mod::settings;

namespace
{

struct Sample
{
	bool on = false;
	double speed = 1.0;
	size_t mode = 0;
};

constexpr std::array<std::string_view, 3> k_Modes {"slow", "fast", "off"};

const std::array<Field<Sample>, 3> k_Fields {
    Field<Sample> {.group = "Main",
                   .section = "sample",
                   .key = "on",
                   .label = "Sample on",
                   .getToggle = [](const Sample& s) { return s.on; },
                   .setToggle = [](Sample& s, bool v) { s.on = v; }},
    Field<Sample> {.group = "Main",
                   .section = "sample",
                   .key = "speed",
                   .label = "Speed",
                   .kind = FieldKind::Number,
                   .min = 0.0,
                   .max = 5.0,
                   .getNumber = [](const Sample& s) { return s.speed; },
                   .setNumber = [](Sample& s, double v) { s.speed = v; }},
    Field<Sample> {.group = "Main",
                   .section = "sample",
                   .key = "mode",
                   .label = "Mode",
                   .kind = FieldKind::Choice,
                   .choices = k_Modes,
                   .getChoice = [](const Sample& s) { return s.mode; },
                   .setChoice = [](Sample& s, size_t v) { s.mode = v; }},
};

[[nodiscard]] ts4::Result<void> AcceptAll(const Sample& /*sample*/)
{
	return {};
}

} // namespace

TEST(SettingsFields, FindsAFieldAndNamesItsPath)
{
	const auto* field = FindField<Sample>(k_Fields, "sample", "speed");
	ASSERT_NE(field, nullptr);
	EXPECT_EQ(FieldPath(*field), "sample.speed");
	EXPECT_EQ(FindField<Sample>(k_Fields, "sample", "missing"), nullptr);
}

TEST(SettingsFields, EditMakesAPatchOfOneFieldAndClampsANumber)
{
	Sample sample;
	auto edit = EditField(sample, k_Fields[1], FieldValue {9.0}, FieldRange {.min = 0.0, .max = 5.0}, AcceptAll);
	ASSERT_TRUE(edit) << edit.GetError().message;
	EXPECT_DOUBLE_EQ(sample.speed, 5.0);
	EXPECT_EQ(edit->patch, nlohmann::json({{"sample", {{"speed", 5.0}}}}));
	EXPECT_EQ(edit->message, "Speed: 5.00");

	edit = EditField(sample, k_Fields[2], FieldValue {size_t {1}}, FieldRange {}, AcceptAll);
	ASSERT_TRUE(edit);
	EXPECT_EQ(sample.mode, 1u);
	EXPECT_EQ(edit->patch, nlohmann::json({{"sample", {{"mode", "fast"}}}}));
	EXPECT_EQ(std::get<size_t>(ValueOf(k_Fields[2], sample)), 1u);
}

TEST(SettingsFields, EditRefusesAWrongTypeAndASettingThatTheCheckRefuses)
{
	Sample sample;
	EXPECT_FALSE(EditField(sample, k_Fields[0], FieldValue {1.0}, FieldRange {}, AcceptAll));
	EXPECT_FALSE(EditField(sample, k_Fields[2], FieldValue {size_t {7}}, FieldRange {}, AcceptAll));
	const auto refuse = [](const Sample& /*next*/) -> ts4::Result<void> { return ts4::MakeError("no"); };
	EXPECT_FALSE(EditField(sample, k_Fields[0], FieldValue {true}, FieldRange {}, refuse));
	EXPECT_FALSE(sample.on);
}

TEST(SettingsFields, ReadFieldChecksTheType)
{
	const auto object = nlohmann::json::parse(R"({"on": true, "speed": 2, "name": 3})");
	bool on = false;
	double speed = 0.0;
	std::string name;
	double missing = 4.0;
	EXPECT_TRUE(ReadField(object, "on", on));
	EXPECT_TRUE(ReadField(object, "speed", speed));
	EXPECT_FALSE(ReadField(object, "name", name));
	EXPECT_TRUE(ReadField(object, "missing", missing));
	EXPECT_TRUE(on);
	EXPECT_DOUBLE_EQ(speed, 2.0);
	EXPECT_DOUBLE_EQ(missing, 4.0);
}
