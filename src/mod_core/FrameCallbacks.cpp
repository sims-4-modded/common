/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <algorithm>
#include <exception>
#include <utility>

#include <mod_core/FrameCallbacks.h>

namespace ts4_mod
{

bool FrameCallbacks::Add(std::string name, Callback callback)
{
	const std::scoped_lock lock(_mutex);
	if (!callback || std::ranges::any_of(_entries, [&](const auto& entry) { return entry->status.name == name; }))
	{
		return false;
	}
	_entries.push_back(std::make_shared<Entry>(Entry {.status = {.name = std::move(name)}, .callback = std::move(callback)}));
	return true;
}

bool FrameCallbacks::SetEnabled(const std::string& name, bool enabled)
{
	const std::scoped_lock lock(_mutex);
	const auto entry = std::ranges::find_if(_entries, [&](const auto& item) { return item->status.name == name; });
	if (entry == _entries.end())
	{
		return false;
	}
	(*entry)->status.enabled = enabled;
	if (enabled)
	{
		(*entry)->status.error.clear();
	}
	return true;
}

void FrameCallbacks::Clear()
{
	const std::scoped_lock lock(_mutex);
	_entries.clear();
	++_generation;
}

bool FrameCallbacks::Run()
{
	uint64_t generation = 0;
	{
		const std::scoped_lock lock(_mutex);
		const auto thread = std::this_thread::get_id();
		if (_mainThread == std::thread::id())
		{
			_mainThread = thread;
		}
		if (thread != _mainThread || _inRun)
		{
			return false;
		}
		_running.assign(_entries.begin(), _entries.end());
		_inRun = true;
		++_frames;
		generation = _generation;
	}
	// The functions run with no lock held. A function can read the statuses (for example a query of the inspector
	// that the main thread does) and can change the list.
	for (const auto& entry : _running)
	{
		{
			const std::scoped_lock lock(_mutex);
			if (generation != _generation)
			{
				break;
			}
			if (!entry->status.enabled)
			{
				continue;
			}
			++entry->status.calls;
		}
		std::string error;
		try
		{
			entry->callback();
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			if (error.empty())
			{
				error = "unknown exception";
			}
		}
		catch (...)
		{
			error = "unknown exception";
		}
		if (!error.empty())
		{
			const std::scoped_lock lock(_mutex);
			entry->status.enabled = false;
			entry->status.error = std::move(error);
		}
	}
	_running.clear();
	const std::scoped_lock lock(_mutex);
	_inRun = false;
	return true;
}

uint64_t FrameCallbacks::Frames() const
{
	const std::scoped_lock lock(_mutex);
	return _frames;
}

std::vector<FrameCallbacks::Status> FrameCallbacks::Statuses() const
{
	const std::scoped_lock lock(_mutex);
	std::vector<Status> result;
	result.reserve(_entries.size());
	for (const auto& entry : _entries)
	{
		result.push_back(entry->status);
	}
	return result;
}

} // namespace ts4_mod
