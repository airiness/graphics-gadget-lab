#include "Parameters/LightingParameterDomain.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

namespace gglab
{
	void LightingParameterDomain::BeginFrame(ParameterDomainState& state) const noexcept
	{
		state.m_Baseline = m_Environment.GetBakeRequestGeneration();
	}

	void LightingParameterDomain::CompleteEffects(ParameterDomainState& state) const noexcept
	{
		const uint64_t requested = m_Environment.GetBakeRequestGeneration();
		state.m_Applied = requested != state.m_Baseline ? requested : 0;
		state.m_Attributed = false;
	}

	ParameterApplication LightingParameterDomain::Translate(const LightingMutationResult& evidence,
		bool unchanged, ParameterDomainState& state) const noexcept
	{
		const uint64_t requested = m_Environment.GetBakeRequestGeneration();
		const bool newTarget = evidence.m_Outcome == LightingMutationOutcome::Requested &&
			evidence.m_Generation > evidence.m_PreviousGeneration;
		const bool existing = evidence.m_Outcome == LightingMutationOutcome::Unchanged &&
			evidence.m_Generation == evidence.m_PreviousGeneration;
		if ((!newTarget && !existing) || evidence.m_PreviousGeneration != state.m_Baseline ||
			evidence.m_Generation != requested || requested == 0)
		{
			return { .m_Failure =
				"LightingEffectUnproven: the current operation did not bind the Runtime target at this boundary." };
		}
		state.m_Attributed = true;
		state.m_Applied = requested;
		return { .m_ActualClass = ParameterEffectClass::Deferred,
			.m_Identity = { .m_Domain = "lighting", .m_Generation = requested },
			.m_NoChange = unchanged && existing && m_Environment.GetBakingStatus().m_ActiveGeneration == requested };
	}

	ParameterApplication LightingParameterDomain::ObserveImmediate(const ParameterDomainState& state) const noexcept
	{
		if (state.m_Applied != 0 && !state.m_Attributed)
		{
			return { .m_ActualClass = ParameterEffectClass::Deferred,
				.m_Identity = { .m_Domain = "lighting", .m_Generation = state.m_Applied } };
		}
		const uint64_t requested = m_Environment.GetBakeRequestGeneration();
		if (requested != state.m_Baseline && (!state.m_Attributed || requested != state.m_Applied))
		{
			return { .m_Failure = "UnexpectedLightingTransition: effect attribution is unavailable for this frame." };
		}
		return {};
	}

	ParameterPublicationObservation LightingParameterDomain::ObservePublication(
		const ParameterStateIdentity& identity, const ParameterSourceFrame& frame) const noexcept
	{
		if (frame.m_LightingGeneration != 0 && frame.m_LightingGeneration == identity.m_Generation)
		{
			return { ParameterPublication::Published };
		}
		if (m_Environment.GetBakeRequestGeneration() != identity.m_Generation)
		{
			return { ParameterPublication::Superseded };
		}
		if (m_Environment.GetBakingStatus().m_Stage == IBLBakeStage::Failed)
		{
			return { ParameterPublication::Failed, "LightingPublicationFailed" };
		}
		return {};
	}
}
