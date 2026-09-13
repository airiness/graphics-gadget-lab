#pragma once

#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <cstdint>

namespace gglab
{
	// Copied observation, not GPU resource ownership. The descriptor may only be
	// submitted through the GUI adapter during the current tooling draw. Do not
	// cache it across frames, resize, resource retirement or runtime shutdown.
	struct PostProcessPreviewDiagnostics
	{
		PostProcessDebugSelection m_Selected{};
		PostProcessDebugSelection m_Published{};
		RHIDescriptorHandle m_SrvDescriptor{};
		uint64_t m_UpdateCount = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
		float m_ExposureEV = 0.0f;
		bool m_Requested = false;
		bool m_HasPublished = false;
	};
}
