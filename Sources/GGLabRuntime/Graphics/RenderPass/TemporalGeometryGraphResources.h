#pragma once
#include "Graphics/Pipeline/TemporalMotion.h"
#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGTemporalGeometryResources
	{
		RGTextureId m_MotionVectors{};
		RHITextureViewDesc m_MotionSrvDesc{};
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_MotionVectors.IsValid() && m_MotionSrvDesc.m_Type ==
				RHITextureViewType::ShaderResource && m_MotionSrvDesc.m_Format ==
				TemporalMotionFormat && m_Width > 0 && m_Height > 0;
		}
	};

	inline constexpr const char* TemporalGeometryResourcesName = "RGTemporalGeometryResources";
}
