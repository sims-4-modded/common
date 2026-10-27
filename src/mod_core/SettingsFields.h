/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cmath>
#include <cstddef>

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#include <common/Result.h>
#include <nlohmann/json.hpp>

namespace ts4_mod::settings
{

/// A change of the settings: a text for the user and the change of mods.json as a JSON merge patch.
struct SettingsEdit
{
	std::string message;  ///< The text for the user, for example "Camera mod: on".
	nlohmann::json patch; ///< The change of the settings file.
};

/// The control that changes a field.
enum class FieldKind
{
	Toggle, ///< A check box.
	Number, ///< A slider.
	Choice, ///< A list of names.
};

/// The lowest and the highest value that a number field can have now.
struct FieldRange
{
	double min = 0.0;
	double max = 0.0;

	friend bool operator==(const FieldRange&, const FieldRange&) = default;
};

/// The value of a field: a toggle, a number, or the index of a choice.
using FieldValue = std::variant<bool, double, size_t>;

/// One value of the settings `S` that the menu bar shows and changes. A mod keeps a table of its fields.
template <typename S>
struct Field
{
	std::string_view group {};   ///< The heading of the part of the menu.
	std::string_view section {}; ///< The JSON object of the field in mods.json, for example "camera".
	std::string_view key {};     ///< The name of the field in its JSON object.
	std::string_view label {};   ///< The text of the control.
	std::string_view help {};    ///< The tooltip of the control.
	FieldKind kind = FieldKind::Toggle;
	double min = 0.0;                             ///< The lowest value that the file permits (numbers only).
	double max = 0.0;                             ///< The highest value that the file permits (numbers only).
	double step = 0.0;                            ///< The step of the keyboard and of the text entry (numbers only).
	std::string_view format {};                   ///< The printf format of the value (numbers only).
	bool logarithmic = false;                     ///< The slider moves on a logarithmic scale.
	bool restartOnly = false;                     ///< The change works only after a restart of the game.
	std::span<const std::string_view> choices {}; ///< The names of a choice field, as mods.json writes them.
	bool (*getToggle)(const S&) = nullptr;
	void (*setToggle)(S&, bool) = nullptr;
	double (*getNumber)(const S&) = nullptr;
	void (*setNumber)(S&, double) = nullptr;
	size_t (*getChoice)(const S&) = nullptr;
	void (*setChoice)(S&, size_t) = nullptr;
};

/// The path of the field in the settings file, for example "camera.zoomSmoothing".
template <typename S>
[[nodiscard]] std::string FieldPath(const Field<S>& field)
{
	return field.section.empty() ? std::string(field.key) : std::format("{}.{}", field.section, field.key);
}

/// Find a field by its JSON object and its name. Return null if there is no such field.
template <typename S>
[[nodiscard]] const Field<S>* FindField(std::span<const Field<S>> fields, std::string_view section, std::string_view key)
{
	const auto found = std::ranges::find_if(fields, [&](const Field<S>& f) { return f.section == section && f.key == key; });
	return found == fields.end() ? nullptr : &*found;
}

/// The value of a field in the settings.
template <typename S>
[[nodiscard]] FieldValue ValueOf(const Field<S>& field, const S& settings)
{
	switch (field.kind)
	{
	case FieldKind::Toggle:
		return field.getToggle(settings);
	case FieldKind::Number:
		return field.getNumber(settings);
	case FieldKind::Choice:
		return field.getChoice(settings);
	}
	return false;
}

/// The value as JSON, in the form of mods.json.
template <typename S>
[[nodiscard]] nlohmann::json FieldJson(const Field<S>& field, const FieldValue& value)
{
	if (const auto* toggle = std::get_if<bool>(&value))
	{
		return *toggle;
	}
	if (const auto* number = std::get_if<double>(&value))
	{
		return *number;
	}
	const auto index = std::get<size_t>(value);
	return index < field.choices.size() ? std::string(field.choices[index]) : std::string();
}

/// A text such as "Smooth zoom: on" for the user.
template <typename S>
[[nodiscard]] std::string DescribeEdit(const Field<S>& field, const FieldValue& value)
{
	if (const auto* toggle = std::get_if<bool>(&value))
	{
		return std::format("{}: {}", field.label, *toggle ? "on" : "off");
	}
	if (const auto* number = std::get_if<double>(&value))
	{
		return std::format("{}: {:.2f}", field.label, *number);
	}
	return std::format("{}: {}", field.label, FieldJson(field, value).template get<std::string>());
}

/// Change one field of `settings`. A number goes into `range`. `check` reads the changed settings as the file would
/// and returns an error for settings that the file refuses. Return the change of the settings file (a JSON merge
/// patch with only this field) and a message. Return an error and keep `settings` if the value has a wrong type, or
/// if `check` refuses the new settings.
template <typename S, typename Check>
[[nodiscard]] ts4::Result<SettingsEdit> EditField(S& settings, const Field<S>& field, const FieldValue& value, FieldRange range,
                                                  Check&& check)
{
	auto next = settings;
	FieldValue stored = value;
	switch (field.kind)
	{
	case FieldKind::Toggle:
		if (!std::holds_alternative<bool>(value))
		{
			return ts4::MakeError("'{}' must be on or off", FieldPath(field));
		}
		field.setToggle(next, std::get<bool>(value));
		break;
	case FieldKind::Number:
	{
		const auto* number = std::get_if<double>(&value);
		if (number == nullptr || !std::isfinite(*number))
		{
			return ts4::MakeError("'{}' must be a number", FieldPath(field));
		}
		const auto clamped = std::clamp(*number, range.min, range.max);
		field.setNumber(next, clamped);
		stored = clamped;
		break;
	}
	case FieldKind::Choice:
		if (!std::holds_alternative<size_t>(value) || std::get<size_t>(value) >= field.choices.size())
		{
			return ts4::MakeError("'{}' must be one of its names", FieldPath(field));
		}
		field.setChoice(next, std::get<size_t>(value));
		break;
	}
	// The menu bar never makes settings that the file would refuse.
	if (const ts4::Result<void> checked = check(next); !checked)
	{
		return ts4::MakeError("the change of '{}' is not correct: {}", FieldPath(field), checked.GetError().message);
	}
	nlohmann::json patch;
	if (field.section.empty())
	{
		patch[std::string(field.key)] = FieldJson(field, stored);
	}
	else
	{
		patch[std::string(field.section)][std::string(field.key)] = FieldJson(field, stored);
	}
	settings = std::move(next);
	return SettingsEdit {.message = DescribeEdit(field, stored), .patch = std::move(patch)};
}

/// Read one optional field of a JSON object into `value`. Return false if the field exists with a different type.
template <typename T>
[[nodiscard]] bool ReadField(const nlohmann::json& object, const char* name, T& value)
{
	const auto field = object.find(name);
	if (field == object.end())
	{
		return true;
	}
	if constexpr (std::is_same_v<T, bool>)
	{
		if (!field->is_boolean())
		{
			return false;
		}
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		if (!field->is_string())
		{
			return false;
		}
	}
	else
	{
		if (!field->is_number())
		{
			return false;
		}
	}
	value = field->template get<T>();
	return true;
}

} // namespace ts4_mod::settings
