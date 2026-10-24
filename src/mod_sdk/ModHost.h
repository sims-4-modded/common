/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <common/Result.h>
#include <mod_api/Ts4ModApi.h>
#include <mod_core/GameVersion.h>
#include <mod_core/SettingsFields.h>
#include <nlohmann/json.hpp>

namespace ts4_mod::sdk
{

/// A native function for `ModHost::AddFunctionCallback`.
struct FunctionRef
{
	std::string module; ///< TS4_MODULE_GAME or TS4_MODULE_SIMULATION.
	std::string name;   ///< The name of the function in the log.
	std::string signature;
	uint32_t parameterCount = 0;
};

/// The state of a menu field for the extender.
struct FieldState
{
	settings::FieldValue value {false};
	settings::FieldRange range {};
	bool restartPending = false;
};

/// What a menu field does: `get` gives its state; `set` changes it and gives a message for the log, or an error.
struct FieldHandlers
{
	std::function<FieldState()> get;
	std::function<ts4::Result<std::string>(const settings::FieldValue& value, bool save)> set;
};

/// The C++ side of the table of the extender (`Ts4HostApi`) in a DLL mod. It keeps each C++ function that the mod
/// gives, and gives the extender a plain function and a pointer to the kept function. No exception goes to the
/// extender: a function that throws writes the error to the log.
///
/// The mod makes one `ModHost` in `Ts4ModInit` and keeps it until `Ts4ModShutdown` returns.
class ModHost
{
public:
	/// Use the table `api`. Call `Check` before.
	explicit ModHost(const Ts4HostApi& api, std::string modName);
	~ModHost();
	ModHost(const ModHost&) = delete;
	ModHost& operator=(const ModHost&) = delete;

	/// Return an error if the extender uses another interface version, if its table is smaller than the table of the
	/// first extender of this version, if a function or a folder of that table is missing, or if the game version is
	/// not correct. A function after `TS4_HOST_API_FIRST_SIZE` can be missing: check it with `TS4_HOST_API_HAS`.
	[[nodiscard]] static ts4::Result<void> Check(const Ts4HostApi* api, uint32_t apiVersion);

	/// Check the table, then make the host.
	[[nodiscard]] static ts4::Result<std::unique_ptr<ModHost>> Create(const Ts4HostApi* api, uint32_t apiVersion,
	                                                                  std::string modName);

	/// The table of the extender. The fields after the size of the table of the extender are 0.
	[[nodiscard]] const Ts4HostApi& Api() const { return _api; }
	[[nodiscard]] const GameVersion& Version() const { return _version; }
	[[nodiscard]] std::string GameVersionText() const;
	[[nodiscard]] std::string LoaderVersion() const;
	[[nodiscard]] std::string GameFolder() const;
	[[nodiscard]] std::string UserFolder() const;
	[[nodiscard]] std::string SettingsFolder() const;
	[[nodiscard]] std::string LogFolder() const;

	/// Write a line to the log of the extender, after the name of the mod.
	void Log(Ts4LogLevel level, std::string_view text) const;

