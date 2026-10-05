#pragma once

namespace gglab::win32
{
	// Routes CRT assertions, CRT errors and abort messages to stderr and
	// suppresses Windows error dialogs. An unattended process then fails visibly
	// in its output and exit code instead of waiting on a dialog nobody can see.
	void ConfigureUnattendedFailureReporting() noexcept;
}
