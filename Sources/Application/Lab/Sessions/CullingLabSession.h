#pragma once
#include "Application/Lab/LabSessionBase.h"

namespace gglab
{
	class CullingLabSession final : public LabSessionBase
	{
	public:
		explicit CullingLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~CullingLabSession() override = default;

		void Update(float deltaTime) noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	protected:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;

	private:
		bool m_EnableCameraInput = true;
	};
}
