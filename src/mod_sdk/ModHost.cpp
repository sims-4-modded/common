/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstring>

#include <algorithm>
#include <exception>
#include <format>
#include <utility>

#include <mod_core/Signature.h>
#include <mod_sdk/ModHost.h>

namespace ts4_mod::sdk
{

namespace
{
void Give(Ts4Text* text, std::string_view value)
{
	if (text != nullptr && text->assign != nullptr)
	{
		text->assign(text->context, value.data(), value.size());
	}
}

[[nodiscard]] std::string Copy(const char* text)
{
	return text == nullptr ? std::string() : std::string(text);
}

[[nodiscard]] settings::FieldValue FromApi(Ts4FieldKind kind, const Ts4FieldValue& value)
{
	switch (kind)
	{
	case TS4_FIELD_TOGGLE:
		return value.toggle != 0;
	case TS4_FIELD_NUMBER:
		return value.number;
	case TS4_FIELD_CHOICE:
		return static_cast<size_t>(value.choice);
	}
	return false;
}

[[nodiscard]] Ts4FieldValue ToApi(const settings::FieldValue& value)
{
	Ts4FieldValue result {};
	if (const auto* toggle = std::get_if<bool>(&value))
	{
		result.toggle = *toggle ? 1u : 0u;
	}
	else if (const auto* number = std::get_if<double>(&value))
	{
		result.number = *number;
	}
	else
	{
		result.choice = static_cast<uint32_t>(std::get<size_t>(value));
	}
	return result;
}

/// Copy the part of the table that the extender gives. A smaller table of an older extender gives 0 for the newer
/// fields, and a larger table of a newer extender gives only the fields that this mod knows.
[[nodiscard]] Ts4HostApi CopyTable(const Ts4HostApi& api)
{
	Ts4HostApi copy {};
	std::memcpy(&copy, &api, std::min<size_t>(api.size, sizeof(Ts4HostApi)));
	copy.size = api.size;
	return copy;
}

/// The text of a string for a C function: a copy with a terminating zero.
[[nodiscard]] std::string Terminated(std::string_view text)
{
	return std::string(text);
}
} // namespace

/// One function that the mod gave, and its name for the log.
struct ModHost::Entry
{
	const ModHost* host = nullptr;
	std::string name;
	Ts4FieldKind kind = TS4_FIELD_TOGGLE;
	std::function<void(const Ts4Call&)> call;
	std::function<void()> task;
	std::function<ts4::Result<void>(const nlohmann::json&)> load;
	FieldHandlers field;
	std::function<bool()> enabled;
	std::function<std::string()> read;
	std::function<nlohmann::json()> status;
	std::function<nlohmann::json(const nlohmann::json&)> query;

