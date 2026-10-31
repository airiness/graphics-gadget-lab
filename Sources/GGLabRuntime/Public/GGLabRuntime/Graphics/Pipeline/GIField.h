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

	struct GIFieldVolumeDescriptor
	{
		std::array<uint32_t, 3> m_Counts{ 33, 10, 39 };
		std::array<float, 3> m_Minimum{ -17.25f, -1.5f, -23.25f };
		float m_Spacing = 1;
	};

	struct GIFieldCreateInfo
	{
		std::shared_ptr<RayScene> m_RayScene;
		GIFieldVolumeDescriptor m_Volume;
		uint32_t m_RaysPerProbe = 256;
		uint32_t m_Seed = GIObservationLimits::DefaultSeed;
		float m_MaximumDistance = 30000;
		float m_Hysteresis = 0.97f;
		// Analytic fixtures may supply incident constant radiance; geometry is still traced.
		bool m_ConstantEnvironment = false;
		std::array<float, 3> m_ConstantRadiance{ 1, 1, 1 };
	};

	struct GIFieldAllocationSnapshot
	{
		uint64_t m_LogicalBytes = 0;
		RHIAllocationFacts m_Allocation;
	};

	struct GIFieldDiagnostics
	{
		// Runtime identity, never a persisted profile or artifact identity.
		uint64_t m_Generation = 0;
		uint64_t m_SubmittedUpdates = 0;
		uint64_t m_CompletedUpdates = 0;
		uint64_t m_SceneRevision = 0;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_EnvironmentGeneration = 0;
		uint64_t m_FieldLogicalBytes = 0;
		uint64_t m_RayCapacityBytes = 0;
		uint32_t m_ProbeCount = 0;
		uint32_t m_StatusReadbackSlots = 0;
		GIUpdateStatus m_UpdateStatus;
		bool m_Initialized = false;
		bool m_Invalid = false;
		std::string m_Reason;
		std::vector<GIFieldAllocationSnapshot> m_Allocations;
	};

	[[nodiscard]] bool ValidateGIFieldCreateInfo(const GIFieldCreateInfo& info) noexcept;

	// Runtime-owned static world field. Every submitted update covers the whole volume.
	// Configure/replace only through the safe pipeline path on the owner thread.
	// Static scene, lighting and shader inputs are sealed at the first update; changing
	// them requires replacement. This owner does not yet provide lookup or readiness.
	class GIField final
	{
	public:
		explicit GIField(GIFieldCreateInfo info) noexcept;
		~GIField();
		GGLAB_DELETE_COPYABLE_MOVABLE(GIField);
		[[nodiscard]] std::shared_ptr<RayScene> GetRayScene() const noexcept;
		[[nodiscard]] GIFieldDiagnostics GetDiagnostics(bool allocations = false) const noexcept;
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services) noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
