#pragma once
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	// A valid value owns the complete Forward binding map and its checked constant references.
	class ForwardBindingLayout final
	{
	public:
		[[nodiscard]] static ForwardBindingLayout Create(RHIPipelineSystem& pipelineSystem,
			const ComposedLayout& common, bool forwardPlusLightLists) noexcept;
		[[nodiscard]] bool IsValid() const noexcept { return m_Layout.IsValid(); }
		[[nodiscard]] RHIBindingLayoutHandle GetNativeLayout() const noexcept { return m_Layout.GetNativeLayout(); }
		[[nodiscard]] ResolvedRenderPipeline ResolvePipeline(RenderPipelineResolver& resolver,
			GraphicsPipelineSlot& slot, const GraphicsPhysicalPipelineKey& recipe,
			const RenderPassInfo& info) const noexcept
		{
			return m_Layout.Resolve(resolver, slot, recipe, info);
		}
		[[nodiscard]] ScopedConstantBindings<RHIGraphicsCommandContext> BeginRecording(
			RHIGraphicsCommandContext& context) const noexcept
		{
			return ScopedConstantBindings<RHIGraphicsCommandContext>(context, m_Layout, m_Scene, m_Shadow);
		}
		[[nodiscard]] bool BindFrameConstants(ScopedConstantBindings<RHIGraphicsCommandContext>& scope,
			const TypedConstantSnapshot<SceneGPU>& scene,
			const TypedConstantSnapshot<DirectionalShadowGPU>& shadow) const noexcept;
		// Buffers remain declared RenderGraph inputs; this only records their existing bindings.
		[[nodiscard]] bool BindTileLists(RHIGraphicsCommandContext& context,
			RHIBufferHandle headers, RHIBufferHandle indices) const noexcept;

	private:
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		ResolvedBindingRef<DirectionalShadowGPU> m_Shadow;
		std::optional<uint32_t> m_TileHeaders;
		std::optional<uint32_t> m_TileIndices;
	};
}
