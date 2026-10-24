/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ts4_mod
{

/// The functions that the extender runs one time in each frame, on the main thread. The hook that runs one time in
/// each frame calls `Run`. A function that throws an exception stops: the list keeps the error and does not call it again,
/// so that one bad mod cannot stop the game.
class FrameCallbacks
{
public:
	using Callback = std::function<void()>;

	struct Status
	{
		std::string name;
		bool enabled = true;
		uint64_t calls = 0;
		std::string error;
	};

	/// Add a function. Each name can occur one time only. Return false if the name exists.
	bool Add(std::string name, Callback callback);

	/// Stop or start a function by its name. Return false if the name does not exist.
	bool SetEnabled(const std::string& name, bool enabled);

	/// Remove all functions.
	void Clear();

	/// Call each enabled function. The first thread that calls `Run` is the main thread. A call from a different
	/// thread, or a call from a function of the list, does nothing and returns false. The lock is not held while a
	/// function runs, so a function can read the statuses, add a function or stop one. A function that `Clear`
	/// removes during `Run` does not run after the removal.
	bool Run();

	[[nodiscard]] uint64_t Frames() const;
	[[nodiscard]] std::vector<Status> Statuses() const;

private:
	struct Entry
	{
		Status status;
		Callback callback;
	};

	mutable std::mutex _mutex;
	/// The entries are shared, so that an entry that `Clear` removes stays alive while its function runs.
	std::vector<std::shared_ptr<Entry>> _entries;
	std::thread::id _mainThread;
	uint64_t _frames = 0;
	/// Each `Clear` increases it. `Run` stops when it changes.
	uint64_t _generation = 0;
	/// Main thread only: the entries of the frame that `Run` calls, and true while `Run` calls them.
	std::vector<std::shared_ptr<Entry>> _running;
	bool _inRun = false;
};

} // namespace ts4_mod
