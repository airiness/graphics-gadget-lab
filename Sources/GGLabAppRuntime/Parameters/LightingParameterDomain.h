#pragma once
#include "Parameters/ParameterSource.h"
#include "GGLabRuntime/Graphics/LightingMutationResult.h"

#include <cstdint>

namespace gglab
{
	class RenderEnvironmentAccess;

	// Domain-specific attribution and publication policy shared by source adapters.
	// Receipts and successful-submission consumption stay with Session.
	class LightingParameterDomain final
	{
	public:
		explicit LightingParameterDomain(RenderEnvironmentAccess& environment) noexcept :
			m_Environment(environment) {}
		void BeginFrame() noexcept;
		void BeginApplication(ParameterDomainState& state) const noexcept;
		void ObserveFrameEffects(const ParameterDomainState& state) noexcept;
		void CompleteEffects(ParameterDomainState& state) const noexcept;
		[[nodiscard]] ParameterApplication Translate(const LightingMutationResult& evidence,
			bool unchanged, ParameterDomainState& state) const noexcept;
		[[nodiscard]] ParameterApplication ObserveImmediate(const ParameterDomainState& state) const noexcept;
		[[nodiscard]] ParameterPublicationObservation ObservePublication(
			const ParameterStateIdentity& identity, const ParameterSourceFrame& frame) const noexcept;

	private:
		RenderEnvironmentAccess& m_Environment;
		uint64_t m_FrameBaseline = 0;
		uint64_t m_FrameAttributedGeneration = 0;
	};
}
