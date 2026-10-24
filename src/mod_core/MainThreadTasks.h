/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <deque>
#include <future>
#include <mutex>
#include <utility>

#include <nlohmann/json.hpp>

namespace ts4_mod
{

/// Work that another thread gives to the main thread of the game. The inspector threads cannot read game objects or
/// call game Python, so a query puts a task here and waits. The main thread calls `RunPending` between two ticks.
class MainThreadTasks
{
public:
	using Task = std::packaged_task<nlohmann::json()>;

	/// Add a task. The future gets the result, or the exception that the task throws.
	[[nodiscard]] std::future<nlohmann::json> Post(Task task)
	{
		auto future = task.get_future();
		const std::scoped_lock lock(_mutex);
		_tasks.push_back(std::move(task));
		return future;
	}

	/// Do all the tasks that wait. Call this only on the main thread.
	void RunPending()
	{
		std::deque<Task> tasks;
		{
			const std::scoped_lock lock(_mutex);
			tasks.swap(_tasks);
		}
		for (auto& task : tasks)
		{
			// A packaged task keeps an exception in its future. It does not throw here.
			task();
		}
	}

	/// The number of tasks that wait.
	[[nodiscard]] size_t Pending() const
	{
		const std::scoped_lock lock(_mutex);
		return _tasks.size();
	}

private:
	mutable std::mutex _mutex;
	std::deque<Task> _tasks;
};

} // namespace ts4_mod
