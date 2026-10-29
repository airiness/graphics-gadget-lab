#pragma once
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"

#include <array>
#include <cstdint>
#include <memory>

namespace gglab
{
	class RayScene;
	struct RaySceneDiagnostics;

	enum class GIReferenceEnvironmentFilter : uint32_t
	{
		Face32,
		MipZero,
	};

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
		// Published radiance cubemap signal; ignored for direct constant inputs.
		// Face32 is the declared default, MipZero measures filtering bias.
		GIReferenceEnvironmentFilter m_EnvironmentFilter = GIReferenceEnvironmentFilter::Face32;
		// Total observation samples are independent of submission partitioning.
		uint32_t m_SamplesPerBatch = 64;
		// Maximum width * height * batch samples. Extents exceeding this reject.
		uint32_t m_SampleWorkPerSubmission = 8'388'608;
	};

	[[nodiscard]] RenderFrameValidationResult ValidateGIReferenceInputs(
		const GIReferenceCreateInfo& info, const RaySceneDiagnostics& scene) noexcept;
}
