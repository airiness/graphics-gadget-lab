#pragma once
#include "Core/StringId.h"
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	class Camera;

	template<RenderViewID ViewId>
	struct RenderViewBuildInfo;

	template<RenderViewID ViewId>
	struct RenderViewBuildTraits;

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
		template<RenderViewID ViewId>
		RenderView Build(const RenderViewBuildInfo<ViewId>& info) const noexcept
		{
			return RenderViewBuildTraits<ViewId>::Build(info);
		}
	};

	template<>
	struct RenderViewBuildInfo<RenderViewID::Main>
	{
		const Camera& m_Camera;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		StringID m_Name = StringID("MainView");
	};

	template<>
	struct RenderViewBuildInfo<RenderViewID::DirectionalShadow>
	{
		const RenderView& m_MainView;
		Vector3 m_LightDirection = -Vector3::UnitY;
		uint32_t m_ShadowMapSize = 2048;
		float m_MaxShadowDistance = 200.0f;
		float m_CasterExtrusionDistance = 600.0f;
		float m_OrthoPadding = 2.0f;
		float m_DepthPadding = 60.0f;
		StringID m_Name = StringID("DirectionalShadowView");
	};

	template<>
	struct RenderViewBuildTraits<RenderViewID::Main>
	{
		static RenderView Build(const RenderViewBuildInfo<RenderViewID::Main>& info) noexcept;
	};

	template<>
	struct RenderViewBuildTraits<RenderViewID::DirectionalShadow>
	{
		static RenderView Build(const RenderViewBuildInfo<RenderViewID::DirectionalShadow>& info) noexcept;
	};
}
