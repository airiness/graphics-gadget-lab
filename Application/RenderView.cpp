#include "Precompiled.h"
#include "RenderView.h"
#include "Camera.h"
#include "MathUtils.h"

namespace gglab
{
	RenderView RenderViewBuilder::Build(const BuildInfo& info) noexcept
	{
		RenderView view{};
		view.m_Name = info.m_Name;
		view.m_ViewId = info.m_ViewId;

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

		view.m_Width = info.m_Width;
		view.m_Height = info.m_Height;

		return view;
	}
}