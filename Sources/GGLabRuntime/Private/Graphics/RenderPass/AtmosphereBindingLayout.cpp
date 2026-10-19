#include "Graphics/RenderPass/AtmosphereBindingLayout.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"

#include <utility>

namespace gglab
{
	AtmosphereBindingLayout AtmosphereBindingLayout::Create(const RenderServices& services) noexcept
	{
		const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
		auto* context = services.m_Presentation ? services.m_Presentation->GetRHIContext() : nullptr;
		return common && context ? Create(context->GetPipelineSystem(), *common) : AtmosphereBindingLayout{};
	}

	AtmosphereBindingLayout AtmosphereBindingLayout::Create(RHIPipelineSystem& pipelines,
		const ComposedLayout& common) noexcept
	{
		const auto composed = scene_bindings::ComposeAtmosphereLayout(common);
		if (!composed.IsSuccess())
		{
			return {};
		}
		AtmosphereBindingLayout result;
		result.m_Layout = ResolvedRenderLayout::Create(pipelines, *composed.m_Layout, "Atmosphere.BindingLayout");
		result.m_Atmosphere = ResolvedBindingRef<AtmosphereGPU>::Resolve(
			result.m_Layout, scene_bindings::AtmosphereConstants);
		return result.m_Atmosphere.IsValid() ? std::move(result) : AtmosphereBindingLayout{};
	}
}
