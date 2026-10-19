#pragma once
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"

namespace gglab
{
	struct RenderServices;
	template <>
	struct ConstantPayloadTraits<AtmosphereGPU>
	{
		static constexpr LayoutPayloadTag Tag{ 3 };
	};

	// The legacy b0 family has an Atmosphere meaning, independent of Scene b0.
	// The physical common parameter map is retained until the b5 ABI migration.
	class AtmosphereBindingLayout final
	{
	public:
		[[nodiscard]] static AtmosphereBindingLayout Create(const RenderServices& services) noexcept;
		[[nodiscard]] static AtmosphereBindingLayout Create(RHIPipelineSystem& pipelines,
			const ComposedLayout& common) noexcept;
		[[nodiscard]] bool IsValid() const noexcept { return m_Layout.IsValid(); }
		[[nodiscard]] RHIBindingLayoutHandle GetNativeLayout() const noexcept { return m_Layout.GetNativeLayout(); }
		[[nodiscard]] ResolvedRenderPipeline ResolvePipeline(RenderPipelineResolver& resolver,
			GraphicsPipelineSlot& slot, const GraphicsPhysicalPipelineKey& recipe,
			const RenderPassInfo& info) const noexcept
		{
			return m_Layout.Resolve(resolver, slot, recipe, info);
		}
		[[nodiscard]] ResolvedRenderPipeline ResolvePipeline(RenderPipelineResolver& resolver,
			ComputePipelineSlot& slot, const ComputePipelineRecipe& recipe,
			const RenderPassInfo& info) const noexcept
		{
			return m_Layout.Resolve(resolver, slot, recipe, info);
		}
		template <typename Context>
		[[nodiscard]] ScopedConstantBindings<Context> BeginRecording(Context& context) const noexcept
		{
			return ScopedConstantBindings<Context>(context, m_Layout, m_Atmosphere);
		}
		template <typename Context>
		[[nodiscard]] bool Bind(ScopedConstantBindings<Context>& scope,
			const TypedConstantSnapshot<AtmosphereGPU>& snapshot) const noexcept
		{
			return scope.Bind(m_Atmosphere, snapshot);
		}

	private:
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<AtmosphereGPU> m_Atmosphere;
	};
}
