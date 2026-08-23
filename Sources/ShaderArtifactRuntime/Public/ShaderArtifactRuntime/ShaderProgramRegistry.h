#pragma once
#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gglab
{
	struct ShaderProgramRef final
	{
		std::string m_ProgramId;
		std::string m_VariantId;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return !m_ProgramId.empty() && !m_VariantId.empty();
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
	// Demand declaration carries stable Runtime identity only; it never carries
	// source, entry-point, define, or compiler policy.
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
