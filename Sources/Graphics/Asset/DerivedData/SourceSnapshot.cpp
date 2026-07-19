#include "Core/Precompiled.h"
#include "Graphics/Asset/DerivedData/SourceSnapshot.h"
#include "Core/Hash/Sha256.h"

namespace gglab
{
	SourceSnapshotResult ReadSourceSnapshot(
		const std::filesystem::path& canonicalPath) noexcept
	{
		SourceSnapshotResult result{};
		std::ifstream stream(canonicalPath, std::ios::binary | std::ios::ate);
		if (!stream)
		{
			result.m_Error = std::format(
				"Failed to open texture source snapshot '{}'.",
				canonicalPath.string());
			return result;
		}
		const std::streamoff end = stream.tellg();
		if (end <= 0 || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max())
		{
			result.m_Error = std::format(
				"Texture source snapshot '{}' has an invalid size.",
				canonicalPath.string());
			return result;
		}
		result.m_Snapshot.m_CanonicalPath = canonicalPath;
		result.m_Snapshot.m_Bytes.resize(static_cast<size_t>(end));
		stream.seekg(0, std::ios::beg);
		stream.read(
			reinterpret_cast<char*>(result.m_Snapshot.m_Bytes.data()),
			static_cast<std::streamsize>(result.m_Snapshot.m_Bytes.size()));
		if (!stream)
		{
			result.m_Snapshot = {};
			result.m_Error = std::format(
				"Failed to read texture source snapshot '{}'.",
				canonicalPath.string());
			return result;
		}
		result.m_Snapshot.m_Digest.m_Value =
			ComputeSha256(result.m_Snapshot.m_Bytes).m_Value;
		if (!result.m_Snapshot.m_Digest.IsValid())
		{
			result.m_Snapshot = {};
			result.m_Error = std::format(
				"Failed to hash texture source snapshot '{}'.",
				canonicalPath.string());
		}
		return result;
	}
}
