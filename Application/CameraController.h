#pragma once
#include "Camera.h"

namespace gglab
{
	struct CameraInput
	{
		bool m_Front = false;
		bool m_Back = false;
		bool m_Left = false;
		bool m_Right = false;
		bool m_Up = false;
		bool m_Down = false;
		bool m_Accelerate = false;

		bool m_IsMouseRelative = false;

		Vector2 m_MouseDelta = Vector2::Zero;
	};

	class CameraController
	{
	public:
		struct Params
		{
			float m_MovementSpeed = 10.0f;
			float m_RotationSpeed = 0.15f;
			float m_AccelerateMultiplier = 3.0f;
			float m_SmoothStepT = 0.5f;
		};

		struct CreateInfo
		{
			Params m_Params{};
		};

	public:
		explicit CameraController(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(CameraController);
		~CameraController() = default;

		void Update(Camera& camera, const CameraInput& input, float deltaTime) noexcept;

		const Params& GetParams() const noexcept { return m_Params; }
		void SetParams(const Params& params) noexcept { m_Params = Sanitize(params); }

		float GetMovementSpeed() const noexcept { return m_Params.m_MovementSpeed; }
		float GetRotationSpeed() const noexcept { return m_Params.m_RotationSpeed; }
		float GetAccelerateMultiplier() const noexcept { return m_Params.m_AccelerateMultiplier; }
		float GetSmoothStepT() const noexcept { return m_Params.m_SmoothStepT; }

		void SetMovementSpeed(float value) noexcept { m_Params.m_MovementSpeed = value; }
		void SetRotationSpeed(float value) noexcept { m_Params.m_RotationSpeed = value; }
		void SetAccelerateMultiplier(float value) noexcept { m_Params.m_AccelerateMultiplier = value; }
		void SetSmoothStepT(float value) noexcept { m_Params.m_SmoothStepT = value; }

		void ResetVelocity() noexcept { m_Velocity = Vector3::Zero; }

	private:
		static Params Sanitize(Params params) noexcept;

		static Vector3 BuildMovementVector(const CameraInput& input) noexcept;

	private:
		Vector3 m_Velocity = Vector3::Zero;
		Params m_Params{};
	};
}