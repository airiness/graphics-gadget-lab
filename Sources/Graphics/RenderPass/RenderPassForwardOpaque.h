#pragma once
#include "Graphics/RenderPass/RenderPassForwardPBRBase.h"

namespace gglab
{
	class RenderPassForwardOpaque final :
		public RenderPassForwardPBRBase
	{
	public:
		RenderPassForwardOpaque() noexcept :
			RenderPassForwardPBRBase(
				{
					.m_TypeName = "Geometry.ForwardOpaque",
					.m_DisplayName = "Forward Opaque",
					.m_CategoryName = "Geometry",
					.m_Description = "Renders opaque and alpha-tested scene geometry with forward PBR shading.",
					.m_Category = RenderPassCategory::Geometry,
					.m_Type = RenderPassType::Graphics,
				},
				ForwardPBRPassKind::Opaque)
		{}
		~RenderPassForwardOpaque() override = default;

		void AddPass(
			RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override
		{
			AddForwardPass(rg, context, services);
		}
	};
}
