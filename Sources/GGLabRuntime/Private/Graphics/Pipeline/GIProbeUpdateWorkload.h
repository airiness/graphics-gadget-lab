#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIProbeUpdate.h"

#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"

#include <cstddef>
#include <cstdint>

namespace gglab
{
	class GIFieldUpdateState;

	// A new request must not pause an existing field or sample staging textures.
	// Before the first accepted update, wait for initial publication as before.
	[[nodiscard]] constexpr bool CanUpdateGIPublishedEnvironment(bool hasActiveBake,
		uint64_t activeGeneration, uint64_t requestedGeneration, uint64_t builtGeneration,
		bool hasSubmittedHistory) noexcept
	{
		return hasActiveBake && activeGeneration != 0 && activeGeneration == builtGeneration &&
			(hasSubmittedHistory || activeGeneration == requestedGeneration);
	}

	// Shared GPU implementation. Experimental controls stay behind the experiment facade;
	// GIField supplies a full-update profile and a separate settlement participant.
	class GIProbeUpdateWorkload final
	{
	public:
		static constexpr size_t ProgramCount = 8;
		GIProbeUpdateWorkload(std::shared_ptr<RayScene> scene, GIProbeUpdateProfile profile,
			bool field = false, GIFieldReadinessSettings readiness = {}, bool feedback = false,
			bool progressiveSampling = false, bool readinessTrace = false) noexcept;
		~GIProbeUpdateWorkload();
		[[nodiscard]] std::shared_ptr<RayScene> GetRayScene() const noexcept;
		[[nodiscard]] GIProbeUpdateDiagnostics GetDiagnostics(bool allocations = false) const noexcept;
		[[nodiscard]] std::array<uint64_t, ProgramCount> GetShaderGenerations() const noexcept;
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services) noexcept;
		[[nodiscard]] GIFieldReadinessSnapshot GetReadiness() const noexcept;
		[[nodiscard]] GIFieldFeedbackSnapshot GetFeedback() const noexcept;
		[[nodiscard]] std::shared_ptr<const GIFieldChangeObservation> GetChangeObservation() const noexcept;
		[[nodiscard]] std::optional<GIObservationStamp> GetCompletedObservation() const noexcept;
		[[nodiscard]] uint64_t GetRefreshEpoch() const noexcept;
		[[nodiscard]] uint64_t GetEpochOriginUpdate() const noexcept;
		void RefreshLighting() noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene,
			std::shared_ptr<GIFieldUpdateState> fieldUpdate = {}, bool compose = false,
			float viewBias = 0.3f, GIFieldIdentity identity = {},
			std::optional<GIProbeInspectionRequest> inspection = {}) noexcept;
		void ExportFieldResources(RenderGraph& graph) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
