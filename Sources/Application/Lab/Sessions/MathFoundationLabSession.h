#pragma once
#include "Application/Lab/LabSessionBase.h"

namespace gglab
{
	class DebugDrawContext;

	class MathFoundationLabSession final : public LabSessionBase
	{
	public:
		explicit MathFoundationLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~MathFoundationLabSession() override = default;

		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void ApplyCameraPreset() noexcept;
		void DrawVectorValidation(DebugDrawContext& debugDraw) noexcept;
		void DrawMatrixValidation(DebugDrawContext& debugDraw) noexcept;
		void DrawQuaternionValidation(DebugDrawContext& debugDraw, float interpolationT) noexcept;
		void DrawBoundsValidation(DebugDrawContext& debugDraw) noexcept;
		void DrawProjectionValidation(DebugDrawContext& debugDraw) noexcept;
		void DrawDegenerateValidation(DebugDrawContext& debugDraw) noexcept;

		float m_ElapsedTime = 0.0f;
		bool m_EnableCameraInput = true;
	};
}
