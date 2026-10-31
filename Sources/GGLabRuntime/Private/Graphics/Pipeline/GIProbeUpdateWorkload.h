#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIProbeUpdate.h"

#include <cstddef>

namespace gglab
{
	class GIFieldUpdateState;

	// Shared GPU implementation. Experimental controls stay behind the experiment facade;
	// GIField supplies a full-update profile and a separate settlement participant.
	class GIProbeUpdateWorkload final
	{
	public:
		static constexpr size_t ProgramCount = 5;
		GIProbeUpdateWorkload(std::shared_ptr<RayScene> scene, GIProbeUpdateProfile profile,
			bool field = false) noexcept;
		~GIProbeUpdateWorkload();
		[[nodiscard]] std::shared_ptr<RayScene> GetRayScene() const noexcept;
		[[nodiscard]] GIProbeUpdateDiagnostics GetDiagnostics(bool allocations = false) const noexcept;
		[[nodiscard]] std::array<uint64_t, ProgramCount> GetShaderGenerations() const noexcept;
		[[nodiscard]] RenderFrameValidationResult Prepare(const RenderFrameContext& frame,
			const RenderServices& services) noexcept;
		void AddPasses(RenderGraph& graph, const RenderFrameContext& frame,
			const RenderServices& services, const RaySceneGraphResources& scene,
			std::shared_ptr<GIFieldUpdateState> fieldUpdate = {}) noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
