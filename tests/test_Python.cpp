/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>
#include <mod_game/Python.h>
#include <windows.h>

using namespace ts4_mod;

namespace
{

// The fake exports of the tests that do not need an interpreter. Only the reference functions run.
struct FakeObject
{
	int references = 1;
};

void FakeIncRef(PyObject* object)
{
	++reinterpret_cast<FakeObject*>(object)->references;
}

void FakeDecRef(PyObject* object)
{
	--reinterpret_cast<FakeObject*>(object)->references;
}

void FakeExport() {}

/// The names of the core exports. A fake interpreter that has only these exports has all groups off.
constexpr std::string_view k_CoreExports[] = {
    "Py_IsInitialized",
    "PyGILState_Ensure",
    "PyGILState_Release",
    "Py_IncRef",
    "Py_DecRef",
    "PyErr_Occurred",
    "PyErr_Fetch",
    "PyErr_NormalizeException",
    "PyErr_Clear",
    "PyObject_Str",
    "PyObject_GetAttrString",
    "PyUnicode_AsUTF8AndSize",
};

[[nodiscard]] void* FakeResolve(const char* name)
{
	const std::string_view text(name);
	if (text == "Py_IncRef")
	{
		return reinterpret_cast<void*>(&FakeIncRef);
	}
	if (text == "Py_DecRef")
	{
		return reinterpret_cast<void*>(&FakeDecRef);
	}
	for (const auto core : k_CoreExports)
	{
		if (text == core)
		{
			return reinterpret_cast<void*>(&FakeExport);
		}
	}
	return nullptr;
}

/// The interpreter of the tests: the Python 3.7 of the developer in the folder that the environment variable
/// `TS4_PYTHON37` gives. It has the same version as the interpreter of the game. The tests never load a file from the
/// game folder. If the variable is not set, or the folder has no interpreter, the tests of this suite are skipped.
[[nodiscard]] std::filesystem::path PythonHome()
{
	const auto size = GetEnvironmentVariableW(L"TS4_PYTHON37", nullptr, 0);
	if (size == 0)
	{
		return {};
	}
	std::wstring value(size, L'\0');
	const auto written = GetEnvironmentVariableW(L"TS4_PYTHON37", value.data(), size);
	value.resize(written);
	return value;
}

class PythonInterpreter: public ::testing::Test
{
protected:
	static void SetUpTestSuite()
	{
		const auto home = PythonHome();
		if (home.empty())
		{
			s_reason = "the environment variable TS4_PYTHON37 does not give the folder of Python 3.7";
			return;
		}
		const auto dll = home / L"python37.dll";
		if (!std::filesystem::exists(dll))
		{
			s_reason = dll.string() + " is not there";
			return;
		}
		// The DLL of the interpreter loads its C runtime from its own folder.
		s_module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (s_module == nullptr)
		{
			s_reason = "python37.dll did not load";
			return;
		}
		const auto getVersion = reinterpret_cast<const char* (*)()>(GetProcAddress(s_module, "Py_GetVersion"));
		const auto setHome = reinterpret_cast<void (*)(const wchar_t*)>(GetProcAddress(s_module, "Py_SetPythonHome"));
		const auto initialize = reinterpret_cast<void (*)(int)>(GetProcAddress(s_module, "Py_InitializeEx"));
		const auto saveThread = reinterpret_cast<void* (*)()>(GetProcAddress(s_module, "PyEval_SaveThread"));
		auto* noSite = reinterpret_cast<int*>(GetProcAddress(s_module, "Py_NoSiteFlag"));
		if (getVersion == nullptr || setHome == nullptr || initialize == nullptr || saveThread == nullptr || noSite == nullptr)
		{
			s_reason = "python37.dll does not export the functions of the start";
			return;
		}
		if (!std::string_view(getVersion()).starts_with("3.7."))
		{
			s_reason = std::string("the interpreter is not Python 3.7: ") + getVersion();
			return;
		}
		auto api = PythonApi::FromModule(s_module, "python37.dll");
		if (!api)
		{
			s_reason = api.GetError().message;
			return;
		}
		// The interpreter of the tests does not import site, so the packages of the developer do not change it.
		*noSite = 1;
		s_home = home.wstring();
		setHome(s_home.c_str());
		initialize(0);
		// The main thread gives the GIL back, so that each test gets it with a GilGuard.
		s_threadState = saveThread();
		s_api.emplace(std::move(*api));
	}

