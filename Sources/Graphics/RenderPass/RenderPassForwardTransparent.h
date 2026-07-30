#pragma once
#include "Graphics/RenderPass/RenderPassForwardPBRBase.h"

namespace gglab
{
	class RenderPassForwardTransparent final :
		public RenderPassForwardPBRBase
	{
	public:
		RenderPassForwardTransparent() noexcept :
			RenderPassForwardPBRBase(
				{
					.m_TypeName =
						"Geometry.ForwardTransparent",
					.m_DisplayName = "Forward Transparent",
					.m_CategoryName = "Geometry",
					.m_Description = "Renders transparent scene geometry with forward PBR shading.",
					.m_Category = RenderPassCategory::Geometry,
					.m_Type = RenderPassType::Graphics,
				},
				ForwardPBRPassKind::Transparent)
		{}
		~RenderPassForwardTransparent() override = default;

		void AddPass(
			RenderGraph& rg,
			const RenderFrameContext& context,
			const RenderServices& services) noexcept override
		{
			AddForwardPass(rg, context, services);
		}
	};
}
