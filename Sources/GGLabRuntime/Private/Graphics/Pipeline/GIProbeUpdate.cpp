#include "GGLabRuntime/Graphics/Pipeline/GIProbeUpdate.h"
#include "Graphics/Pipeline/GIProbeUpdateWorkload.h"

#include <cmath>
#include <utility>

namespace gglab
{
	bool ValidateGIProbeUpdateProfile(const GIProbeUpdateProfile& profile) noexcept
	{
		uint64_t count = 1;
		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			if (profile.m_Counts[axis] == 0 || profile.m_Counts[axis] > GIProbeUpdateLimits::MaximumAxisProbes ||
				!std::isfinite(profile.m_Minimum[axis]) || !std::isfinite(profile.m_ConstantRadiance[axis]) ||
				profile.m_ConstantRadiance[axis] < 0)
			{
				return false;
			}
			count *= profile.m_Counts[axis];
		}
		return count >= profile.m_UpdateDivisor && count <= GIProbeUpdateLimits::MaximumProbeCount &&
			profile.m_Counts[1] <= GIProbeUpdateLimits::MaximumArrayLayers &&
			profile.m_RaysPerProbe >= GIProbeUpdateLimits::RayCountStep &&
			profile.m_RaysPerProbe <= GIProbeUpdateLimits::MaximumRaysPerProbe &&
			profile.m_RaysPerProbe % GIProbeUpdateLimits::RayCountStep == 0 &&
			(profile.m_UpdateDivisor == GIProbeUpdateLimits::FullUpdateDivisor ||
				profile.m_UpdateDivisor == GIProbeUpdateLimits::PartialUpdateDivisor) &&
			std::isfinite(profile.m_Spacing) && profile.m_Spacing > 0 &&
			std::isfinite(profile.m_MaximumDistance) && profile.m_MaximumDistance > 0 &&
			std::isfinite(profile.m_Hysteresis) && profile.m_Hysteresis >= 0 && profile.m_Hysteresis < 1 &&
			profile.m_MaximumUpdates <= GIProbeUpdateLimits::MaximumUpdateCount &&
			profile.m_RefreshPeriod <= GIProbeUpdateLimits::MaximumUpdateCount;
	}

	GIProbeUpdateRange GetGIProbeUpdateRange(const GIProbeUpdateProfile& profile, uint32_t update) noexcept
	{
		if (!ValidateGIProbeUpdateProfile(profile))
		{
			return {};
		}
		const uint32_t total = profile.m_Counts[0] * profile.m_Counts[1] * profile.m_Counts[2];
		const uint32_t divisor = profile.m_RefreshPeriod != 0 && update % profile.m_RefreshPeriod == 0 ?
			1u :
			profile.m_UpdateDivisor;
		const uint32_t slot = update % divisor;
		const uint32_t begin = slot * total / divisor;
		return { begin, (slot + 1) * total / divisor - begin };
	}

	struct GIProbeUpdateExperiment::State
	{
		GIProbeUpdateWorkload m_Workload;
		State(std::shared_ptr<RayScene> scene, GIProbeUpdateProfile profile) noexcept :
			m_Workload(std::move(scene), profile) {}
	};

	GIProbeUpdateExperiment::GIProbeUpdateExperiment(std::shared_ptr<RayScene> scene,
		GIProbeUpdateProfile profile) noexcept :
		m_State(std::make_unique<State>(std::move(scene), profile)) {}
	GIProbeUpdateExperiment::~GIProbeUpdateExperiment() = default;
	std::shared_ptr<RayScene> GIProbeUpdateExperiment::GetRayScene() const noexcept
	{
		return m_State->m_Workload.GetRayScene();
	}
	GIProbeUpdateDiagnostics GIProbeUpdateExperiment::GetDiagnostics(bool allocations) const noexcept
	{
		return m_State->m_Workload.GetDiagnostics(allocations);
	}
	RenderFrameValidationResult GIProbeUpdateExperiment::Prepare(const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		return m_State->m_Workload.Prepare(frame, services);
	}
	void GIProbeUpdateExperiment::AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene) noexcept
	{
		m_State->m_Workload.AddPasses(graph, frame, services, scene);
	}
}
