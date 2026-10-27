/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstddef>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <mod_sdk/HostLogSink.h>
#include <mod_sdk/ModHost.h>

using namespace ts4_mod;

namespace
{

/// An extender for the tests: it keeps each registration and lets the test call it.
struct FakeExtender
{
	struct Callback
	{
		std::string name;
		std::string module;
		std::string signature;
		uint32_t parameterCount;
		Ts4HookPhase phase;
		Ts4CallFn call;
		void* user;
	};
	struct Settings
	{
		std::string section;
		Ts4SettingsFn load;
		void* user;
	};
	struct FieldEntry
	{
		std::string key;
		Ts4FieldKind kind;
		Ts4FieldGetFn get;
		Ts4FieldSetFn set;
		void* user;
	};
	struct Query
	{
		std::string name;
		Ts4QueryFn query;
		void* user;
	};

	std::vector<std::pair<Ts4LogLevel, std::string>> log;
	std::vector<Callback> callbacks;
	std::vector<std::pair<Ts4TaskFn, void*>> tasks;
	std::vector<Settings> settings;
	std::vector<std::string> saved;
	std::vector<FieldEntry> fields;
	std::vector<Query> queries;
	std::vector<std::string> messages;
	bool refuse = false;

	[[nodiscard]] Ts4HostApi Api()
	{
		Ts4HostApi api {};
		api.apiVersion = TS4_MOD_API_VERSION;
		api.size = sizeof(Ts4HostApi);
		api.context = this;
		api.loaderVersion = "0.1.0";
		api.gameVersion = "1.128.90.1030";
		api.gameFolder = "F:/Game";
		api.userFolder = "C:/User";
		api.settingsFolder = "C:/Local/Sims4Mods";
		api.logFolder = "C:/Local/Sims4Mods/logs";
		api.log = [](void* c, Ts4LogLevel level, const char* text) {
			static_cast<FakeExtender*>(c)->log.emplace_back(level, text);
		};
		api.addFunctionCallback = [](void* c, const Ts4FunctionDesc* function, Ts4HookPhase phase, const char* name,
		                             Ts4CallFn call, void* user) -> uint32_t {
			auto& self = *static_cast<FakeExtender*>(c);
			if (self.refuse)
			{
				return 0;
			}
			self.callbacks.push_back({.name = std::string(function->name) + "/" + name,
			                          .module = function->module,
			                          .signature = function->signature,
			                          .parameterCount = function->parameterCount,
			                          .phase = phase,
			                          .call = call,
			                          .user = user});
			return 1;
		};
		api.postTask = [](void* c, Ts4TaskFn task, void* user) -> uint32_t {
			static_cast<FakeExtender*>(c)->tasks.emplace_back(task, user);
			return 1;
		};
		api.addSettings = [](void* c, const char* section, Ts4SettingsFn load, void* user) -> uint32_t {
			static_cast<FakeExtender*>(c)->settings.push_back({.section = section, .load = load, .user = user});
			return 1;
		};
		api.saveSettings = [](void* c, const char* patch) -> uint32_t {
			static_cast<FakeExtender*>(c)->saved.emplace_back(patch);
			return 1;
		};
		api.addField = [](void* c, const Ts4FieldDesc* field, Ts4FieldGetFn get, Ts4FieldSetFn set, void* user) -> uint32_t {
			static_cast<FakeExtender*>(c)->fields.push_back(
			    {.key = field->key, .kind = field->kind, .get = get, .set = set, .user = user});
			return 1;
		};
		api.addAction = [](void*, const Ts4ActionDesc*, Ts4TaskFn, Ts4EnabledFn, void*) -> uint32_t { return 1; };
		api.addReadout = [](void*, const char*, const char*, Ts4ReadFn, void*) -> uint32_t { return 1; };
		api.setStatusReader = [](void*, Ts4ReadFn, void*) -> uint32_t { return 1; };
		api.showMessage = [](void* c, const char* text, double /*seconds*/) {
			static_cast<FakeExtender*>(c)->messages.emplace_back(text);
		};
		api.addQuery = [](void* c, const char* name, const char* /*help*/, Ts4QueryFn query, void* user) -> uint32_t {
			static_cast<FakeExtender*>(c)->queries.push_back({.name = name, .query = query, .user = user});
			return 1;
		};
		api.listQueries = [](void* c, Ts4Text* list) {
			auto names = nlohmann::json::array();
			for (const auto& query : static_cast<FakeExtender*>(c)->queries)
			{
				names.push_back({{"name", query.name}});
			}
			const auto text = names.dump();
			list->assign(list->context, text.data(), text.size());
		};
		api.callQuery = [](void* c, const char* name, const char* params, Ts4Text* result) -> uint32_t {
			for (const auto& query : static_cast<FakeExtender*>(c)->queries)
			{
				if (query.name == name)
				{
					return query.query(query.user, params, result);
				}
			}
			const std::string error = "the query is not known";
			result->assign(result->context, error.data(), error.size());
			return 0;
		};
		return api;
	}
};

void AssignTo(void* context, const char* text, size_t size)
{
	static_cast<std::string*>(context)->assign(text, size);
}

struct Sample
{
	bool on = false;
	double speed = 1.0;
};

const std::array<settings::Field<Sample>, 2> k_SampleFields {
    settings::Field<Sample> {.group = "Main",
                             .section = "sample",
                             .key = "on",
                             .label = "On",
                             .getToggle = [](const Sample& s) { return s.on; },
                             .setToggle = [](Sample& s, bool v) { s.on = v; }},
    settings::Field<Sample> {.group = "Main",
                             .section = "sample",
                             .key = "speed",
                             .label = "Speed",
                             .kind = settings::FieldKind::Number,
                             .min = 0.0,
                             .max = 4.0,
                             .restartOnly = true,
                             .getNumber = [](const Sample& s) { return s.speed; },
                             .setNumber = [](Sample& s, double v) { s.speed = v; }},
};

const sdk::FunctionRef k_ZoneLoad {
    .module = TS4_MODULE_SIMULATION, .name = "ZoneLoad", .signature = "48 89 5C 24 ?? 57", .parameterCount = 2};

} // namespace