	template <typename... Args>
	void Info(std::format_string<Args...> format, Args&&... args) const
	{
		Log(TS4_LOG_INFO, std::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Warn(std::format_string<Args...> format, Args&&... args) const
	{
		Log(TS4_LOG_WARN, std::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void Error(std::format_string<Args...> format, Args&&... args) const
	{
		Log(TS4_LOG_ERROR, std::format(format, std::forward<Args>(args)...));
	}

	/// Ask the extender to call `callback` before or after each call of a native function. The mod checks the
	/// signature text and the number of parameters before it asks; a bad value gives a log line and false.
	[[nodiscard]] bool AddFunctionCallback(const FunctionRef& function, Ts4HookPhase phase, std::string_view name,
	                                       std::function<void(const Ts4Call&)> callback);
	/// Run `task` one time on the main thread, between two ticks.
	bool PostTask(std::function<void()> task);
	/// Read the JSON object `section` of mods.json now and after each change of the file.
	[[nodiscard]] bool AddSettings(std::string_view section, std::function<ts4::Result<void>(const nlohmann::json&)> load);
	/// Write a change of mods.json (a JSON merge patch).
	bool SaveSettings(const nlohmann::json& patch);
	[[nodiscard]] bool AddField(const Ts4FieldDesc& field, FieldHandlers handlers);
	[[nodiscard]] bool AddAction(const Ts4ActionDesc& action, std::function<void()> run, std::function<bool()> enabled = {});
	[[nodiscard]] bool AddReadout(const char* menu, const char* group, std::function<std::string()> read);
	[[nodiscard]] bool SetStatusReader(std::function<nlohmann::json()> read);
	void ShowMessage(std::string_view text, double seconds) const;

	/// Add a query that the inspector can call. `query` gets the JSON object of the parameters and runs on the main
	/// thread. An exception in `query` gives its message as the error of the query. Return false if the extender
	/// refuses the query.
	[[nodiscard]] bool AddQuery(std::string_view name, std::string_view help,
	                            std::function<nlohmann::json(const nlohmann::json& params)> query);
	/// The queries of the extender and of all mods: a JSON array of objects with "name", "help" and "mod". Any thread.
	[[nodiscard]] ts4::Result<nlohmann::json> ListQueries() const;
	/// Call a query of the extender or of a mod. Main thread only.
	[[nodiscard]] ts4::Result<nlohmann::json> CallQuery(std::string_view name, const nlohmann::json& params) const;

private:
	struct Entry;

	Entry& Keep(std::unique_ptr<Entry> entry);

	Ts4HostApi _api {};
	std::string _modName;
	GameVersion _version;
	std::deque<std::unique_ptr<Entry>> _entries;
};

/// Register the fields of a table with the extender. `requested` gives the settings that the user asked for, `live`
/// gives the settings that the mod uses now, `range` gives the range of a number field for the current settings, and
/// `edit` changes one field (with `EditField`) and uses the new settings.
template <typename S>
struct FieldBinding
{
	std::function<const S&()> requested;
	std::function<const S&()> live;
	std::function<settings::FieldRange(const settings::Field<S>&, const S&)> range;
	std::function<ts4::Result<std::string>(const settings::Field<S>&, const settings::FieldValue&, bool save)> edit;
};

/// The texts of the description of a field, which live as long as the description that points to them.
struct FieldTexts
{
	std::string menu;
	std::string group;
	std::string section;
	std::string key;
	std::string label;
	std::string help;
	std::string format;
	std::vector<std::string> choices;
	std::vector<const char*> choicePointers;
};

template <typename S>
[[nodiscard]] FieldTexts MakeFieldTexts(const settings::Field<S>& field, std::string_view menu)
{
	FieldTexts texts {.menu = std::string(menu),
	                  .group = std::string(field.group),
	                  .section = std::string(field.section),
	                  .key = std::string(field.key),
	                  .label = std::string(field.label),
	                  .help = std::string(field.help),
	                  .format = std::string(field.format)};
	for (const auto choice : field.choices)
	{
		texts.choices.emplace_back(choice);
	}
	for (const auto& choice : texts.choices)
	{
		texts.choicePointers.push_back(choice.c_str());
	}
	return texts;
}

[[nodiscard]] inline Ts4FieldKind ToApiKind(settings::FieldKind kind)
{
	switch (kind)
	{
	case settings::FieldKind::Toggle:
		return TS4_FIELD_TOGGLE;
	case settings::FieldKind::Number:
		return TS4_FIELD_NUMBER;
	case settings::FieldKind::Choice:
		return TS4_FIELD_CHOICE;
	}
	return TS4_FIELD_TOGGLE;
}

/// Register each field of `fields` in the menu `menu` (null for the menu of the mod). Return the count of the fields
/// that the extender refused.
template <typename S>
size_t BindFields(ModHost& host, std::span<const settings::Field<S>> fields, const char* menu, FieldBinding<S> binding)
{
	size_t refused = 0;
	auto shared = std::make_shared<FieldBinding<S>>(std::move(binding));
	for (const auto& field : fields)
	{
		const auto texts = MakeFieldTexts(field, menu == nullptr ? "" : menu);
		const Ts4FieldDesc desc {.menu = menu == nullptr ? nullptr : texts.menu.c_str(),
		                         .group = texts.group.c_str(),
		                         .section = texts.section.c_str(),
		                         .key = texts.key.c_str(),
		                         .label = texts.label.c_str(),
		                         .help = texts.help.c_str(),
		                         .kind = ToApiKind(field.kind),
		                         .min = field.min,
		                         .max = field.max,
		                         .step = field.step,
		                         .format = texts.format.c_str(),
		                         .logarithmic = field.logarithmic ? 1u : 0u,
		                         .restartOnly = field.restartOnly ? 1u : 0u,
		                         .choices = texts.choicePointers.data(),
		                         .choiceCount = static_cast<uint32_t>(texts.choicePointers.size())};
		const auto* pointer = &field;
		FieldHandlers handlers {
		    .get =
		        [shared, pointer] {
			        const auto& requested = shared->requested();
			        return FieldState {.value = settings::ValueOf(*pointer, requested),
			                           .range = shared->range(*pointer, requested),
			                           .restartPending =
			                               pointer->restartOnly && settings::ValueOf(*pointer, requested) !=
			                                                           settings::ValueOf(*pointer, shared->live())};
		        },
		    .set = [shared, pointer](const settings::FieldValue& value,
		                             bool save) { return shared->edit(*pointer, value, save); },
		};
		if (!host.AddField(desc, std::move(handlers)))
		{
			++refused;
		}
	}
	return refused;
}

} // namespace ts4_mod::sdk