	static void TearDownTestSuite()
	{
		if (!s_api)
		{
			return;
		}
		const auto restoreThread = reinterpret_cast<void (*)(void*)>(GetProcAddress(s_module, "PyEval_RestoreThread"));
		const auto finalize = reinterpret_cast<int (*)()>(GetProcAddress(s_module, "Py_FinalizeEx"));
		s_api.reset();
		if (restoreThread != nullptr && finalize != nullptr)
		{
			restoreThread(s_threadState);
			(void)finalize();
		}
	}

	void SetUp() override
	{
		if (!s_api)
		{
			GTEST_SKIP() << s_reason;
		}
	}

	[[nodiscard]] static const PythonApi& Api() { return *s_api; }

	static inline HMODULE s_module = nullptr;
	static inline void* s_threadState = nullptr;
	static inline std::optional<PythonApi> s_api;
	static inline std::string s_reason;
	/// Py_SetPythonHome keeps the pointer, so the text must live as long as the interpreter.
	static inline std::wstring s_home;
};

} // namespace

TEST(PythonApi, RefusesAnInterpreterThatIsNotLoaded)
{
	const auto api = PythonApi::Find();
	ASSERT_FALSE(api);
	EXPECT_EQ(api.GetError().message, "python37_x64.dll is not loaded");
}

TEST(PythonApi, NamesTheMissingCoreExports)
{
	const auto api = PythonApi::FromModule(GetModuleHandleW(L"kernel32.dll"), "kernel32.dll");
	ASSERT_FALSE(api);
	EXPECT_NE(api.GetError().message.find("kernel32.dll does not export: Py_IsInitialized, PyGILState_Ensure"),
	          std::string::npos)
	    << api.GetError().message;
}

TEST(PythonApi, AMissingExportTurnsOffOnlyItsGroup)
{
	const auto api = PythonApi::Resolve("fake.dll", &FakeResolve);
	ASSERT_TRUE(api) << api.GetError().message;
	EXPECT_FALSE(api->Has(PythonFeature::Eval));
	EXPECT_FALSE(api->Has(PythonFeature::Import));
	EXPECT_FALSE(api->Has(PythonFeature::Values));
	EXPECT_EQ(api->Missing(PythonFeature::Eval), "PyRun_StringFlags, PyDict_New, PyDict_SetItemString, PyEval_GetBuiltins");

	// The functions of a group that is off return an error and do not call the interpreter.
	const auto eval = api->Eval("1");
	ASSERT_FALSE(eval);
	EXPECT_EQ(eval.GetError().message,
	          "eval is off: fake.dll does not export PyRun_StringFlags, PyDict_New, PyDict_SetItemString, PyEval_GetBuiltins");
	EXPECT_FALSE(api->Import("math"));
	EXPECT_FALSE(api->FromInteger(1));
	EXPECT_FALSE(api->ToInteger(nullptr));
}

TEST(PythonApi, AReferenceCountsItsCopies)
{
	const auto api = PythonApi::Resolve("fake.dll", &FakeResolve);
	ASSERT_TRUE(api) << api.GetError().message;
	FakeObject object;
	auto* python = reinterpret_cast<PyObject*>(&object);
	{
		auto first = PyRef::Steal(*api, python);
		EXPECT_EQ(object.references, 1);
		{
			const auto copy = first;
			EXPECT_EQ(object.references, 2);
			auto assigned = PyRef::Borrow(*api, python);
			EXPECT_EQ(object.references, 3);
			assigned = copy;
			EXPECT_EQ(object.references, 3);
		}
		EXPECT_EQ(object.references, 1);
		auto moved = std::move(first);
		EXPECT_EQ(object.references, 1);
		EXPECT_FALSE(first);
		EXPECT_EQ(moved.Get(), python);
		auto* released = moved.Release();
		EXPECT_EQ(released, python);
		EXPECT_EQ(object.references, 1);
		auto again = PyRef::Steal(*api, released);
		again.Reset();
		EXPECT_EQ(object.references, 0);
		EXPECT_FALSE(again);
	}
	EXPECT_EQ(object.references, 0);
	EXPECT_FALSE(PyRef::Borrow(*api, nullptr));
}