TEST(ModHost, AcceptsACorrectTable)
{
	FakeExtender extender;
	const auto api = extender.Api();
	const auto host = sdk::ModHost::Create(&api, TS4_MOD_API_VERSION, "test");
	ASSERT_TRUE(host) << host.GetError().message;
	EXPECT_EQ((*host)->Version().minor, 128u);
	EXPECT_EQ((*host)->GameVersionText(), "1.128.90.1030");
	EXPECT_EQ((*host)->UserFolder(), "C:/User");
	EXPECT_EQ((*host)->SettingsFolder(), "C:/Local/Sims4Mods");

	(*host)->Info("the mod starts on {}", "1.128.90.1030");
	ASSERT_EQ(extender.log.size(), 1u);
	EXPECT_EQ(extender.log[0].first, TS4_LOG_INFO);
	EXPECT_EQ(extender.log[0].second, "test: the mod starts on 1.128.90.1030");
}

TEST(ModHost, RefusesATableOfAnotherVersion)
{
	FakeExtender extender;
	auto api = extender.Api();
	EXPECT_TRUE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
	EXPECT_FALSE(sdk::ModHost::Check(nullptr, TS4_MOD_API_VERSION));
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION + 1));
	api.apiVersion = TS4_MOD_API_VERSION + 1;
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
}

TEST(ModHost, RefusesASmallTable)
{
	FakeExtender extender;
	auto api = extender.Api();
	api.size = static_cast<uint32_t>(TS4_HOST_API_FIRST_SIZE - 1);
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
	api.size = 8;
	EXPECT_FALSE(sdk::ModHost::Create(&api, TS4_MOD_API_VERSION, "test"));
}

TEST(ModHost, TheFirstTableHasEachFunctionOfVersionOne)
{
	FakeExtender extender;
	auto api = extender.Api();
	api.size = static_cast<uint32_t>(TS4_HOST_API_FIRST_SIZE);
	EXPECT_TRUE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
	EXPECT_EQ(TS4_HOST_API_HAS(&api, callQuery), 1);
	EXPECT_EQ(TS4_HOST_API_HAS(&api, addFunctionCallback), 1);
	api.size = static_cast<uint32_t>(offsetof(Ts4HostApi, callQuery));
	EXPECT_EQ(TS4_HOST_API_HAS(&api, callQuery), 0);
}

TEST(ModHost, AcceptsALargerTableOfANewerExtender)
{
	FakeExtender extender;
	// A newer extender adds functions after the fields that this mod knows.
	struct NewerTable
	{
		Ts4HostApi api;
		void* newerFunction;
	} newer {.api = extender.Api(), .newerFunction = nullptr};
	newer.api.size = sizeof(NewerTable);
	const auto host = sdk::ModHost::Create(&newer.api, TS4_MOD_API_VERSION, "test");
	ASSERT_TRUE(host) << host.GetError().message;
	EXPECT_EQ((*host)->Api().size, sizeof(NewerTable));
	(*host)->ShowMessage("hello", 2.0);
	ASSERT_EQ(extender.messages.size(), 1u);
}

TEST(ModHost, RefusesAMissingFunctionOrFolder)
{
	FakeExtender extender;
	auto api = extender.Api();
	api.log = nullptr;
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
	api = extender.Api();
	api.callQuery = nullptr;
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
	api = extender.Api();
	api.userFolder = nullptr;
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
}

