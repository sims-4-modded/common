/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <functional>
#include <string>
#include <string_view>

#include <common/Result.h>

// The objects of the interpreter are opaque. The extender uses them only through the Py* exports of the DLL of the
// interpreter, so it does not depend on the layout of an object. This is the name that the Python headers use.
struct _object;

namespace ts4_mod
{

using PyObject = ::_object;
using PySsize = std::intptr_t;

/// The file name of the interpreter of the game in `Game\Bin`.
inline constexpr std::string_view k_GamePythonModule = "python37_x64.dll";

/// The groups of Python functions that can be off. Each group needs some exports. If an export of a group is missing,
/// only the functions of that group return an error.
enum class PythonFeature
{
	Eval,   ///< `Eval` and `Exec`.
	Import, ///< `Import` and `Call`.
	Values, ///< `ToInteger`, `FromInteger` and `FromText`.
};

// Each row: the group, the result type, the name and the parameter types. The core functions must all be there. The
// table follows the C API of Python 3.7.
#define TS4_PYTHON_FUNCTIONS(X)                                                            \
	X(Core, int, Py_IsInitialized, ())                                                     \
	X(Core, int, PyGILState_Ensure, ())                                                    \
	X(Core, void, PyGILState_Release, (int))                                               \
	X(Core, void, Py_IncRef, (PyObject*))                                                  \
	X(Core, void, Py_DecRef, (PyObject*))                                                  \
	X(Core, PyObject*, PyErr_Occurred, ())                                                 \
	X(Core, void, PyErr_Fetch, (PyObject**, PyObject**, PyObject**))                       \
	X(Core, void, PyErr_NormalizeException, (PyObject**, PyObject**, PyObject**))          \
	X(Core, void, PyErr_Clear, ())                                                         \
	X(Core, PyObject*, PyObject_Str, (PyObject*))                                          \
	X(Core, PyObject*, PyObject_GetAttrString, (PyObject*, const char*))                   \
	X(Core, const char*, PyUnicode_AsUTF8AndSize, (PyObject*, PySsize*))                   \
	X(Eval, PyObject*, PyRun_StringFlags, (const char*, int, PyObject*, PyObject*, void*)) \
	X(Eval, PyObject*, PyDict_New, ())                                                     \
	X(Eval, int, PyDict_SetItemString, (PyObject*, const char*, PyObject*))                \
	X(Eval, PyObject*, PyEval_GetBuiltins, ())                                             \
	X(Import, PyObject*, PyImport_ImportModule, (const char*))                             \
	X(Import, PyObject*, PyObject_CallObject, (PyObject*, PyObject*))                      \
	X(Values, long long, PyLong_AsLongLong, (PyObject*))                                   \
	X(Values, PyObject*, PyLong_FromLongLong, (long long))                                 \
	X(Values, PyObject*, PyUnicode_FromStringAndSize, (const char*, PySsize))

class PythonApi;

/// An owned reference to a Python object. The destructor calls `Py_DecRef`; a copy calls `Py_IncRef`. Copy, move and
/// destroy a reference only while the thread holds the GIL. The reference keeps the two functions, not the
/// `PythonApi`, so a move of the `PythonApi` does not change it.
class PyRef
{
public:
	PyRef() = default;

	/// Take a new reference, for example the result of a call. A null object gives an empty reference.
	[[nodiscard]] static PyRef Steal(const PythonApi& api, PyObject* object);
	/// Add a reference to a borrowed object.
	[[nodiscard]] static PyRef Borrow(const PythonApi& api, PyObject* object);

	~PyRef() { Reset(); }
	PyRef(const PyRef& other);
	PyRef& operator=(const PyRef& other);
	PyRef(PyRef&& other) noexcept;
	PyRef& operator=(PyRef&& other) noexcept;

	[[nodiscard]] PyObject* Get() const { return _object; }
	explicit operator bool() const { return _object != nullptr; }

	/// Give the reference to the caller. The object becomes empty.
	[[nodiscard]] PyObject* Release();
	/// Give back the reference, if there is one.
	void Reset();

private:
	using RefFunction = void (*)(PyObject*);

	PyRef(RefFunction incRef, RefFunction decRef, PyObject* object)
	    : _incRef(incRef)
	    , _decRef(decRef)
	    , _object(object)
	{
	}

