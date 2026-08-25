#pragma once
#include "Lab/LabSessionBase.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	class MiniPBRGridLabSession final : public LabSessionBase
	{
	public:
		explicit MiniPBRGridLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~MiniPBRGridLabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_LoadingProgress;
		}
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void Update(float deltaTime) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		void BuildProceduralGridRow(uint32_t row) noexcept;
		bool BuildAssetModel(std::string_view path) noexcept;
		bool FinalizeAssetModel() noexcept;
		void BuildLighting() noexcept;
		void ApplyCameraPreset() noexcept;

		enum class PrepareMode : uint8_t
		{
			None,
			ProceduralGrid,
			AssetModel,
		};

		bool m_EnableCameraInput = true;
		PrepareMode m_PrepareMode = PrepareMode::None;
		uint32_t m_NextGridRow = 0;
		ModelID m_PendingModelId{};
		std::string m_PendingModelPath;
		LoadingProgress m_LoadingProgress{};
	};
}
