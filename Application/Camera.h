#pragma once
namespace graphicsGadgetLab
{
	class Camera
	{
	public:
		struct Info
		{
			float m_Near = 0.01f;
			float m_Far = 1000.0f;
			float m_Fov = 60.0f;
			uint32_t m_Width = 1920;
			uint32_t m_Height = 720;
			Vector3 m_Position = Vector3::Zero;
			Vector3 m_Forward = Vector3::UnitZ;
		};

	public:
		explicit Camera(const Info& info) noexcept;
		~Camera() noexcept = default;

		void Update() noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;

		Matrix GetViewMatrix() const noexcept;
		Matrix GetProjMatrix() const noexcept;

		Vector3 GetPosition() const noexcept { return m_Position; }
		Vector3 GetDirection() const noexcept;

		float GetNear() const noexcept { return m_Near; }
		float GetFar() const noexcept { return m_Far; }
		float GetFov() const noexcept { return m_Fov; }

	private:
		void RotateCamera(float yaw, float pitch);
		void SetProjMatrix(float fov, float aspect, float nearZ, float farZ) noexcept;

	private:
		static constexpr float SmoothStepT = 0.5f;

	private:
		Matrix m_ViewMatrix = Matrix::Identity;
		Matrix m_ProjMatrix = Matrix::Identity;

		Quaternion m_Oritation;

		Vector3 m_Position = Vector3::Zero;
		Vector3 m_Velocity = Vector3::Zero;
		float m_Near = 0.01f;
		float m_Far = 1000.0f;
		float m_Aspect = 1.0f;
		float m_Fov = 60.0f;

		float m_MovementSpeed = 1.0f;
		float m_RotationSpeed = 0.5f;
	};
}