#pragma once
#include "Core/Utility/MathUtils.h"

namespace gglab
{
	class Camera
	{
	public:
		struct CreateInfo
		{
			Vector3 m_Forward = Vector3::UnitZ;
			Vector3 m_Position = Vector3::Zero;

			float m_Near = 0.01f;
			float m_Far = 1000.0f;
			float m_Fov = 60.0f;

			uint32_t m_Width = 1280;
			uint32_t m_Height = 720;
		};

	public:
		explicit Camera(const CreateInfo& info) noexcept;
		~Camera() noexcept = default;

		void Update() noexcept;

		const Matrix& GetViewMatrix() const noexcept { return m_ViewMatrix; }
		const Matrix& GetProjMatrix() const noexcept { return m_ProjMatrix; }

		const Vector3& GetPosition() const noexcept { return m_Position; }
		const Vector3& GetForward() const noexcept { return m_Forward; }
		const Vector3& GetRight() const noexcept { return m_Right; }
		const Vector3& GetUp() const noexcept { return m_Up; }

		float GetNear() const noexcept { return m_Near; }
		float GetFar() const noexcept { return m_Far; }
		float GetFov() const noexcept { return m_Fov; }
		float GetAspect() const noexcept { return m_Aspect; }

		float GetYaw() const noexcept { return m_Yaw; }
		float GetPitch() const noexcept { return m_Pitch; }

		void SetPosition(const Vector3& pos) noexcept;
		void SetYawPitch(float yawRadians, float pitchRadians) noexcept;
		void SetNearFar(float nearZ, float farZ) noexcept;
		void SetFov(float fovDegrees) noexcept;
		void SetAspect(float aspect) noexcept;

		void OnResize(uint32_t width, uint32_t height) noexcept;

		void LookAt(const Vector3& eye, const Vector3& target) noexcept;

	public:
		static float ClampNear(float nearZ) noexcept;
		static float ClampFar(float nearZ, float farZ) noexcept;
		static float ClampFov(float fov) noexcept;

	private:
		void MarkViewDirty() noexcept { m_ViewDirty = true; }
		void MarkProjDirty() noexcept { m_ProjDirty = true; }

		void UpdateProjMatrix() noexcept;
		void UpdateViewMatrix() noexcept;

		void ComputeYawPitchFromForward(const Vector3& forward) noexcept;

	private:
		static constexpr float PitchLimitRadians = utils::ToRadians(85.0f);

	private:
		Matrix m_ViewMatrix = Matrix::Identity;
		Matrix m_ProjMatrix = Matrix::Identity;

		Vector3 m_Forward = Vector3::UnitZ;
		Vector3 m_Up = Vector3::UnitY;
		Vector3 m_Right = Vector3::UnitX;
		Vector3 m_Position = Vector3::Zero;

		float m_Near = 0.01f;
		float m_Far = 1000.0f;
		float m_Aspect = 1.0f;
		float m_Fov = 60.0f;	// degrees

		float m_Pitch = 0.0f;	// radians
		float m_Yaw = 0.0f;		// radians

		bool m_ViewDirty = true;
		bool m_ProjDirty = true;
	};
}