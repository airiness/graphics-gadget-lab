#pragma once
#include <cstdint>
#include <memory>

namespace gglab
{
	class RayScene;
	struct RaySceneDiagnostics;
	struct RenderFrameValidationResult;

	// Continuous split-sum prefilter-kernel visibility, not reflected radiance.
	// The scene must track primary material textures. Both diagnostics may share
	// this same scene; Forward+ imports it only once per frame.
	struct GISpecularVisibilityCreateInfo
	{
		std::shared_ptr<RayScene> m_RayScene;
		uint32_t m_SampleCount = 4096;
		uint32_t m_Seed = 61051;
		uint32_t m_SamplesPerBatch = 64;
		uint32_t m_QueryWorkPerSubmission = 8'388'608;
		uint32_t m_MaximumTilePixels = 0;
		float m_MaximumDistance = 1000.0f;
	};

	enum class GISpecularVisibilityStatus : uint8_t
	{
		Inactive,
		Running,
		Completed,
		Cancelled,
		Failed,
	};

	// Calculation progress only. Numerical sampling qualification is external.
	struct GISpecularVisibilityProgress
	{
		GISpecularVisibilityStatus m_Status = GISpecularVisibilityStatus::Inactive;
		uint64_t m_Generation = 0;
		uint64_t m_CommittedVersion = 0;
		uint32_t m_TargetSamples = 0;
		uint32_t m_SubmittedSamples = 0;
		uint32_t m_CompletedSamples = 0;
		uint32_t m_BatchSamples = 0;
		uint32_t m_SweepSamples = 0;
		uint32_t m_SubmittedTiles = 0;
		uint32_t m_TileCount = 0;
		bool m_Active = false;
	};

	[[nodiscard]] RenderFrameValidationResult ValidateGISpecularVisibilityInputs(
		const GISpecularVisibilityCreateInfo& info, const RaySceneDiagnostics& scene) noexcept;
}