TEST(ModHost, RefusesABadGameVersion)
{
	FakeExtender extender;
	auto api = extender.Api();
	api.gameVersion = "unknown";
	EXPECT_FALSE(sdk::ModHost::Check(&api, TS4_MOD_API_VERSION));
}

TEST(ModHost, GivesAQueryItsParametersAndItsAnswerOrError)
{
	FakeExtender extender;
	sdk::ModHost host(extender.Api(), "test");
	ASSERT_TRUE(host.AddQuery("test.double", "Doubles a value.", [](const nlohmann::json& params) {
		if (!params.contains("value"))
		{
			throw std::runtime_error("no value");
		}
		return nlohmann::json {{"double", params.at("value").get<int>() * 2}};
	}));
	ASSERT_EQ(extender.queries.size(), 1u);
	auto& query = extender.queries[0];
	std::string answer;
	Ts4Text text {.context = &answer, .assign = AssignTo};
	EXPECT_EQ(query.query(query.user, R"({"value": 4})", &text), 1u);
	EXPECT_EQ(answer, R"({"double":8})");
	EXPECT_EQ(query.query(query.user, "{}", &text), 0u);
	EXPECT_EQ(answer, "no value");
	EXPECT_EQ(query.query(query.user, "[1]", &text), 0u);
	EXPECT_EQ(query.query(query.user, nullptr, &text), 0u);
	EXPECT_EQ(answer, "no value");
	// An exception of a query is its error, not an error of the mod.
	EXPECT_TRUE(extender.log.empty());

	const auto list = host.ListQueries();
	ASSERT_TRUE(list);
	EXPECT_EQ(list->at(0).at("name"), "test.double");
	const auto called = host.CallQuery("test.double", {{"value", 5}});
	ASSERT_TRUE(called);
	EXPECT_EQ(called->at("double"), 10);
	const auto failed = host.CallQuery("test.other", nlohmann::json::object());
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.GetError().message, "the query is not known");
}

TEST(ModHost, GivesTheCallOfAFunctionToTheCallbackAndCatchesAnException)
{
	FakeExtender extender;
	sdk::ModHost host(extender.Api(), "test");
	uintptr_t seen = 0;
	ASSERT_TRUE(host.AddFunctionCallback(k_ZoneLoad, TS4_HOOK_AFTER, "a",
	                                     [&](const Ts4Call& call) { seen = call.arguments[1] + call.result; }));
	ASSERT_TRUE(host.AddFunctionCallback(k_ZoneLoad, TS4_HOOK_BEFORE, "b",
	                                     [](const Ts4Call& /*call*/) { throw std::runtime_error("bad zone"); }));
	ASSERT_EQ(extender.callbacks.size(), 2u);
	EXPECT_EQ(extender.callbacks[0].name, "ZoneLoad/a");
	EXPECT_EQ(extender.callbacks[0].module, "Simulation_x64.dll");
	EXPECT_EQ(extender.callbacks[0].signature, "48 89 5C 24 ?? 57");
	EXPECT_EQ(extender.callbacks[0].parameterCount, 2u);
	EXPECT_EQ(extender.callbacks[0].phase, TS4_HOOK_AFTER);
	EXPECT_EQ(extender.callbacks[1].phase, TS4_HOOK_BEFORE);
	const Ts4Call call {.arguments = {7, 40, 0, 0}, .result = 2};
	for (const auto& callback : extender.callbacks)
	{
		callback.call(callback.user, &call);
		callback.call(callback.user, nullptr);
	}
	EXPECT_EQ(seen, 42u);
	ASSERT_EQ(extender.log.size(), 1u);
	EXPECT_EQ(extender.log[0].first, TS4_LOG_ERROR);
	EXPECT_NE(extender.log[0].second.find("bad zone"), std::string::npos);
	extender.refuse = true;
	EXPECT_FALSE(host.AddFunctionCallback(k_ZoneLoad, TS4_HOOK_BEFORE, "c", [](const Ts4Call& /*call*/) {}));
}

TEST(ModHost, RefusesABadSignatureBeforeItAsksTheExtender)
{
	FakeExtender extender;
	sdk::ModHost host(extender.Api(), "test");
	auto bad = k_ZoneLoad;
	bad.signature = "48 8";
	EXPECT_FALSE(host.AddFunctionCallback(bad, TS4_HOOK_AFTER, "a", [](const Ts4Call& /*call*/) {}));
	bad = k_ZoneLoad;
	bad.parameterCount = TS4_CALL_MAX_ARGUMENTS + 1;
	EXPECT_FALSE(host.AddFunctionCallback(bad, TS4_HOOK_AFTER, "a", [](const Ts4Call& /*call*/) {}));
	EXPECT_TRUE(extender.callbacks.empty());
	ASSERT_EQ(extender.log.size(), 2u);
	EXPECT_NE(extender.log[0].second.find("ZoneLoad"), std::string::npos);
}

