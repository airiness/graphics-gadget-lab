#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/Pipeline/GIUpdateStatus.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	class RayScene;
	struct RaySceneGraphResources;

	struct GIProbeUpdateLimits
	{
		static constexpr uint32_t MaximumAxisProbes = 128;
		static constexpr uint32_t MaximumProbeCount = 65535; // One dispatch group per probe.
		static constexpr uint32_t MaximumArrayLayers = 32;
		static constexpr uint32_t RayCountStep = 64;
		static constexpr uint32_t MaximumRaysPerProbe = 256;
		static constexpr uint32_t FullUpdateDivisor = 1;
		static constexpr uint32_t PartialUpdateDivisor = 4;
		static constexpr uint32_t MaximumUpdateCount = 1'000'000;
	};

	// Experimental update workload, independent of production field sampling/readiness.
	struct GIProbeUpdateProfile
	{
		std::array<uint32_t, 3> m_Counts{ 33, 10, 39 };
		std::array<float, 3> m_Minimum{ -17.25f, -1.5f, -23.25f };
		float m_Spacing = 1;
		uint32_t m_RaysPerProbe = GIProbeUpdateLimits::MaximumRaysPerProbe;
		uint32_t m_UpdateDivisor = GIProbeUpdateLimits::FullUpdateDivisor;
		// A declared full refresh every N updates; zero keeps the fixed divisor.
		uint32_t m_RefreshPeriod = 0;
		// Zero continuously records updates for quiet timing; bounded runs stop exactly.
		uint32_t m_MaximumUpdates = 1;
		uint32_t m_Seed = GIObservationLimits::DefaultSeed;
		bool m_SharedBlend = true;
		bool m_ConstantEnvironment = false;
		bool m_TraverseScene = true;
		// A separate traversal-only control; its difference is not an isolated shade timer.
		bool m_TraversalOnly = false;
		bool m_RotateDirections = true;
		std::array<float, 3> m_ConstantRadiance{ 1, 1, 1 };
		float m_MaximumDistance = 30000;
		float m_Hysteresis = 0.97f;
	};

	struct GIProbeAllocationSnapshot
	{
		uint64_t m_LogicalBytes = 0;
		RHIAllocationFacts m_Allocation;
	};

	struct GIProbeUpdateDiagnostics
	{
		uint64_t m_SubmittedUpdates = 0;
		uint64_t m_CompletedUpdates = 0;
		uint32_t m_ProbeCount = 0;
		uint32_t m_StatusReadbackSlots = 0;
		GIUpdateStatus m_UpdateStatus;
		uint64_t m_RayCapacityBytes = 0;
		uint64_t m_FieldLogicalBytes = 0;
		uint64_t m_WorldLightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		bool m_AtmosphereAttenuation = false;
		double m_PreparationMilliseconds = 0;
		double m_AllocationMilliseconds = 0;
		double m_ShaderLookupMilliseconds = 0;
		double m_PipelineMilliseconds = 0;
		bool m_Initialized = false;
		bool m_Failed = false;
		std::string m_Reason;
		std::vector<GIProbeAllocationSnapshot> m_Allocations;
	};

	struct GIProbeUpdateRange
	{
		uint32_t m_First = 0;
		uint32_t m_Count = 0;
	};
	[[nodiscard]] GIProbeUpdateRange GetGIProbeUpdateRange(
		const GIProbeUpdateProfile& profile, uint32_t update) noexcept;

	[[nodiscard]] bool ValidateGIProbeUpdateProfile(const GIProbeUpdateProfile& profile) noexcept;

	// Owner-thread configuration and immutable diagnostics. GPU owners are retained by
	// command submissions; deferred commits capture only the independent CPU progress.
	class GIProbeUpdateExperiment final
	{
	public:
		GIProbeUpdateExperiment(std::shared_ptr<RayScene> scene, GIProbeUpdateProfile profile) noexcept;
		~GIProbeUpdateExperiment();
		GGLAB_DELETE_COPYABLE_MOVABLE(GIProbeUpdateExperiment);
		[[nodiscard]] std::shared_ptr<RayScene> GetRayScene() const noexcept;
		[[nodiscard]] GIProbeUpdateDiagnostics GetDiagnostics(bool includeAllocations = false) const noexcept;
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services) noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
