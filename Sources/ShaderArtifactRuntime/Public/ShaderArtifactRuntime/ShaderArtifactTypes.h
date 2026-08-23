#pragma once
#include "GGLabFoundation/Base/EnumFlags.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace gglab
{
	enum class ShaderBinaryFormat : uint8_t
	{
		Unknown,
		Dxil,
		SpirV,
	};

	enum class ShaderStage : uint32_t
	{
		Vertex,
		Pixel,
		Hull,
		Domain,
		Geometry,
		Mesh,
		Compute
	};

	enum class ShaderModel : uint32_t
	{
		SM_6_6,
		SM_6_7,
		SM_6_8
	};

	enum class ShaderCompileFlags : uint32_t
	{
		None = 0u,
		Debug = 1u << 0,
		Optimization = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(ShaderCompileFlags);

	enum class ShaderSpirVTargetEnvironment : uint8_t
	{
		None,
		Vulkan1_3,
	};

	enum class ShaderCoordinateOptions : uint8_t
	{
		None = 0u,
		InvertY = 1u << 0,
		UseDxPositionW = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(ShaderCoordinateOptions);

	enum class ShaderTargetProfile : uint8_t
	{
		GGLabDX12,
		GGLabVulkan13,
	};

	[[nodiscard]] constexpr ShaderTargetProfile GetShaderTargetProfile(
		ShaderBinaryFormat binaryFormat, ShaderSpirVTargetEnvironment environment) noexcept
	{
		if (binaryFormat == ShaderBinaryFormat::SpirV &&
			environment == ShaderSpirVTargetEnvironment::Vulkan1_3)
		{
			return ShaderTargetProfile::GGLabVulkan13;
		}
		return ShaderTargetProfile::GGLabDX12;
	}

	enum class ShaderCompilerKind : uint8_t
	{
		Dxc,
	};

	struct ShaderCompilerIdentity
	{
		ShaderCompilerKind m_Kind = ShaderCompilerKind::Dxc;
		std::wstring m_CanonicalIdentity;

		friend constexpr bool operator==(
			const ShaderCompilerIdentity&, const ShaderCompilerIdentity&) noexcept = default;
	};

	// Transitional compact digest container. Durable persisted identity uses
	// BinaryContentDigest (SHA-256) instead; this value remains a runtime
	// convenience representation only.
	struct ShaderHash128
	{
		uint64_t m_LowBits = 0;
		uint64_t m_HighBits = 0;

		auto AsTuple() const noexcept { return std::make_tuple(m_LowBits, m_HighBits); }
		constexpr bool operator==(const ShaderHash128&) const noexcept = default;
	};

	// GPU binary byte container shared by the Toolchain producer and the
	// Runtime consumer (ShaderBytecode points into it).
	class ShaderBinary
	{
	public:
		ShaderBinary() = default;
		explicit ShaderBinary(size_t sizeInBytes) : m_Data(sizeInBytes) {}

		[[nodiscard]] void* Data() noexcept { return m_Data.data(); }
		[[nodiscard]] const void* Data() const noexcept { return m_Data.data(); }
		[[nodiscard]] size_t SizeInBytes() const noexcept { return m_Data.size(); }
		[[nodiscard]] bool IsValid() const noexcept { return !m_Data.empty(); }
		void Reset() noexcept { m_Data.clear(); }

	private:
		std::vector<std::byte> m_Data;
	};
}