TEST_F(PythonInterpreter, EvaluatesAnExpression)
{
	const GilGuard gil(Api());
	EXPECT_TRUE(Api().Has(PythonFeature::Eval));
	const auto value = Api().Eval("6 * 7");
	ASSERT_TRUE(value) << value.GetError().message;
	const auto number = Api().ToInteger(value->Get());
	ASSERT_TRUE(number) << number.GetError().message;
	EXPECT_EQ(*number, 42);

	const auto text = Api().Eval("'Sim' + 's ' + str(len([1, 2, 3]))");
	ASSERT_TRUE(text) << text.GetError().message;
	const auto string = Api().Str(text->Get());
	ASSERT_TRUE(string) << string.GetError().message;
	EXPECT_EQ(*string, "Sims 3");
}

TEST_F(PythonInterpreter, MakesValues)
{
	const GilGuard gil(Api());
	const auto number = Api().FromInteger(-9'000'000'000);
	ASSERT_TRUE(number) << number.GetError().message;
	EXPECT_EQ(*Api().ToInteger(number->Get()), -9'000'000'000);
	const auto text = Api().FromText("caf\xC3\xA9");
	ASSERT_TRUE(text) << text.GetError().message;
	EXPECT_EQ(*Api().Str(text->Get()), "caf\xC3\xA9");
	const auto notNumber = Api().ToInteger(text->Get());
	ASSERT_FALSE(notNumber);
	EXPECT_NE(notNumber.GetError().message.find("TypeError"), std::string::npos) << notNumber.GetError().message;
}

TEST_F(PythonInterpreter, TurnsAPythonExceptionIntoAnError)
{
	const GilGuard gil(Api());
	const auto division = Api().Eval("1 / 0");
	ASSERT_FALSE(division);
	EXPECT_EQ(division.GetError().message, "eval: ZeroDivisionError: division by zero");
	EXPECT_EQ(Api().PyErr_Occurred(), nullptr);

	const auto syntax = Api().Eval("1 +");
	ASSERT_FALSE(syntax);
	EXPECT_NE(syntax.GetError().message.find("eval: SyntaxError"), std::string::npos) << syntax.GetError().message;
	EXPECT_EQ(Api().PyErr_Occurred(), nullptr);

	const auto raised = Api().Exec("x = 1\nraise ValueError('bad value %d' % x)\n");
	ASSERT_FALSE(raised);
	EXPECT_EQ(raised.GetError().message, "exec: ValueError: bad value 1");

	const auto missing = Api().Import("ts4_no_such_module");
	ASSERT_FALSE(missing);
	EXPECT_NE(missing.GetError().message.find("import ts4_no_such_module: ModuleNotFoundError"), std::string::npos)
	    << missing.GetError().message;
	EXPECT_EQ(Api().PyErr_Occurred(), nullptr);
}

TEST_F(PythonInterpreter, ImportsAndCalls)
{
	const GilGuard gil(Api());
	const auto os = Api().Import("os");
	ASSERT_TRUE(os) << os.GetError().message;
	const auto getpid = Api().GetAttr(os->Get(), "getpid");
	ASSERT_TRUE(getpid) << getpid.GetError().message;
	const auto pid = Api().Call(getpid->Get());
	ASSERT_TRUE(pid) << pid.GetError().message;
	EXPECT_EQ(*Api().ToInteger(pid->Get()), static_cast<long long>(GetCurrentProcessId()));

	const auto missing = Api().GetAttr(os->Get(), "no_such_attribute");
	ASSERT_FALSE(missing);
	EXPECT_NE(missing.GetError().message.find("AttributeError"), std::string::npos) << missing.GetError().message;
}

TEST_F(PythonInterpreter, AnotherThreadGetsTheGil)
{
	long long value = 0;
	std::thread([&] {
		const GilGuard gil(Api());
		const auto result = Api().Eval("2 ** 10");
		value = result ? Api().ToInteger(result->Get()).Value() : -1;
	}).join();
	EXPECT_EQ(value, 1024);
}
