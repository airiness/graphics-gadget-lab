#pragma once
#include "AssetPreparationTracker.h"
#include "Graphics/GraphicsTypes.h"
#include "Lab/LabSessionBase.h"

#include <memory>

namespace gglab
{
	struct ShaderGraphPreviewLabState;

	class ShaderGraphPreviewLabSession final : public LabSessionBase
	{
	public:
		explicit ShaderGraphPreviewLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~ShaderGraphPreviewLabSession() override = default;

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
		void RecreatePipeline() noexcept override;
		void ApplyCameraPreset() noexcept;
		void ApplyPrimitiveVisibility() noexcept;

		std::shared_ptr<ShaderGraphPreviewLabState> m_State;
		bool m_EnableCameraInput = false;
		entt::entity m_SphereEntity = entt::null;
		entt::entity m_PlaneEntity = entt::null;
		entt::entity m_CubeEntity = entt::null;
		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
		bool m_PrimitivesConstructed = false;
	};
}
