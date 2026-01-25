#pragma once
#include "StringId.h"
#include "GraphicsTypes.h"

namespace gglab
{
	class Camera;

	struct RenderView
	{
		Matrix m_View = Matrix::Identity;
		Matrix m_Proj = Matrix::Identity;
		Matrix m_ViewProj = Matrix::Identity;
		Matrix m_InvView = Matrix::Identity;
		Matrix m_InvProj = Matrix::Identity;
		Matrix m_InvViewProj = Matrix::Identity;

		Vector3 m_CameraPosition = Vector3::Zero;
		float m_Near = 0.1f;
		float m_Far = 10000.0f;
		float m_FovRadians = 0.0f;
		float m_Aspect = 1.0f;
		float m_Exposure = 1.0f;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		RenderViewID m_ViewId = RenderViewID::Unknown;
		StringID m_Name{};
	};

	class RenderViewBuilder
	{
	public:
		struct BuildInfo
		{
			const Camera& m_Camera;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;

			StringID m_Name{};
			RenderViewID m_ViewId = RenderViewID::Unknown;
		};

	public:
		RenderView Build(const BuildInfo& info) noexcept;
	};
}