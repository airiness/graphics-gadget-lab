#pragma once
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	struct SourceSnapshot
	{
		std::filesystem::path m_CanonicalPath;
		std::vector<std::byte> m_Bytes;
		SourceDigest m_Digest{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_CanonicalPath.empty() && !m_Bytes.empty() && m_Digest.IsValid();
		}
	};

	struct SourceSnapshotResult
	{
		SourceSnapshot m_Snapshot;
		std::string m_Error;
		[[nodiscard]] bool Succeeded() const noexcept { return m_Snapshot.IsValid(); }
	};

	[[nodiscard]] SourceSnapshotResult ReadSourceSnapshot(
		const std::filesystem::path& canonicalPath) noexcept;
}
