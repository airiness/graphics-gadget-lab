#include "GGLabRuntime/Graphics/Pipeline/GIField.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "Graphics/Pipeline/GIFieldState.h"
#include "Graphics/Pipeline/GIProbeUpdateWorkload.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <utility>

namespace gglab
{
	namespace
	{
		std::atomic<uint64_t> NextGeneration{ 1 };

		GIProbeUpdateProfile UpdateProfile(const GIFieldCreateInfo& info) noexcept
		{
			GIProbeUpdateProfile result;
			result.m_Counts = info.m_Volume.m_Counts;
			result.m_Minimum = info.m_Volume.m_Minimum;
			result.m_Spacing = info.m_Volume.m_Spacing;
			result.m_RaysPerProbe = info.m_RaysPerProbe;
			result.m_Seed = info.m_Seed;
			result.m_MaximumDistance = info.m_MaximumDistance;
			result.m_Hysteresis = info.m_Hysteresis;
			result.m_MaximumUpdates = 0;
			result.m_ConstantEnvironment = info.m_ConstantEnvironment;
			result.m_ConstantRadiance = info.m_ConstantRadiance;
			return result;
		}
	}

	bool ValidateGIFieldCreateInfo(const GIFieldCreateInfo& info) noexcept
	{
		if (!info.m_RayScene || !ValidateGIProbeUpdateProfile(UpdateProfile(info)))
		{
			return false;
		}
		for (size_t axis = 0; axis < 3; ++axis)
		{
			const float extent = float(info.m_Volume.m_Counts[axis] - 1) * info.m_Volume.m_Spacing;
			if (info.m_Volume.m_Counts[axis] < 2 || !std::isfinite(extent) ||
				!std::isfinite(info.m_Volume.m_Minimum[axis] + extent) ||
				info.m_Volume.m_Minimum[axis] + info.m_Volume.m_Spacing <= info.m_Volume.m_Minimum[axis])
			{
				return false;
			}
		}
		return true;
	}

	struct GIField::State
	{
		GIFieldCreateInfo m_Info;
		GIProbeUpdateWorkload m_Workload;
		std::shared_ptr<GIFieldUpdateState> m_Update = std::make_shared<GIFieldUpdateState>();
		uint64_t m_Generation = NextGeneration.fetch_add(1, std::memory_order_relaxed);
		uint64_t m_SceneRevision = 0;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		std::array<uint64_t, GIProbeUpdateWorkload::ProgramCount> m_ShaderGenerations{};
		bool m_Sealed = false;
		bool m_UpdatePrepared = false;
		State(GIFieldCreateInfo info) noexcept :
			m_Info(std::move(info)), m_Workload(m_Info.m_RayScene, UpdateProfile(m_Info), true) {}
	};

	GIField::GIField(GIFieldCreateInfo info) noexcept :
		m_State(std::make_unique<State>(std::move(info))) {}
	GIField::~GIField() = default;
	std::shared_ptr<RayScene> GIField::GetRayScene() const noexcept { return m_State->m_Info.m_RayScene; }

	GIFieldDiagnostics GIField::GetDiagnostics(bool allocations) const noexcept
	{
		const auto& state = *m_State;
		const auto update = state.m_Workload.GetDiagnostics(allocations);
		GIFieldDiagnostics result{
			.m_Generation = state.m_Generation,
			.m_SubmittedUpdates = update.m_SubmittedUpdates,
			.m_CompletedUpdates = update.m_CompletedUpdates,
			.m_SceneRevision = state.m_SceneRevision,
			.m_LightingGeneration = state.m_LightingGeneration,
			.m_EnvironmentGeneration = state.m_EnvironmentGeneration,
			.m_FieldLogicalBytes = update.m_FieldLogicalBytes,
			.m_RayCapacityBytes = update.m_RayCapacityBytes,
			.m_ProbeCount = update.m_ProbeCount,
			.m_StatusReadbackSlots = update.m_StatusReadbackSlots,
			.m_UpdateStatus = update.m_UpdateStatus,
			.m_Initialized = update.m_Initialized,
			.m_Invalid = state.m_Update->IsInvalid() || update.m_Failed,
			.m_Reason = state.m_Update->IsInvalid() ? "GI field submission failed or completion is unknown" : update.m_Reason,
		};
		for (const auto& allocation : update.m_Allocations)
		{
			result.m_Allocations.push_back({ allocation.m_LogicalBytes, allocation.m_Allocation });
		}
		return result;
	}

	RenderFrameValidationResult GIField::Prepare(const RenderFrameContext& frame,
		const RenderServices& services) noexcept
	{
		auto& state = *m_State;
		state.m_UpdatePrepared = false;
		if (!ValidateGIFieldCreateInfo(state.m_Info) || !services.m_GIFieldFrameState)
		{
			return RenderFrameValidationResult::ContractFailure("Invalid GI field descriptor or settlement service");
		}
		if (state.m_Update->IsInvalid())
		{
			return RenderFrameValidationResult::ContractFailure("GI field invalidated by failed or unknown submission");
		}
		if (!state.m_Info.m_ConstantEnvironment)
		{
			const auto bake = services.m_Environment->GetBakingStatus();
			if (!bake.m_HasActiveBake || bake.m_ActiveGeneration != services.m_Environment->GetBakeRequestGeneration())
			{
				// IBL baking itself needs this frame to render. Skip only field work,
				// otherwise waiting for publication prevents the producer from advancing.
				return RenderFrameValidationResult::Ready();
			}
		}
		const auto result = state.m_Workload.Prepare(frame, services);
		if (!result.IsReady())
		{
			return result;
		}
		const auto revision = state.m_Info.m_RayScene->GetDiagnostics().m_Revision;
		const auto lighting = frame.m_RenderScene.m_WorldLightingGeneration;
		const uint64_t environment = state.m_Info.m_ConstantEnvironment ?
			0 : services.m_Environment->GetBakingStatus().m_ActiveGeneration;
		const auto shaders = state.m_Workload.GetShaderGenerations();
		if (state.m_Sealed && (revision != state.m_SceneRevision || lighting != state.m_LightingGeneration ||
			environment != state.m_EnvironmentGeneration || shaders != state.m_ShaderGenerations))
		{
			return RenderFrameValidationResult::ContractFailure(
				"Static GI field inputs changed; replace the field through the safe pipeline path");
		}
		state.m_SceneRevision = revision;
		state.m_LightingGeneration = lighting;
		state.m_EnvironmentGeneration = environment;
		state.m_ShaderGenerations = shaders;
		state.m_Sealed = true;
		state.m_UpdatePrepared = true;
		return RenderFrameValidationResult::Ready();
	}

	void GIField::AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene) noexcept
	{
		if (!m_State->m_UpdatePrepared)
		{
			return;
		}
		const bool tracked = services.m_GIFieldFrameState->Track(m_State->m_Update, frame.m_FrameSerial);
		GGLAB_ASSERT_MSG(tracked, "A GI field must participate in exactly one matching renderer frame.");
		m_State->m_Workload.AddPasses(graph, frame, services, scene, m_State->m_Update);
	}
}
