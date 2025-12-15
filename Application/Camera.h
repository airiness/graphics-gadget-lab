#pragma once
namespace gglab
{
	class Camera
	{
	public:
		struct CreateInfo
		{
			Vector3 m_Forward = Vector3::UnitZ;
			Vector3 m_Up = Vector3::UnitY;
			Vector3 m_Right = Vector3::UnitX;
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
		void OnResize(uint32_t width, uint32_t height) noexcept;

		Matrix GetViewMatrix() const noexcept;
		Matrix GetProjMatrix() const noexcept;

		Vector3 GetPosition() const noexcept { return m_Position; }

		float GetNear() const noexcept { return m_Near; }
		float GetFar() const noexcept { return m_Far; }
		float GetFov() const noexcept { return m_Fov; }
		float GetAspect() const noexcept { return m_Aspect; }

	private:
		void UpdateProjMatrix() noexcept;
		void UpdateViewMatrix() noexcept;
	private:
		static constexpr float SmoothStepT = 0.5f;

	private:
		Matrix m_ViewMatrix = Matrix::Identity;
		Matrix m_ProjMatrix = Matrix::Identity;

		Vector3 m_Forward = Vector3::UnitZ;
		Vector3 m_Up = Vector3::UnitY;
		Vector3 m_Right = Vector3::UnitX;
		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Velocity = Vector3::Zero;

		float m_Near = 0.01f;
		float m_Far = 1000.0f;
		float m_Aspect = 1.0f;
		float m_Fov = 60.0f;

		float m_MovementSpeed = 120.0f;
		float m_RotationSpeed = 0.25f;

		float m_Pitch = 0.0f;
		float m_Yaw = 0.0f;
	};
}