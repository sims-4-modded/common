/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <format>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace ts4
{

/// The reason that an operation did not complete.
struct Error
{
	std::string message;
};

/// Make an error with a formatted message.
template <typename... Args>
[[nodiscard]] Error MakeError(std::format_string<Args...> format, Args&&... args)
{
	return Error {std::format(format, std::forward<Args>(args)...)};
}

/// A value, or the error that stopped the operation that makes the value.
template <typename T>
class [[nodiscard]] Result
{
public:
	Result(T value)
	    : _state(std::move(value))
	{
	}

	Result(Error error)
	    : _state(std::move(error))
	{
	}

	[[nodiscard]] bool HasValue() const { return std::holds_alternative<T>(_state); }
	explicit operator bool() const { return HasValue(); }

	[[nodiscard]] T& Value() & { return std::get<T>(_state); }
	[[nodiscard]] const T& Value() const& { return std::get<T>(_state); }
	[[nodiscard]] T&& Value() && { return std::get<T>(std::move(_state)); }

	[[nodiscard]] const Error& GetError() const { return std::get<Error>(_state); }

	// Use std::addressof: a COM smart pointer changes the meaning of `&`.
	T* operator->() { return std::addressof(Value()); }
	const T* operator->() const { return std::addressof(Value()); }
	T& operator*() & { return Value(); }
	const T& operator*() const& { return Value(); }

private:
	std::variant<T, Error> _state;
};

/// The result of an operation that makes no value.
template <>
class [[nodiscard]] Result<void>
{
public:
	Result() = default;

	Result(Error error)
	    : _error(std::move(error))
	    , _failed(true)
	{
	}

	[[nodiscard]] bool HasValue() const { return !_failed; }
	explicit operator bool() const { return HasValue(); }

	[[nodiscard]] const Error& GetError() const { return _error; }

private:
	Error _error;
	bool _failed = false;
};

} // namespace ts4
