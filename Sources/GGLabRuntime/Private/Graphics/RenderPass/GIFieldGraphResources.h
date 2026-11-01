#pragma once
#include "Graphics/Pipeline/GIFieldSampling.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <array>

namespace gglab
{
	struct RGGIFieldResources
	{
		GIFieldSamplingParameters m_Parameters;
		std::array<RGTextureId, 3> m_Textures; // Irradiance, distance, classification.
		RGTextureId m_State;
		RGBufferId m_Rays;
		RGBufferId m_Status;
		bool m_Compose = true;
	};

	struct GIFieldSamplingViews
	{
		GIFieldSamplingParameters m_Parameters;
		std::array<RGTextureViewId, 3> m_Views{};

		void Declare(RenderGraph::RGBuilder& builder, const RGGIFieldResources& field,
			RHIStage stages) noexcept
		{
			m_Parameters = field.m_Parameters;
			for (size_t index = 0; index < m_Views.size(); ++index)
			{
				const auto texture = builder.Read(field.m_Textures[index], RGTextureAccess::Sample, stages);
				const auto format = builder.GetTextureDesc(texture).m_Format;
				m_Views[index] = builder.CreateView<RHITextureViewType::ShaderResource>(texture,
					MakeRHITexture2DArrayViewDesc(format, 0, 0, m_Parameters.m_Counts[1]));
			}
		}

		[[nodiscard]] GIFieldSamplingParameters Resolve(RGExecuteContext& execute) const noexcept
		{
			auto result = m_Parameters;
			if (result.m_IrradianceInteriorWidth != 0)
			{
				result.m_Irradiance = execute.GetViewDescriptor(m_Views[0]).m_Index;
				result.m_Distance = execute.GetViewDescriptor(m_Views[1]).m_Index;
				result.m_Classification = execute.GetViewDescriptor(m_Views[2]).m_Index;
			}
			return result;
		}
	};
}
