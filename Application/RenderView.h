#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

namespace gglab
{
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

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};
}