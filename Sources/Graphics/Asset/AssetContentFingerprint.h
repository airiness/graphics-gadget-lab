#pragma once
#include <cstdint>

namespace gglab
{
	struct AssetContentFingerprint
	{
		// Hashes the normalized decoded payload that is eligible for publication,
		// never the source path or a later re-read of the source file.
		uint64_t m_SourceContentHash = 0;
		uint64_t m_ImportSettingsHash = 0;
		uint32_t m_DecoderVersion = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_SourceContentHash != 0 &&
				m_ImportSettingsHash != 0 &&
				m_DecoderVersion != 0;
		}

		bool operator==(const AssetContentFingerprint&) const noexcept = default;
	};
}