	RefFunction _incRef = nullptr;
	RefFunction _decRef = nullptr;
	PyObject* _object = nullptr;
};

/// The Python 3.7 C API of the interpreter of the game. `Find` gets each export one time with GetProcAddress. The
/// extender links to no Python library.
///
/// Call each function that uses an object only while the thread holds the GIL (see `GilGuard`). A function that
/// fails returns an error with the text of the Python exception, and clears the exception.
class PythonApi
{
public:
#define TS4_PYTHON_MEMBER(group, result, name, parameters) result(*name) parameters = nullptr;
	TS4_PYTHON_FUNCTIONS(TS4_PYTHON_MEMBER)
#undef TS4_PYTHON_MEMBER

	/// Find the exports in the loaded module `moduleName`. Return an error if the module is not loaded, or if a core
	/// export is missing. A missing export of a group turns off only that group.
	[[nodiscard]] static ts4::Result<PythonApi> Find(std::string_view moduleName = k_GamePythonModule);

	/// Find the exports in a loaded module. `module` is the HMODULE of the module.
	[[nodiscard]] static ts4::Result<PythonApi> FromModule(void* module, std::string moduleName);

	/// Find the exports with `resolve`, which gives the address of an export by its name, or null.
	[[nodiscard]] static ts4::Result<PythonApi> Resolve(std::string moduleName,
	                                                    const std::function<void*(const char* name)>& resolve);

	[[nodiscard]] const std::string& ModuleName() const { return _moduleName; }
	[[nodiscard]] bool Has(PythonFeature feature) const;
	/// The names of the missing exports of a group, or empty.
	[[nodiscard]] const std::string& Missing(PythonFeature feature) const;

	/// Get the exception of the interpreter, clear it, and make an error: "<what>: <type>: <text of the value>".
	[[nodiscard]] ts4::Error TakeError(std::string_view what) const;

	/// The text of an object, as `str()` gives it.
	[[nodiscard]] ts4::Result<std::string> Str(PyObject* object) const;

	/// Get the value of a Python expression. The expression runs in a new namespace with the builtins only.
	[[nodiscard]] ts4::Result<PyRef> Eval(std::string_view expression) const;
	/// Run Python statements in a new namespace with the builtins only.
	[[nodiscard]] ts4::Result<void> Exec(std::string_view code) const;

	/// Import a module, for example "services".
	[[nodiscard]] ts4::Result<PyRef> Import(const std::string& name) const;
	/// Get an attribute of an object.
	[[nodiscard]] ts4::Result<PyRef> GetAttr(PyObject* object, const std::string& name) const;
	/// Call an object with no arguments.
	[[nodiscard]] ts4::Result<PyRef> Call(PyObject* callable) const;

	/// The value of a Python int that fits in 64 bits.
	[[nodiscard]] ts4::Result<long long> ToInteger(PyObject* object) const;
	[[nodiscard]] ts4::Result<PyRef> FromInteger(long long value) const;
	[[nodiscard]] ts4::Result<PyRef> FromText(std::string_view text) const;

private:
	PythonApi() = default;

	/// Return an error if a group is off.
	[[nodiscard]] ts4::Result<void> Need(PythonFeature feature, std::string_view what) const;
	/// Take a new reference from a call of the interpreter, or the error of the interpreter.
	[[nodiscard]] ts4::Result<PyRef> Owned(PyObject* object, std::string_view what) const;
	[[nodiscard]] ts4::Result<PyRef> Run(std::string_view code, int start, std::string_view what) const;

	std::string _moduleName;
	std::string _missingEval;
	std::string _missingImport;
	std::string _missingValues;
};

/// Hold the GIL while the object exists (`PyGILState_Ensure` and `PyGILState_Release`). A thread that holds the GIL
/// already can make a guard too. Destroy the guards in the reverse order of their construction.
class GilGuard
{
public:
	explicit GilGuard(const PythonApi& api)
	    : _release(api.PyGILState_Release)
	    , _state(api.PyGILState_Ensure())
	{
	}
	~GilGuard() { _release(_state); }
	GilGuard(const GilGuard&) = delete;
	GilGuard& operator=(const GilGuard&) = delete;
	GilGuard(GilGuard&&) = delete;
	GilGuard& operator=(GilGuard&&) = delete;

private:
	void (*_release)(int);
	int _state;
};

} // namespace ts4_mod
