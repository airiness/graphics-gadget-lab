#include "Core/Precompiled.h"
#include "Graphics/RenderView.h"
#include "Graphics/Camera.h"
#include "Core/Utility/MathUtils.h"

namespace gglab
{
	RenderView RenderViewBuildTraits<RenderViewID::Main>::Build(
		const RenderViewBuildInfo<RenderViewID::Main>& info) noexcept
	{
		RenderView view{};
		view.m_Name = info.m_Name;
		view.m_ViewId = RenderViewID::Main;

		auto& camera = info.m_Camera;

		view.m_View = camera.GetViewMatrix();
		view.m_Proj = camera.GetProjMatrix();
		view.m_ViewProj = view.m_View * view.m_Proj;
		view.m_InvView = view.m_View.Invert();
		view.m_InvProj = view.m_Proj.Invert();
		view.m_InvViewProj = view.m_ViewProj.Invert();

		view.m_CameraPosition = camera.GetPosition();
		view.m_Near = camera.GetNear();
		view.m_Far = camera.GetFar();
		view.m_FovRadians = utils::ToRadians(camera.GetFov());
		view.m_Aspect = camera.GetAspect();
		view.m_Exposure = 1.0f; //camera.GetExposure();

		view.m_Width = info.m_Width;
		view.m_Height = info.m_Height;

		return view;
	}

	RenderView RenderViewBuildTraits<RenderViewID::DirectionalShadow>::Build(
		const RenderViewBuildInfo<RenderViewID::DirectionalShadow>& info) noexcept
	{
		Vector3 lightDir = info.m_LightDirection;
		if (lightDir.LengthSquared() <= 1.0e-8f)
		{
			lightDir = -Vector3::UnitY;
		}
		lightDir.Normalize();

		Vector3 cameraForward = Vector3::TransformNormal(Vector3::UnitZ, info.m_MainView.m_InvView);
		Vector3 cameraRight = Vector3::TransformNormal(Vector3::UnitX, info.m_MainView.m_InvView);
		Vector3 cameraUp = Vector3::TransformNormal(Vector3::UnitY, info.m_MainView.m_InvView);
		if (cameraForward.LengthSquared() <= 1.0e-8f)
		{
			cameraForward = Vector3::UnitZ;
		}
		if (cameraRight.LengthSquared() <= 1.0e-8f)
		{
			cameraRight = Vector3::UnitX;
		}
		if (cameraUp.LengthSquared() <= 1.0e-8f)
		{
			cameraUp = Vector3::UnitY;
		}
		cameraForward.Normalize();
		cameraRight.Normalize();
		cameraUp.Normalize();

		const float nearZ = std::max(info.m_MainView.m_Near, 0.001f);
		const float farZ = std::max(nearZ + 0.001f, std::min(info.m_MainView.m_Far, info.m_MaxShadowDistance));
		const float tanHalfFov = std::tan(info.m_MainView.m_FovRadians * 0.5f);
		const float nearHalfHeight = tanHalfFov * nearZ;
		const float nearHalfWidth = nearHalfHeight * std::max(info.m_MainView.m_Aspect, 0.001f);
		const float farHalfHeight = tanHalfFov * farZ;
		const float farHalfWidth = farHalfHeight * std::max(info.m_MainView.m_Aspect, 0.001f);

		const Vector3 nearCenter = info.m_MainView.m_CameraPosition + cameraForward * nearZ;
		const Vector3 farCenter = info.m_MainView.m_CameraPosition + cameraForward * farZ;

		std::array<Vector3, 8> frustumCorners = {
			nearCenter - cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
			nearCenter + cameraRight * nearHalfWidth + cameraUp * nearHalfHeight,
			nearCenter - cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
			nearCenter + cameraRight * nearHalfWidth - cameraUp * nearHalfHeight,
			farCenter - cameraRight * farHalfWidth + cameraUp * farHalfHeight,
			farCenter + cameraRight * farHalfWidth + cameraUp * farHalfHeight,
			farCenter - cameraRight * farHalfWidth - cameraUp * farHalfHeight,
			farCenter + cameraRight * farHalfWidth - cameraUp * farHalfHeight,
		};

		Vector3 frustumCenter = Vector3::Zero;
		for (const Vector3& corner : frustumCorners)
		{
			frustumCenter += corner;
		}
		frustumCenter /= static_cast<float>(frustumCorners.size());

		Vector3 lightUp = Vector3::UnitY;
		if (std::abs(lightDir.Dot(lightUp)) > 0.95f)
		{
			lightUp = Vector3::UnitZ;
		}

		const Vector3 lightEyeForBounds = frustumCenter - lightDir;
		const Matrix lightViewForBounds = Matrix::CreateLookAt(
			lightEyeForBounds,
			frustumCenter,
			lightUp);

		Vector3 minLS(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
		Vector3 maxLS(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

		const auto includeLightSpacePoint =
			[&minLS, &maxLS, &lightViewForBounds](const Vector3& pointWS) noexcept
			{
				const Vector3 pointLS = Vector3::Transform(pointWS, lightViewForBounds);
				minLS = Vector3::Min(minLS, pointLS);
				maxLS = Vector3::Max(maxLS, pointLS);
			};

		const float casterExtrusionDistance = std::max(info.m_CasterExtrusionDistance, 0.0f);
		for (const Vector3& corner : frustumCorners)
		{
			includeLightSpacePoint(corner);
			includeLightSpacePoint(corner - lightDir * casterExtrusionDistance);
		}

		const float orthoPadding = std::max(info.m_OrthoPadding, 0.0f);
		minLS.x -= orthoPadding;
		minLS.y -= orthoPadding;
		maxLS.x += orthoPadding;
		maxLS.y += orthoPadding;

		const float depthPadding = std::max(info.m_DepthPadding, 0.0f);
		minLS.z -= depthPadding;
		maxLS.z += depthPadding;

		constexpr float ShadowNear = 0.1f;
		const float shadowFar = std::max((maxLS.z - minLS.z) + ShadowNear, ShadowNear + 1.0f);
		const Vector3 lightEye = lightEyeForBounds + lightDir * (minLS.z - ShadowNear);
		const Vector3 lightTarget = lightEye + lightDir;

		RenderView view{};
		view.m_Name = info.m_Name;
		view.m_ViewId = RenderViewID::DirectionalShadow;
		view.m_View = Matrix::CreateLookAt(lightEye, lightTarget, lightUp);
		view.m_Proj = Matrix::CreateOrthographicOffCenter(minLS.x, maxLS.x, minLS.y, maxLS.y, ShadowNear, shadowFar);
		view.m_ViewProj = view.m_View * view.m_Proj;
		view.m_InvView = view.m_View.Invert();
		view.m_InvProj = view.m_Proj.Invert();
		view.m_InvViewProj = view.m_ViewProj.Invert();
		view.m_CameraPosition = lightEye;
		view.m_Near = ShadowNear;
		view.m_Far = shadowFar;
		view.m_FovRadians = 0.0f;
		view.m_Aspect = 1.0f;
		view.m_Exposure = 1.0f;
		view.m_Width = info.m_ShadowMapSize;
		view.m_Height = info.m_ShadowMapSize;

		return view;
	}
}
