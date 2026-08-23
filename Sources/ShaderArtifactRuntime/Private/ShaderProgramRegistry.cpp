#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <functional>
#include <mutex>
#include <utility>

namespace gglab
{
	size_t ShaderProgramRefHash::operator()(const ShaderProgramRef& programRef) const noexcept
	{
		const size_t programHash = std::hash<std::string>{}(programRef.m_ProgramId);
		const size_t variantHash = std::hash<std::string>{}(programRef.m_VariantId);
		return programHash ^ (variantHash + static_cast<size_t>(0x9E3779B9u) +
			(programHash << 6u) + (programHash >> 2u));
	}

	ShaderProgramBindStatus ShaderProgramRegistry::Bind(
		const ShaderProgramRef& programRef,
		const ShaderArtifactRef& artifactRef) noexcept
	{
		if (!programRef.IsValid())
		{
			return ShaderProgramBindStatus::InvalidProgram;
		}
		if (!artifactRef.IsValid())
		{
			return ShaderProgramBindStatus::InvalidArtifact;
		}

		try
		{
			std::unique_lock lock(m_Mutex);
			const auto iterator = m_Mappings.find(programRef);
			if (iterator == m_Mappings.end())
			{
				m_Mappings.emplace(programRef, artifactRef);
				return ShaderProgramBindStatus::Bound;
			}
			if (iterator->second == artifactRef)
			{
				return ShaderProgramBindStatus::AlreadyBound;
			}
			iterator->second = artifactRef;
			return ShaderProgramBindStatus::Rebound;
		}
		catch (...)
		{
			return ShaderProgramBindStatus::Failed;
		}
	}

	std::optional<ShaderArtifactRef> ShaderProgramRegistry::Resolve(
		const ShaderProgramRef& programRef) const noexcept
	{
		if (!programRef.IsValid())
		{
			return std::nullopt;
		}
		try
		{
			std::shared_lock lock(m_Mutex);
			const auto iterator = m_Mappings.find(programRef);
			return iterator != m_Mappings.end()
				? std::optional<ShaderArtifactRef>(iterator->second)
				: std::nullopt;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	size_t ShaderProgramRegistry::GetMappingCount() const noexcept
	{
		try
		{
			std::shared_lock lock(m_Mutex);
			return m_Mappings.size();
		}
		catch (...)
		{
			return 0;
		}
	}

	ShaderProgramDemandAddStatus ShaderProgramDemandSet::Add(
		const ShaderProgramRef& programRef) noexcept
	{
		if (!programRef.IsValid())
		{
			return ShaderProgramDemandAddStatus::InvalidProgram;
		}
		if (std::ranges::find(m_Programs, programRef) != m_Programs.end())
		{
			return ShaderProgramDemandAddStatus::AlreadyPresent;
		}
		try
		{
			m_Programs.push_back(programRef);
			return ShaderProgramDemandAddStatus::Added;
		}
		catch (...)
		{
			return ShaderProgramDemandAddStatus::Failed;
		}
	}

	bool ShaderProgramDemandSet::AddRange(
		std::span<const ShaderProgramRef> programRefs) noexcept
	{
		for (const ShaderProgramRef& programRef : programRefs)
		{
			const ShaderProgramDemandAddStatus status = Add(programRef);
			if (status != ShaderProgramDemandAddStatus::Added &&
				status != ShaderProgramDemandAddStatus::AlreadyPresent)
			{
				return false;
			}
		}
		return true;
	}

	std::span<const ShaderProgramRef> ShaderProgramDemandSet::GetPrograms() const noexcept
	{
		return m_Programs;
	}

	std::vector<ShaderProgramRef> ShaderProgramDemandSet::ReleasePrograms() noexcept
	{
		return std::move(m_Programs);
	}
}
