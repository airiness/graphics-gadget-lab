#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeUpdate.h"
#include "GGLabRuntime/Graphics/Pipeline/GIUpdateStatus.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"
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
		uint32_t m_IrradianceInteriorWidth = GIProbeUpdateLimits::DefaultIrradianceInteriorWidth;
		uint32_t m_RaysPerProbe = 256;
		uint32_t m_Seed = GIObservationLimits::DefaultSeed;
		float m_MaximumDistance = 30000;
		float m_Hysteresis = 0.97f;
		// Viewer-directed query offset, in probe spacings; normal bias remains 0.1.
		float m_ViewBias = 0.3f;
		// Update-only controls can inspect atlases without changing opaque lighting.
		bool m_EnableReceiverSampling = true;
		GIFieldReadinessSettings m_Readiness;
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
		GIFieldReadinessSnapshot m_Readiness;
		bool m_Initialized = false;
		bool m_Invalid = false;
		std::string m_Reason;
		std::vector<GIFieldAllocationSnapshot> m_Allocations;
	};

	[[nodiscard]] bool ValidateGIFieldCreateInfo(const GIFieldCreateInfo& info) noexcept;

	// Runtime-owned static world field. Every submitted update covers the whole volume.
	// Configure/replace only through the safe pipeline path on the owner thread.
	// Static scene and shader inputs require replacement. With readiness enabled,
	// published lighting changes retain history and restart irradiance-only refresh;
	// fixed-settling diagnostics retain the sealed-lighting contract. Lookup remains
	// single-bounce; observed readiness does not establish reference convergence.
	// Numerical failure rejects production preparation; update-only owners can retain
	// raw evidence. A GPU failure flag prevents sampling invalid in-place contents.
	class GIField final
	{
	public:
		explicit GIField(GIFieldCreateInfo info) noexcept;
		~GIField();
		GGLAB_DELETE_COPYABLE_MOVABLE(GIField);
		[[nodiscard]] std::shared_ptr<RayScene> GetRayScene() const noexcept;
		[[nodiscard]] GIFieldDiagnostics GetDiagnostics(bool allocations = false) const noexcept;
		[[nodiscard]] GIFieldCaptureState GetCaptureState() const noexcept;
		void SetProbeInspection(std::optional<GIProbeInspectionRequest> request) noexcept;
		void AddProbeInspection(RenderGraph& graph, const RenderFrameContext& frame) noexcept;
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services) noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene) noexcept;
		// Optional diagnostics consume the recipe's existing private receiver records.
		void AddReceiverDiagnostics(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services) noexcept;
		// Close graph access only after all opaque/diagnostic consumers declare reads.
		void ExportResources(RenderGraph& graph) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
