#pragma once
#include "AssetPreparationTracker.h"
#include "Lab/LabSessionBase.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	// Surface integration probe: proves the gglab.surface seam end-to-end on
	// the real Forward PBR path. Fixture A/B values and textures are driven
	// through the normal runtime material update path
	// (MaterialInstanceComponent -> MaterialGPU -> g_Materials ->
	//  EvaluateSurface -> existing Forward PBR lighting).
	class SurfaceProbeLabSession final : public LabSessionBase
	{
	public:
		explicit SurfaceProbeLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~SurfaceProbeLabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_LoadingProgress;
		}
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		void BuildLighting() noexcept;
		void ApplyCameraPreset() noexcept;
		void ApplyProbeFixtures() noexcept;
		bool ProbeMaterialMatchesFixtures(const MaterialProperties& properties,
			int32_t factorIndex, int32_t textureIndex, MaterialDebugView debugView) const noexcept;

		bool m_EnableCameraInput = false;
		entt::entity m_ProbeEntity = entt::null;
		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
		bool m_FixtureConfigured = false;
	};
}
