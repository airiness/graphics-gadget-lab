#include "Core/Precompiled.h"
#include "Graphics/Asset/ArtifactContentDigest.h"

namespace gglab
{
	std::string ArtifactContentDigestText(const ArtifactContentDigest& digest, size_t byteCount)
	{
		if (!digest.IsValid())
		{
			return {};
		}

		constexpr char HexDigits[] = "0123456789abcdef";
		const size_t count = std::min(byteCount, digest.m_Value.size());
		std::string result(count * 2, '0');
		for (size_t index = 0; index < count; ++index)
		{
			const uint8_t value = std::to_integer<uint8_t>(digest.m_Value[index]);
			result[index * 2] = HexDigits[value >> 4];
			result[index * 2 + 1] = HexDigits[value & 0x0f];
		}
		return result;
	}
}
