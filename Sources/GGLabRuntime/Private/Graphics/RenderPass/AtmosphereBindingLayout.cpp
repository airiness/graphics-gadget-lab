#include "Graphics/RenderPass/AtmosphereBindingLayout.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"

#include <array>
#include <span>
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
		const auto desc = common.BuildRHIDesc();
		std::array<LayoutBindingRequirement, RHIBindingLayoutDesc::MaxSlots> declarations{};
		bool replacedScene = false;
		for (uint32_t index = 0; index < desc.m_SlotCount; ++index)
		{
			declarations[index] = common.GetBindings()[index];
			declarations[index].m_Slot.m_DebugName = desc.m_Slots[index].m_DebugName;
			if (declarations[index].m_Key == scene_bindings::SceneConstants)
			{
				const auto& slot = declarations[index].m_Slot;
				if (slot.m_Type != RHIBindingType::ConstantBuffer || slot.m_Binding != 0 ||
					slot.m_Space != 0 || slot.m_Count != 1 || slot.m_BufferBindingMode != RHIBufferBindingMode::Fixed)
				{
					return {};
				}
				declarations[index].m_Key = scene_bindings::LegacyAtmosphereConstants;
				declarations[index].m_PayloadTag = ConstantPayloadTraits<AtmosphereGPU>::Tag;
				replacedScene = true;
			}
		}
		if (!replacedScene)
		{
			return {};
		}
		const std::array fragments{ LayoutFragment{ "LegacyAtmosphere",
			std::span(declarations).first(desc.m_SlotCount) } };
		const auto composed = ComposeLayoutFragments(fragments);
		if (!composed.IsSuccess())
		{
			return {};
		}
		AtmosphereBindingLayout result;
		result.m_Layout = ResolvedRenderLayout::Create(pipelines, *composed.m_Layout, "Atmosphere.LegacyBindingLayout");
		result.m_Atmosphere = ResolvedBindingRef<AtmosphereGPU>::Resolve(
			result.m_Layout, scene_bindings::LegacyAtmosphereConstants);
		return result.m_Atmosphere.IsValid() ? std::move(result) : AtmosphereBindingLayout{};
	}
}
