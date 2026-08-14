#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "Graphics/RenderPass/RenderPassInfo.h"

namespace gglab::devtools
{
	template <> struct EnumTextTraits<RenderPassCategory>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RenderPassCategory::Unknown, "Unknown"},
			EnumTextEntry{RenderPassCategory::Geometry, "Geometry"},
			EnumTextEntry{RenderPassCategory::Lighting, "Lighting"},
			EnumTextEntry{RenderPassCategory::Shadow, "Shadow"},
			EnumTextEntry{RenderPassCategory::IBL, "IBL"},
			EnumTextEntry{RenderPassCategory::PostProcess, "PostProcess"},
			EnumTextEntry{RenderPassCategory::Debug, "Debug"},
			EnumTextEntry{RenderPassCategory::UI, "UI"},
		};
	};

	template <> struct EnumTextTraits<RenderPassType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{RenderPassType::Graphics, "Graphics"},
			EnumTextEntry{RenderPassType::Compute, "Compute"},
			EnumTextEntry{RenderPassType::Transfer, "Transfer"},
			EnumTextEntry{RenderPassType::Mixed, "Mixed"},
		};
	};
}
