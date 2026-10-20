#include "Graphics/RenderPass/ForwardBindingLayout.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"

#include <utility>

namespace gglab
{
	ForwardBindingLayout ForwardBindingLayout::Create(RHIPipelineSystem& pipelineSystem,
		const ComposedLayout& common, bool forwardPlusLightLists) noexcept
	{
		const auto composition = scene_bindings::ComposeForwardLayout(common, forwardPlusLightLists);
		if (!composition.m_Layout)
		{
			return {};
		}
		ForwardBindingLayout result;
		auto layout = ResolvedRenderLayout::Create(pipelineSystem, *composition.m_Layout,
			forwardPlusLightLists ? "ForwardPBR.ForwardPlusBindingLayout" : "ForwardPBR.BindingLayout");
		result.m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(layout, scene_bindings::SceneConstants);
		result.m_Shadow = ResolvedBindingRef<DirectionalShadowGPU>::Resolve(layout, scene_bindings::DirectionalShadow);
		result.m_Atmosphere = ResolvedBindingRef<AtmosphereGPU>::Resolve(layout, scene_bindings::AtmosphereConstants);
		if (!result.m_Scene.IsValid() || !result.m_Shadow.IsValid() || !result.m_Atmosphere.IsValid())
		{
			return {};
		}
		if (forwardPlusLightLists)
		{
			const auto& contract = *layout.GetContract();
			result.m_TileHeaders = contract.FindParameterIndex(scene_bindings::ForwardPlusTileHeaders);
			result.m_TileIndices = contract.FindParameterIndex(scene_bindings::ForwardPlusTileIndices);
			if (!result.m_TileHeaders || !result.m_TileIndices)
			{
				return {};
			}
		}
		result.m_Layout = std::move(layout);
		return result;
	}

	bool ForwardBindingLayout::BindFrameConstants(ScopedConstantBindings<RHIGraphicsCommandContext>& scope,
		const TypedConstantSnapshot<SceneGPU>& scene,
		const TypedConstantSnapshot<DirectionalShadowGPU>& shadow,
		const TypedConstantSnapshot<AtmosphereGPU>& atmosphere) const noexcept
	{
		return scope.Bind(m_Scene, scene) && scope.Bind(m_Shadow, shadow) && scope.Bind(m_Atmosphere, atmosphere);
	}

	bool ForwardBindingLayout::BindTileLists(RHIGraphicsCommandContext& context,
		RHIBufferHandle headers, RHIBufferHandle indices) const noexcept
	{
		if (!IsValid() || !m_TileHeaders || !m_TileIndices || !headers.IsValid() || !indices.IsValid())
		{
			context.RejectRecording();
			return false;
		}
		context.SetReadOnlyBuffer(*m_TileHeaders, headers);
		context.SetReadOnlyBuffer(*m_TileIndices, indices);
		return !context.HasRecordingError();
	}
}
