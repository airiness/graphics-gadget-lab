#pragma once

#include <filesystem>

namespace gglab::win32
{
	// Routes CRT assertions, CRT errors and abort messages to stderr and
	// suppresses Windows error dialogs. An unattended process then fails visibly
	// in its output and exit code instead of waiting on a dialog nobody can see.
	void ConfigureUnattendedFailureReporting() noexcept;

	// Reopens stdout and stderr on one file, for both the C runtime and the
	// process standard handles, so console logging and outcome lines reach it.
	// A launcher can then start the process without inheriting any handle.
	[[nodiscard]] bool RedirectStandardOutputToFile(const std::filesystem::path& path) noexcept;
}