TEST(ModHost, RunsATaskOneTime)
{
	FakeExtender extender;
	sdk::ModHost host(extender.Api(), "test");
	int runs = 0;
	ASSERT_TRUE(host.PostTask([&] { ++runs; }));
	ASSERT_EQ(extender.tasks.size(), 1u);
	extender.tasks[0].first(extender.tasks[0].second);
	EXPECT_EQ(runs, 1);
}

TEST(ModHost, GivesTheSettingsAndReturnsTheError)
{
	FakeExtender extender;
	sdk::ModHost host(extender.Api(), "test");
	double value = 0.0;
	ASSERT_TRUE(host.AddSettings("sample", [&](const nlohmann::json& object) -> ts4::Result<void> {
		if (object.contains("bad"))
		{
			return ts4::MakeError("bad value");
		}
		value = object.value("speed", 1.0);
		return {};
	}));
	ASSERT_EQ(extender.settings.size(), 1u);
	auto& entry = extender.settings[0];
	EXPECT_EQ(entry.section, "sample");
	std::string error;
	Ts4Text text {.context = &error, .assign = AssignTo};
	EXPECT_EQ(entry.load(entry.user, R"({"speed": 3})", &text), 1u);
	EXPECT_DOUBLE_EQ(value, 3.0);
	EXPECT_EQ(entry.load(entry.user, R"({"bad": 1})", &text), 0u);
	EXPECT_EQ(error, "bad value");
	EXPECT_EQ(entry.load(entry.user, "[1]", &text), 0u);
	EXPECT_TRUE(host.SaveSettings({{"sample", {{"speed", 2}}}}));
	EXPECT_EQ(extender.saved.back(), R"({"sample":{"speed":2}})");
}

TEST(ModHost, BindsTheFieldsOfATable)
{
	FakeExtender extender;
	sdk::ModHost host(extender.Api(), "test");
	Sample requested;
	const Sample live;
	const auto refused = sdk::BindFields<Sample>(
	    host, k_SampleFields, nullptr,
	    {.requested = [&]() -> const Sample& { return requested; },
	     .live = [&]() -> const Sample& { return live; },
	     .range = [](const settings::Field<Sample>& f,
	                 const Sample& /*settings*/) { return settings::FieldRange {.min = f.min, .max = f.max}; },
	     .edit = [&](const settings::Field<Sample>& f, const settings::FieldValue& value,
	                 bool /*save*/) -> ts4::Result<std::string> {
		     auto edit = settings::EditField(requested, f, value, settings::FieldRange {.min = f.min, .max = f.max},
		                                     [](const Sample& /*next*/) -> ts4::Result<void> { return {}; });
		     if (!edit)
		     {
			     return edit.GetError();
		     }
		     return edit->message;
	     }});
	EXPECT_EQ(refused, 0u);
	ASSERT_EQ(extender.fields.size(), 2u);
	auto& speed = extender.fields[1];
	EXPECT_EQ(speed.kind, TS4_FIELD_NUMBER);
	std::string message;
	Ts4Text text {.context = &message, .assign = AssignTo};
	Ts4FieldValue value {};
	value.number = 9.0;
	EXPECT_EQ(speed.set(speed.user, &value, 1, &text), 1u);
	EXPECT_EQ(message, "Speed: 4.00");
	Ts4FieldState state {};
	speed.get(speed.user, &state);
	EXPECT_DOUBLE_EQ(state.value.number, 4.0);
	EXPECT_DOUBLE_EQ(state.max, 4.0);
	EXPECT_EQ(state.restartPending, 1u);
}

TEST(HostLogSink, GivesEachLineToTheExtenderUntilItStops)
{
	FakeExtender extender;
	const auto api = extender.Api();
	auto sink = std::make_shared<sdk::HostLogSink>(api, "test");
	spdlog::logger logger("test", sink);
	logger.set_level(spdlog::level::debug);
	logger.warn("the zone has {} Sims", 8);
	logger.debug("a detail");
	ASSERT_EQ(extender.log.size(), 2u);
	EXPECT_EQ(extender.log[0].first, TS4_LOG_WARN);
	EXPECT_EQ(extender.log[0].second, "test: the zone has 8 Sims");
	EXPECT_EQ(extender.log[1].first, TS4_LOG_DEBUG);
	sink->Detach();
	logger.error("after the shutdown");
	EXPECT_EQ(extender.log.size(), 2u);
}
