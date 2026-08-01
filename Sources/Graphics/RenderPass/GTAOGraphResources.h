#pragma once

#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	inline constexpr const char* GTAOResourcesName = "GTAO.Resources";

	struct RGGTAOResources
	{
		RGTextureId m_RawAO{};
		RGTextureId m_HalfDepthViewZ{};
		RGTextureId m_DenoiseX{};
		RGTextureId m_DenoiseY{};
		RGTextureId m_FinalAO{};
		RGTextureId m_ReconstructedNormal{};
		RGTextureId m_SelectedSurfaceOffset{};
		GTAOCapabilityStatus m_Capabilities{};
		RHIFormat m_FinalAOFormat = RHIFormat::Unknown;
		uint32_t m_FullWidth = 0;
		uint32_t m_FullHeight = 0;
		uint32_t m_HalfWidth = 0;
		uint32_t m_HalfHeight = 0;

		[[nodiscard]] bool IsEvaluateValid() const noexcept
		{
			return m_RawAO.IsValid() && m_HalfDepthViewZ.IsValid() && m_HalfWidth > 0 &&
				m_HalfHeight > 0;
		}

		[[nodiscard]] bool IsComplete() const noexcept
		{
			return IsEvaluateValid() && m_DenoiseX.IsValid() && m_DenoiseY.IsValid() &&
				m_FinalAO.IsValid() && m_FinalAOFormat != RHIFormat::Unknown;
		}

		[[nodiscard]] bool HasDiagnosticOutputs() const noexcept
		{
			return m_ReconstructedNormal.IsValid() && m_SelectedSurfaceOffset.IsValid();
		}
	};
}
