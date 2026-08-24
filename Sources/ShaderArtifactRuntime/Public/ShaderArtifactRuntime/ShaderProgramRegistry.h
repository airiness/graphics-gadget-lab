#pragma once
#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gglab
{
	inline constexpr size_t MaxShaderProgramIdentityComponentSize = 1024;
	inline constexpr ShaderStage InvalidShaderProgramStage =
		static_cast<ShaderStage>(UINT32_MAX);

	[[nodiscard]] constexpr bool IsValidShaderProgramIdentityComponent(
		std::string_view value) noexcept
	{
		if (value.empty() || value.size() > MaxShaderProgramIdentityComponentSize)
		{
			return false;
		}
		for (size_t index = 0; index < value.size();)
		{
			const uint8_t first = static_cast<uint8_t>(value[index]);
			if (first == 0)
			{
				return false;
			}
			if (first <= 0x7fu)
			{
				++index;
				continue;
			}

			size_t continuationCount = 0;
			uint32_t codePoint = 0;
			if (first >= 0xc2u && first <= 0xdfu)
			{
				continuationCount = 1;
				codePoint = first & 0x1fu;
			}
			else if (first >= 0xe0u && first <= 0xefu)
			{
				continuationCount = 2;
				codePoint = first & 0x0fu;
			}
			else if (first >= 0xf0u && first <= 0xf4u)
			{
				continuationCount = 3;
				codePoint = first & 0x07u;
			}
			else
			{
				return false;
			}
			if (index + continuationCount >= value.size())
			{
				return false;
			}
			for (size_t continuation = 1; continuation <= continuationCount; ++continuation)
			{
				const uint8_t byte = static_cast<uint8_t>(value[index + continuation]);
				if ((byte & 0xc0u) != 0x80u)
				{
					return false;
				}
				codePoint = (codePoint << 6u) | (byte & 0x3fu);
			}
			if ((continuationCount == 2 && codePoint < 0x800u) ||
				(continuationCount == 3 && codePoint < 0x10000u) ||
				(codePoint >= 0xd800u && codePoint <= 0xdfffu) || codePoint > 0x10ffffu)
			{
				return false;
			}
			index += continuationCount + 1;
		}
		return true;
	}

	struct ShaderProgramRef final
	{
		std::string m_ProgramId;
		std::string m_VariantId;
		ShaderStage m_Stage = InvalidShaderProgramStage;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return IsValidShaderProgramIdentityComponent(m_ProgramId) &&
				IsValidShaderProgramIdentityComponent(m_VariantId) &&
				IsKnownShaderStage(m_Stage);
		}

		friend bool operator==(
			const ShaderProgramRef&, const ShaderProgramRef&) noexcept = default;
	};

	struct ShaderProgramRefHash final
	{
		[[nodiscard]] size_t operator()(const ShaderProgramRef& programRef) const noexcept;
	};

	enum class ShaderProgramBindStatus : uint8_t
	{
		Bound,
		AlreadyBound,
		Rebound,
		InvalidProgram,
		InvalidArtifact,
		Failed,
	};

	class ShaderProgramRegistry final
	{
	public:
		[[nodiscard]] ShaderProgramBindStatus Bind(
			const ShaderProgramRef& programRef,
			const ShaderArtifactRef& artifactRef) noexcept;
		[[nodiscard]] std::optional<ShaderArtifactRef> Resolve(
			const ShaderProgramRef& programRef) const noexcept;
		[[nodiscard]] size_t GetMappingCount() const noexcept;

	private:
		mutable std::shared_mutex m_Mutex;
		std::unordered_map<ShaderProgramRef, ShaderArtifactRef, ShaderProgramRefHash> m_Mappings;
	};

	enum class ShaderProgramDemandAddStatus : uint8_t
	{
		Added,
		AlreadyPresent,
		InvalidProgram,
		Failed,
	};

	// Ordered, duplicate-free aggregation used to freeze one preload snapshot.
	// Demand declaration carries stable logical identity and expected Runtime stage;
	// it never carries source, entry-point, define, or compiler policy.
	class ShaderProgramDemandSet final
	{
	public:
		[[nodiscard]] ShaderProgramDemandAddStatus Add(
			const ShaderProgramRef& programRef) noexcept;
		[[nodiscard]] bool AddRange(std::span<const ShaderProgramRef> programRefs) noexcept;
		[[nodiscard]] std::span<const ShaderProgramRef> GetPrograms() const noexcept;
		[[nodiscard]] std::vector<ShaderProgramRef> ReleasePrograms() noexcept;

	private:
		std::vector<ShaderProgramRef> m_Programs;
	};
}
