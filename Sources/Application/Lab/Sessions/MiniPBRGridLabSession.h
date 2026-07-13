#pragma once
#include "Application/Lab/LabSessionBase.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	class MiniPBRGridLabSession final : public LabSessionBase
	{
	public:
		explicit MiniPBRGridLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~MiniPBRGridLabSession() override = default;

		void Update(float deltaTime) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void BuildProceduralGrid() noexcept;
		bool BuildAssetModel(std::string_view path) noexcept;
		void TryFinalizeAssetModel() noexcept;
		void BuildLighting() noexcept;
		void ApplyCameraPreset() noexcept;

		bool m_EnableCameraInput = true;
		ModelID m_PendingModelId{};
		std::string m_PendingModelPath;
	};
}
