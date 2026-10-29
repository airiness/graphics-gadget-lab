#pragma once
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"

#include <array>
#include <cstdint>
#include <memory>

namespace gglab
{
	class RayScene;
	struct RaySceneDiagnostics;

	// Opt-in Runtime reference configuration. The owner prepares this all-World ray
	// scene before Forward+ frame validation. The recipe retains it and owns its graph
	// imports and queries; do not import the same scene separately in that frame.
	struct GIReferenceCreateInfo
	{
		std::shared_ptr<RayScene> m_RayScene;
		uint32_t m_SampleCount = 64;
		uint32_t m_Seed = 61051;
		bool m_ConstantEnvironment = true;
		std::array<float, 3> m_ConstantRadiance{ 1.0f, 1.0f, 1.0f };
		float m_MaximumDistance = 1000.0f;
	};

	[[nodiscard]] RenderFrameValidationResult ValidateGIReferenceInputs(
		const GIReferenceCreateInfo& info, const RaySceneDiagnostics& scene) noexcept;
}