	/// Run `work` and write an exception to the log. Return false after an exception.
	template <typename Work>
	bool Guard(Work&& work) noexcept
	{
		try
		{
			work();
			return true;
		}
		catch (const std::exception& error)
		{
			host->Log(TS4_LOG_ERROR, std::format("{}: an exception: {}", name, error.what()));
		}
		catch (...)
		{
			host->Log(TS4_LOG_ERROR, std::format("{}: an unknown exception", name));
		}
		return false;
	}
};

ModHost::ModHost(const Ts4HostApi& api, std::string modName)
    : _api(CopyTable(api))
    , _modName(std::move(modName))
{
	if (auto version = ParseGameVersion(Copy(_api.gameVersion)))
	{
		_version = *version;
	}
}

ModHost::~ModHost() = default;

ts4::Result<void> ModHost::Check(const Ts4HostApi* api, uint32_t apiVersion)
{
	if (api == nullptr)
	{
		return ts4::MakeError("the extender gave no table");
	}
	if (apiVersion != TS4_MOD_API_VERSION || api->apiVersion != TS4_MOD_API_VERSION)
	{
		return ts4::MakeError("the extender uses the interface version {}; this mod uses {}", api->apiVersion,
		                      TS4_MOD_API_VERSION);
	}
	if (api->size < TS4_HOST_API_FIRST_SIZE)
	{
		return ts4::MakeError("the table of the extender has {} bytes; this mod needs {}", api->size, TS4_HOST_API_FIRST_SIZE);
	}
	const bool functions = api->log != nullptr && api->addFunctionCallback != nullptr && api->postTask != nullptr &&
	                       api->addSettings != nullptr && api->saveSettings != nullptr && api->addField != nullptr &&
	                       api->addAction != nullptr && api->addReadout != nullptr && api->setStatusReader != nullptr &&
	                       api->showMessage != nullptr && api->addQuery != nullptr && api->listQueries != nullptr &&
	                       api->callQuery != nullptr;
	const bool texts = api->loaderVersion != nullptr && api->gameVersion != nullptr && api->gameFolder != nullptr &&
	                   api->userFolder != nullptr && api->settingsFolder != nullptr && api->logFolder != nullptr;
	if (!functions || !texts)
	{
		return ts4::MakeError("the table of the extender has a missing function or folder");
	}
	if (auto version = ParseGameVersion(api->gameVersion); !version)
	{
		return version.GetError();
	}
	return {};
}

ts4::Result<std::unique_ptr<ModHost>> ModHost::Create(const Ts4HostApi* api, uint32_t apiVersion, std::string modName)
{
	if (auto checked = Check(api, apiVersion); !checked)
	{
		return checked.GetError();
	}
	return std::make_unique<ModHost>(*api, std::move(modName));
}

std::string ModHost::GameVersionText() const
{
	return Copy(_api.gameVersion);
}

std::string ModHost::LoaderVersion() const
{
	return Copy(_api.loaderVersion);
}

std::string ModHost::GameFolder() const
{
	return Copy(_api.gameFolder);
}

std::string ModHost::UserFolder() const
{
	return Copy(_api.userFolder);
}

std::string ModHost::SettingsFolder() const
{
	return Copy(_api.settingsFolder);
}

std::string ModHost::LogFolder() const
{
	return Copy(_api.logFolder);
}

void ModHost::Log(Ts4LogLevel level, std::string_view text) const
{
	if (_api.log != nullptr)
	{
		const auto line = std::format("{}: {}", _modName, text);
		_api.log(_api.context, level, line.c_str());
	}
}

ModHost::Entry& ModHost::Keep(std::unique_ptr<Entry> entry)
{
	entry->host = this;
	_entries.push_back(std::move(entry));
	return *_entries.back();
}

bool ModHost::AddFunctionCallback(const FunctionRef& function, Ts4HookPhase phase, std::string_view name,
                                  std::function<void(const Ts4Call&)> callback)
{
	if (auto signature = Signature::Parse(function.signature); !signature)
	{
		Log(TS4_LOG_ERROR, std::format("the function '{}': {}", function.name, signature.GetError().message));
		return false;
	}
	if (function.parameterCount > TS4_CALL_MAX_ARGUMENTS)
	{
		Log(TS4_LOG_ERROR, std::format("the function '{}' has {} parameters; a callback gets at most {}", function.name,
		                               function.parameterCount, TS4_CALL_MAX_ARGUMENTS));
		return false;
	}
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("{} {}", function.name, name);
	entry.call = std::move(callback);
	const Ts4FunctionDesc desc {.module = function.module.c_str(),
	                            .name = function.name.c_str(),
	                            .signature = function.signature.c_str(),
	                            .parameterCount = function.parameterCount};
	const auto trampoline = [](void* user, const Ts4Call* call) {
		auto& e = *static_cast<Entry*>(user);
		if (call != nullptr)
		{
			(void)e.Guard([&] { e.call(*call); });
		}
	};
	const auto name0 = Terminated(name);
	return _api.addFunctionCallback(_api.context, &desc, phase, name0.c_str(), trampoline, &entry) != 0;
}

bool ModHost::PostTask(std::function<void()> task)
{
	// The task deletes itself after it runs. A task that the extender never runs stays in memory until the process
	// stops.
	auto entry = std::make_unique<Entry>();
	entry->host = this;
	entry->name = "task";
	entry->task = std::move(task);
	const auto trampoline = [](void* user) {
		std::unique_ptr<Entry> e(static_cast<Entry*>(user));
		(void)e->Guard([&] { e->task(); });
	};
	if (_api.postTask(_api.context, trampoline, entry.get()) == 0)
	{
		return false;
	}
	(void)entry.release();
	return true;
}

bool ModHost::AddSettings(std::string_view section, std::function<ts4::Result<void>(const nlohmann::json&)> load)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("settings '{}'", section);
	entry.load = std::move(load);
	const auto trampoline = [](void* user, const char* json, Ts4Text* error) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		uint32_t accepted = 0;
		const bool ran = e.Guard([&] {
			const auto object = nlohmann::json::parse(json == nullptr ? "{}" : json, nullptr, false);
			if (object.is_discarded() || !object.is_object())
			{
				Give(error, std::format("the object '{}' is not a JSON object", e.name));
				return;
			}
			if (auto loaded = e.load(object); !loaded)
			{
				Give(error, loaded.GetError().message);
				return;
			}
			accepted = 1;
		});
		if (!ran)
		{
			Give(error, "an exception in the mod");
		}
		return accepted;
	};
	const auto section0 = Terminated(section);
	return _api.addSettings(_api.context, section0.c_str(), trampoline, &entry) != 0;
}

bool ModHost::SaveSettings(const nlohmann::json& patch)
{
	const auto text = patch.dump();
	return _api.saveSettings(_api.context, text.c_str()) != 0;
}

