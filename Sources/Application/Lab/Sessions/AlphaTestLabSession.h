#pragma once
#include "Application/Lab/LabSessionBase.h"

namespace gglab
{
	class AlphaTestLabSession final : public LabSessionBase
	{
	public:
		explicit AlphaTestLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AlphaTestLabSession() override = default;

		void Update(float deltaTime) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void BuildLighting() noexcept;
		void ApplyCameraPreset() noexcept;

		bool m_EnableCameraInput = true;
	};
}
