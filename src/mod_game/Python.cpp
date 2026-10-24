/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "mod_game/Python.h"

#include <format>
#include <utility>

#include <windows.h>

#include "mod_game/WideText.h"

namespace ts4_mod
{

namespace
{
/// The start symbols of the Python 3.7 compiler (`Py_eval_input` and `Py_file_input`).
constexpr int k_PyEvalInput = 258;
constexpr int k_PyFileInput = 257;

void AddName(std::string& list, const char* name)
{
	list += list.empty() ? "" : ", ";
	list += name;
}
} // namespace

PyRef PyRef::Steal(const PythonApi& api, PyObject* object)
{
	return PyRef(api.Py_IncRef, api.Py_DecRef, object);
}

PyRef PyRef::Borrow(const PythonApi& api, PyObject* object)
{
	if (object != nullptr)
	{
		api.Py_IncRef(object);
	}
	return PyRef(api.Py_IncRef, api.Py_DecRef, object);
}

PyRef::PyRef(const PyRef& other)
    : _incRef(other._incRef)
    , _decRef(other._decRef)
    , _object(other._object)
{
	if (_object != nullptr)
	{
		_incRef(_object);
	}
}

PyRef& PyRef::operator=(const PyRef& other)
{
	if (this != &other)
	{
		PyRef copy(other);
		*this = std::move(copy);
	}
	return *this;
}

PyRef::PyRef(PyRef&& other) noexcept
    : _incRef(other._incRef)
    , _decRef(other._decRef)
    , _object(std::exchange(other._object, nullptr))
{
}

PyRef& PyRef::operator=(PyRef&& other) noexcept
{
	if (this != &other)
	{
		Reset();
		_incRef = other._incRef;
		_decRef = other._decRef;
		_object = std::exchange(other._object, nullptr);
	}
	return *this;
}

PyObject* PyRef::Release()
{
	return std::exchange(_object, nullptr);
}

void PyRef::Reset()
{
	if (auto* object = std::exchange(_object, nullptr); object != nullptr)
	{
		_decRef(object);
	}
}

ts4::Result<PythonApi> PythonApi::Find(std::string_view moduleName)
{
	const auto module = GetModuleHandleW(ToWide(moduleName).c_str());
	if (module == nullptr)
	{
		return ts4::MakeError("{} is not loaded", moduleName);
	}
	return FromModule(module, std::string(moduleName));
}

ts4::Result<PythonApi> PythonApi::FromModule(void* module, std::string moduleName)
{
	if (module == nullptr)
	{
		return ts4::MakeError("{} is not loaded", moduleName);
	}
	return Resolve(std::move(moduleName), [module](const char* name) {
		return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
	});
}

ts4::Result<PythonApi> PythonApi::Resolve(std::string moduleName, const std::function<void*(const char* name)>& resolve)
{
	PythonApi api;
	std::string missingCore;
	const auto missingList = [&](std::string_view group) -> std::string& {
		if (group == "Eval")
		{
			return api._missingEval;
		}
		if (group == "Import")
		{
			return api._missingImport;
		}
		if (group == "Values")
		{
			return api._missingValues;
		}
		return missingCore;
	};
#define TS4_PYTHON_LOAD(group, result, name, parameters)               \
	api.name = reinterpret_cast<result(*) parameters>(resolve(#name)); \
	if (api.name == nullptr)                                           \
	{                                                                  \
		AddName(missingList(#group), #name);                           \
	}
	TS4_PYTHON_FUNCTIONS(TS4_PYTHON_LOAD)
#undef TS4_PYTHON_LOAD

	if (!missingCore.empty())
	{
		return ts4::MakeError("{} does not export: {}", moduleName, missingCore);
	}
	api._moduleName = std::move(moduleName);
	return api;
}

bool PythonApi::Has(PythonFeature feature) const
{
	return Missing(feature).empty();
}

const std::string& PythonApi::Missing(PythonFeature feature) const
{
	switch (feature)
	{
	case PythonFeature::Eval:
		return _missingEval;
	case PythonFeature::Import:
		return _missingImport;
	case PythonFeature::Values:
		break;
	}
	return _missingValues;
}

ts4::Result<void> PythonApi::Need(PythonFeature feature, std::string_view what) const
{
	if (const auto& missing = Missing(feature); !missing.empty())
	{
		return ts4::MakeError("{} is off: {} does not export {}", what, _moduleName, missing);
	}
	return {};
}

ts4::Error PythonApi::TakeError(std::string_view what) const
{
	if (PyErr_Occurred() == nullptr)
	{
		return ts4::MakeError("{}: the interpreter gave no result and no exception", what);
	}
	PyObject* rawType = nullptr;
	PyObject* rawValue = nullptr;
	PyObject* rawTraceback = nullptr;
	PyErr_Fetch(&rawType, &rawValue, &rawTraceback);
	PyErr_NormalizeException(&rawType, &rawValue, &rawTraceback);
	const auto type = PyRef::Steal(*this, rawType);
	const auto value = PyRef::Steal(*this, rawValue);
	const auto traceback = PyRef::Steal(*this, rawTraceback);

	std::string typeName = "an exception";
	if (type)
	{
		if (const auto name = PyRef::Steal(*this, PyObject_GetAttrString(type.Get(), "__name__")); name)
		{
			if (auto text = Str(name.Get()); text)
			{
				typeName = std::move(*text);
			}
		}
	}
	std::string valueText;
	if (value)
	{
		auto text = Str(value.Get());
		valueText = text ? std::move(*text) : std::string("the value has no text");
	}
	// A failure in the code above leaves no exception: each step clears its own.
	PyErr_Clear();
	if (valueText.empty())
	{
		return ts4::MakeError("{}: {}", what, typeName);
	}
	return ts4::MakeError("{}: {}: {}", what, typeName, valueText);
}

ts4::Result<std::string> PythonApi::Str(PyObject* object) const
{
	if (object == nullptr)
	{
		return ts4::MakeError("no object");
	}
	const auto text = PyRef::Steal(*this, PyObject_Str(object));
	if (!text)
	{
		PyErr_Clear();
		return ts4::MakeError("str() of the object failed");
	}
	PySsize size = 0;
	const auto* utf8 = PyUnicode_AsUTF8AndSize(text.Get(), &size);
	if (utf8 == nullptr || size < 0)
	{
		PyErr_Clear();
		return ts4::MakeError("the text of the object is not UTF-8");
	}
	return std::string(utf8, static_cast<std::size_t>(size));
}

ts4::Result<PyRef> PythonApi::Owned(PyObject* object, std::string_view what) const
{
	if (object == nullptr)
	{
		return TakeError(what);
	}
	return PyRef::Steal(*this, object);
}

ts4::Result<PyRef> PythonApi::Run(std::string_view code, int start, std::string_view what) const
{
	if (auto needed = Need(PythonFeature::Eval, what); !needed)
	{
		return needed.GetError();
	}
	auto globals = Owned(PyDict_New(), what);
	if (!globals)
	{
		return globals.GetError();
	}
	// Without `__builtins__`, the code would get only `None` as a builtin.
	if (PyDict_SetItemString(globals->Get(), "__builtins__", PyEval_GetBuiltins()) != 0)
	{
		return TakeError(what);
	}
	const std::string text(code);
	return Owned(PyRun_StringFlags(text.c_str(), start, globals->Get(), globals->Get(), nullptr), what);
}

ts4::Result<PyRef> PythonApi::Eval(std::string_view expression) const
{
	return Run(expression, k_PyEvalInput, "eval");
}

ts4::Result<void> PythonApi::Exec(std::string_view code) const
{
	auto result = Run(code, k_PyFileInput, "exec");
	if (!result)
	{
		return result.GetError();
	}
	return {};
}

ts4::Result<PyRef> PythonApi::Import(const std::string& name) const
{
	const auto what = std::format("import {}", name);
	if (auto needed = Need(PythonFeature::Import, what); !needed)
	{
		return needed.GetError();
	}
	return Owned(PyImport_ImportModule(name.c_str()), what);
}

ts4::Result<PyRef> PythonApi::GetAttr(PyObject* object, const std::string& name) const
{
	const auto what = std::format("the attribute {}", name);
	if (object == nullptr)
	{
		return ts4::MakeError("{}: no object", what);
	}
	return Owned(PyObject_GetAttrString(object, name.c_str()), what);
}

ts4::Result<PyRef> PythonApi::Call(PyObject* callable) const
{
	if (auto needed = Need(PythonFeature::Import, "call"); !needed)
	{
		return needed.GetError();
	}
	if (callable == nullptr)
	{
		return ts4::MakeError("call: no object");
	}
	return Owned(PyObject_CallObject(callable, nullptr), "call");
}

ts4::Result<long long> PythonApi::ToInteger(PyObject* object) const
{
	if (auto needed = Need(PythonFeature::Values, "an integer"); !needed)
	{
		return needed.GetError();
	}
	if (object == nullptr)
	{
		return ts4::MakeError("an integer: no object");
	}
	const auto value = PyLong_AsLongLong(object);
	if (value == -1 && PyErr_Occurred() != nullptr)
	{
		return TakeError("an integer");
	}
	return value;
}

ts4::Result<PyRef> PythonApi::FromInteger(long long value) const
{
	if (auto needed = Need(PythonFeature::Values, "an integer"); !needed)
	{
		return needed.GetError();
	}
	return Owned(PyLong_FromLongLong(value), "an integer");
}

ts4::Result<PyRef> PythonApi::FromText(std::string_view text) const
{
	if (auto needed = Need(PythonFeature::Values, "a text"); !needed)
	{
		return needed.GetError();
	}
	return Owned(PyUnicode_FromStringAndSize(text.data(), static_cast<PySsize>(text.size())), "a text");
}

} // namespace ts4_mod