bool ModHost::AddField(const Ts4FieldDesc& field, FieldHandlers handlers)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("field {}.{}", Copy(field.section), Copy(field.key));
	entry.kind = field.kind;
	entry.field = std::move(handlers);
	const auto get = [](void* user, Ts4FieldState* state) {
		auto& e = *static_cast<Entry*>(user);
		if (state == nullptr)
		{
			return;
		}
		(void)e.Guard([&] {
			const auto current = e.field.get();
			*state = Ts4FieldState {.value = ToApi(current.value),
			                        .min = current.range.min,
			                        .max = current.range.max,
			                        .restartPending = current.restartPending ? 1u : 0u};
		});
	};
	const auto set = [](void* user, const Ts4FieldValue* value, uint32_t save, Ts4Text* message) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		uint32_t accepted = 0;
		if (value == nullptr)
		{
			return 0;
		}
		const bool ran = e.Guard([&] {
			auto result = e.field.set(FromApi(e.kind, *value), save != 0);
			if (!result)
			{
				Give(message, result.GetError().message);
				return;
			}
			Give(message, *result);
			accepted = 1;
		});
		if (!ran)
		{
			Give(message, "an exception in the mod");
		}
		return accepted;
	};
	return _api.addField(_api.context, &field, get, set, &entry) != 0;
}

bool ModHost::AddAction(const Ts4ActionDesc& action, std::function<void()> run, std::function<bool()> enabled)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("action {}", Copy(action.hotkey != nullptr ? action.hotkey : action.label));
	entry.task = std::move(run);
	entry.enabled = std::move(enabled);
	const auto trampoline = [](void* user) {
		auto& e = *static_cast<Entry*>(user);
		(void)e.Guard([&] { e.task(); });
	};
	const auto isEnabled = [](void* user) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		bool result = true;
		if (e.enabled)
		{
			result = false;
			(void)e.Guard([&] { result = e.enabled(); });
		}
		return result ? 1u : 0u;
	};
	return _api.addAction(_api.context, &action, trampoline, isEnabled, &entry) != 0;
}

bool ModHost::AddReadout(const char* menu, const char* group, std::function<std::string()> read)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("readout {}", Copy(group));
	entry.read = std::move(read);
	const auto trampoline = [](void* user, Ts4Text* text) {
		auto& e = *static_cast<Entry*>(user);
		(void)e.Guard([&] { Give(text, e.read()); });
	};
	return _api.addReadout(_api.context, menu, group, trampoline, &entry) != 0;
}

bool ModHost::SetStatusReader(std::function<nlohmann::json()> read)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = "status";
	entry.status = std::move(read);
	const auto trampoline = [](void* user, Ts4Text* text) {
		auto& e = *static_cast<Entry*>(user);
		(void)e.Guard([&] { Give(text, e.status().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)); });
	};
	return _api.setStatusReader(_api.context, trampoline, &entry) != 0;
}

void ModHost::ShowMessage(std::string_view text, double seconds) const
{
	const auto copy = Terminated(text);
	_api.showMessage(_api.context, copy.c_str(), seconds);
}

bool ModHost::AddQuery(std::string_view name, std::string_view help,
                       std::function<nlohmann::json(const nlohmann::json& params)> query)
{
	auto& entry = Keep(std::make_unique<Entry>());
	entry.name = std::format("query {}", name);
	entry.query = std::move(query);
	const auto trampoline = [](void* user, const char* params, Ts4Text* result) -> uint32_t {
		auto& e = *static_cast<Entry*>(user);
		// An exception is the normal error of a query, so the error goes to the inspector and not to the log.
		try
		{
			auto object = params == nullptr ? nlohmann::json::object() : nlohmann::json::parse(params, nullptr, false);
			if (object.is_null())
			{
				object = nlohmann::json::object();
			}
			if (object.is_discarded() || !object.is_object())
			{
				Give(result, "the parameters are not a JSON object");
				return 0;
			}
			const auto answer = e.query(object);
			Give(result, answer.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
			return 1;
		}
		catch (const std::exception& error)
		{
			Give(result, error.what());
		}
		catch (...)
		{
			Give(result, "an unknown exception in the query");
		}
		return 0;
	};
	const auto name0 = Terminated(name);
	const auto help0 = Terminated(help);
	return _api.addQuery(_api.context, name0.c_str(), help0.c_str(), trampoline, &entry) != 0;
}

ts4::Result<nlohmann::json> ModHost::ListQueries() const
{
	std::string text;
	Ts4Text target {.context = &text, .assign = [](void* context, const char* utf8, size_t size) {
		                static_cast<std::string*>(context)->assign(utf8, size);
	                }};
	_api.listQueries(_api.context, &target);
	auto list = nlohmann::json::parse(text, nullptr, false);
	if (list.is_discarded() || !list.is_array())
	{
		return ts4::MakeError("the list of the queries is not a JSON array");
	}
	return list;
}

ts4::Result<nlohmann::json> ModHost::CallQuery(std::string_view name, const nlohmann::json& params) const
{
	std::string text;
	Ts4Text target {.context = &text, .assign = [](void* context, const char* utf8, size_t size) {
		                static_cast<std::string*>(context)->assign(utf8, size);
	                }};
	const auto name0 = Terminated(name);
	const auto params0 = params.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	if (_api.callQuery(_api.context, name0.c_str(), params0.c_str(), &target) == 0)
	{
		return ts4::MakeError("{}", text.empty() ? std::string("the query failed") : text);
	}
	auto answer = nlohmann::json::parse(text, nullptr, false);
	if (answer.is_discarded())
	{
		return ts4::MakeError("the answer of the query '{}' is not JSON", name);
	}
	return answer;
}

} // namespace ts4_mod::sdk
