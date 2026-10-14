#pragma once

#include <filesystem>
#include <string>

namespace gglab::win32
{
	// Opens an existing directory through the Windows Shell. Success means the
	// Shell accepted the request, not that an Explorer window has appeared.
	// Call only for an explicit user action; never from capture publication.
	[[nodiscard]] bool OpenDirectory(const std::filesystem::path& directory,
		std::string& failure) noexcept;
}
