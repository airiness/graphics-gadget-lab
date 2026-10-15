#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/RHISubresourceUtils.h"

#include <limits>
#include <utility>

namespace gglab
{
	std::string_view GetNonFiniteWatchSemanticName(NonFiniteWatchSemantic semantic) noexcept
	{
		switch (semantic)
		{
		case NonFiniteWatchSemantic::MotionVectors: return "motion-vectors";
		case NonFiniteWatchSemantic::AmbientOcclusion: return "ambient-occlusion";
		case NonFiniteWatchSemantic::TemporalInput: return "scene-color.temporal-input";
		case NonFiniteWatchSemantic::PostProcessInput: return "scene-color.post-process-input";
		case NonFiniteWatchSemantic::Environment: return "ibl.environment";
		case NonFiniteWatchSemantic::Irradiance: return "ibl.irradiance";
		case NonFiniteWatchSemantic::Specular: return "ibl.specular";
		case NonFiniteWatchSemantic::AtmosphereTransmittance: return "atmosphere.transmittance";
		case NonFiniteWatchSemantic::AtmosphereMultipleScattering: return "atmosphere.multiple-scattering";
		case NonFiniteWatchSemantic::AtmosphereSkyView: return "atmosphere.sky-view";
		default: return "unknown";
		}
	}

	void NonFiniteWatchDefaultRegistry::Declare(std::array<bool, NonFiniteWatchSemanticCount> applicable) noexcept
	{
		m_ConflictingDeclaration |= m_Declared;
		m_Declared = true;
		for (size_t i = 0; i < m_Entries.size(); ++i)
		{
			m_Entries[i].m_Applicable = applicable[i];
		}
	}

	void NonFiniteWatchDefaultRegistry::Register(NonFiniteWatchSemantic semantic,
		RGResourceHandle resource, RGResourceAddress address) noexcept
	{
		const size_t index = static_cast<size_t>(semantic);
		if (index < m_Entries.size())
		{
			m_Entries[index].m_Bindings.push_back({ resource, std::move(address) });
		}
	}

	void NonFiniteWatchDefaultRegistry::RegisterGeneration(NonFiniteWatchSemantic semantic,
		RGResourceHandle resource, RGResourceAddress address, NonFiniteWatchGeneration generation) noexcept
	{
		const size_t index = static_cast<size_t>(semantic);
		if (index < m_Entries.size())
		{
			m_Entries[index].m_Bindings.push_back({ resource, std::move(address), generation });
		}
	}

	NonFiniteWatchDefaults NonFiniteWatchDefaultRegistry::Resolve(const RenderGraph& graph) const noexcept
	{
		NonFiniteWatchDefaults result;
		for (size_t i = 0; i < result.size(); ++i)
		{
			auto& target = result[i];
			target.m_Semantic = static_cast<NonFiniteWatchSemantic>(i);
			const auto& entry = m_Entries[i];
			if (!m_Declared || m_ConflictingDeclaration)
			{
				target.m_Failure = "MissingCoverage: pipeline applicability is missing or declared more than once.";
			}
			else if (!entry.m_Applicable && entry.m_Bindings.empty())
			{
				target.m_Claim = { NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			}
			else if (!entry.m_Applicable || entry.m_Bindings.empty())
			{
				target.m_Failure = "MissingCoverage: active feature has no binding, or inactive feature has a binding.";
			}
			else
			{
				uint32_t earliest = std::numeric_limits<uint32_t>::max();
				bool conflicting = false;
				for (const auto& binding : entry.m_Bindings)
				{
					conflicting |= binding.m_Resource != entry.m_Bindings.front().m_Resource;
					conflicting |= binding.m_Generation != entry.m_Bindings.front().m_Generation;
					const auto resolved = graph.ResolveObservation(binding.m_Address, RGObservationKind::NonFiniteScan);
					conflicting |= resolved.m_Failure != RGObservationFailure::None && resolved.m_Failure != RGObservationFailure::Culled;
					conflicting |= resolved.m_Failure == RGObservationFailure::None && resolved.m_Resource != binding.m_Resource;
					if (resolved.m_Failure == RGObservationFailure::None && resolved.m_Resource == binding.m_Resource &&
						resolved.m_InsertionPosition < earliest)
					{
						earliest = resolved.m_InsertionPosition;
						target.m_Address = binding.m_Address;
						target.m_Resource = binding.m_Resource;
						target.m_Ready = true;
						target.m_Generation = binding.m_Generation;
					}
				}
				if (GetNonFiniteWatchTier(target.m_Semantic) == 2 && target.m_Ready)
				{
					conflicting |= !target.m_Generation.IsValid();
					const auto base = graph.ResolveObservation(target.m_Address, RGObservationKind::NonFiniteScan);
					for (uint32_t mip = 0; mip < GetRHITextureMipLevelCount(base.m_TextureDesc); ++mip)
					{
						auto address = target.m_Address;
						address.m_Mip = mip;
						address.m_FirstSlice = 0;
						address.m_SliceCount = GetRHITextureArraySize(base.m_TextureDesc);
						const auto resolved = graph.ResolveObservation(address, RGObservationKind::NonFiniteScan);
						conflicting |= resolved.m_Failure != RGObservationFailure::None || resolved.m_Resource != target.m_Resource;
						target.m_Mips.push_back(std::move(address));
					}
					conflicting |= target.m_Mips.empty();
				}
				if (conflicting || !target.m_Ready)
				{
					target.m_Ready = false;
					target.m_Resource.reset();
					target.m_Failure = "MissingCoverage: bindings disagree on the version or no live exact version resolves.";
				}
			}
		}
		return result;
	}

	NonFiniteWatchDefaults ResolveNonFiniteWatchDefaults(const RenderGraph& graph) noexcept
	{
		const auto* registry = graph.GetBlackboard().TryGet<NonFiniteWatchDefaultRegistry>();
		return registry ? registry->Resolve(graph) : NonFiniteWatchDefaultRegistry{}.Resolve(graph);
	}

	std::optional<uint32_t> GetNonFiniteDefaultWatchTier(const RenderGraph& graph, const RGObservationTarget& target) noexcept
	{
		for (const auto& semantic : ResolveNonFiniteWatchDefaults(graph))
		{
			if (semantic.m_Ready && semantic.m_Resource == target.m_Resource && target.m_Resource)
			{
				return GetNonFiniteWatchTier(semantic.m_Semantic);
			}
		}
		return std::nullopt;
	}
}
