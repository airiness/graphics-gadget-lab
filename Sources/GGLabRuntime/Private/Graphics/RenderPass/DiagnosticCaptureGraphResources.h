#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

namespace gglab
{
	// Display-resolution visualization of the frame's pending diagnostic tap, read by
	// the Diagnostic capture pass. Present only in frames with Diagnostic requests.
	struct RGDiagnosticCaptureResources
	{
		RGTextureId m_Texture{};
		RHITextureDesc m_Desc{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Texture.IsValid() && m_Desc.m_Extent.m_Width > 0 &&
				m_Desc.m_Extent.m_Height > 0;
		}
	};

	inline constexpr const char* DiagnosticCaptureResourcesName = "RGDiagnosticCaptureResources";
}
