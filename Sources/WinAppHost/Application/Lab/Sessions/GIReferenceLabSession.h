#pragma once
#include "AssetPreparationTracker.h"
#include "Application/Lab/Sessions/GIReferenceLabFixture.h"
#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingOverrideBase.h"

#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <entt/entity/entity.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <map>
#include <string>

namespace gglab
{
	class RayScene;
	class GIProbeUpdateExperiment;

	class GIReferenceLabSession final : public LabSessionBase
	{
	public:
		explicit GIReferenceLabSession(const LabSessionCreateInfo& info) noexcept;
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_Progress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		[[nodiscard]] bool SupportsImmediateParameter(const LabParameterId& id) const noexcept override;
		[[nodiscard]] std::optional<LabValue> ReadImmediateParameterFrameValue(
			const LabParameterId& id, const LabParameterFrameContext& frame) const noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept override;
		std::optional<FrameCaptureAssetBundle> GetCaptureAssetBundle() const noexcept override
		{
			return m_CaptureBundle;
		}
		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& info) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void RecreatePipeline() noexcept override;
		std::shared_ptr<GIProbeUpdateExperiment> m_ProbeUpdate;
		bool m_RestoreProbeProfiling = false;
		bool m_PreviousProbeProfiling = false;
		uint64_t m_FirstProbeFrame = 0;
		uint64_t m_LastProbeFrame = 0;
		uint64_t m_ActivationGpuFrame = 0;
		uint64_t m_InitializationGpuFrame = 0;
		double m_InitializationGpuMilliseconds = 0;
		std::map<std::string, std::pair<uint32_t, double>> m_ActivationGpu;
		entt::entity m_ProbeEntity = entt::null;
		void BuildMaterialContract() noexcept;
		void BuildVisibilityContract() noexcept;
		void ApplyVisibilityMaterial() noexcept;
		entt::entity m_VisibilityReceiver = entt::null;
		std::array<TextureID, static_cast<size_t>(GIVisibilityTexture::Count)> m_VisibilityTextures{};
		ModelID m_VisibilityNormalModel{};
		std::optional<FrameCaptureAssetBundle> m_CaptureBundle;
		void UpdateMaterialProgress() noexcept;
		std::shared_ptr<RayScene> m_RayScene;
		std::array<TextureID, static_cast<size_t>(GIMaterialTexture::Count)> m_MaterialTextures{};
		std::array<ModelID, 2> m_MaterialModels{};
		entt::entity m_MaterialWall = entt::null;
		uint32_t m_MaterialCase = 0;
		uint64_t m_LoggedSnapshot = 0;
		AssetPreparationTracker m_Assets;
		LoadingProgress m_Progress;
		std::unique_ptr<EnvironmentLightingOverrideBase> m_LightingOverride;
		std::filesystem::path m_PreviousEnvironmentFile;
		bool m_SelectedEnvironment = false;
	};
}
