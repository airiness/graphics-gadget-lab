#include "ShaderArtifactRuntime/ShaderArtifact.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace gglab
{
	bool IsShaderBinaryFormat(const ShaderBinary& binary, ShaderBinaryFormat format) noexcept
	{
		if (!binary.IsValid())
		{
			return false;
		}

		const auto* data = static_cast<const uint8_t*>(binary.Data());
		const size_t size = binary.SizeInBytes();
		switch (format)
		{
		case ShaderBinaryFormat::Dxil:
			return size >= 20 && std::memcmp(data, "DXBC", 4) == 0;
		case ShaderBinaryFormat::SpirV:
		{
			if (size < 5 * sizeof(uint32_t) || size % sizeof(uint32_t) != 0)
			{
				return false;
			}
			uint32_t magic = 0;
			std::memcpy(&magic, data, sizeof(magic));
			return magic == 0x07230203u;
		}
		case ShaderBinaryFormat::Unknown:
			break;
		}
		return false;
	}

	namespace
	{
		[[nodiscard]] bool GetContainerHash(
			const void* data, size_t size, ShaderHash128& outHash) noexcept
		{
			constexpr size_t MinDxilSize = 20;
			if (data == nullptr || size < MinDxilSize)
			{
				return false;
			}

			static const unsigned char DXBCMagicNumber[] = { 'D', 'X', 'B', 'C' };
			if (std::memcmp(data, DXBCMagicNumber, 4) != 0)
			{
				return false;
			}
			const unsigned char* ptr = static_cast<const unsigned char*>(data);
			std::memcpy(&outHash.m_LowBits, ptr + 4, sizeof(uint64_t));
			std::memcpy(&outHash.m_HighBits, ptr + 12, sizeof(uint64_t));
			return true;
		}
	}

	ShaderHash128 ComputeShaderBinaryHash(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept
	{
		ShaderHash128 hash{};
		if (!binary.IsValid())
		{
			return hash;
		}

		const auto* ptr = static_cast<const uint8_t*>(binary.Data());
		const auto size = binary.SizeInBytes();
		if (format == ShaderBinaryFormat::Dxil && GetContainerHash(ptr, size, hash))
		{
			return hash;
		}
		return ShaderDigestFastHash(ComputeSha256(
			std::span(reinterpret_cast<const std::byte*>(ptr), size)));
	}
}
