#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "DevTools/EnumText/EnumTextDX12.h"
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"

namespace gglab::devtools
{
	template<>
	struct EnumTextTraits<RGResourceType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGResourceType::RGTexture, "Texture" },
			EnumTextEntry{ RGResourceType::RGBuffer, "Buffer" },
		};
	};

	template<>
	struct EnumTextTraits<RGResourceAccessType>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGResourceAccessType::Read, "Read" },
			EnumTextEntry{ RGResourceAccessType::Write, "Write" },
		};
	};

	template<>
	struct EnumTextTraits<RGTextureUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGTextureUsage::Sample, "Sample" },
			EnumTextEntry{ RGTextureUsage::RenderTarget, "RTV" },
			EnumTextEntry{ RGTextureUsage::DepthStencil, "DSV Write" },
			EnumTextEntry{ RGTextureUsage::DepthStencilRead, "DSV Read" },
			EnumTextEntry{ RGTextureUsage::UAV, "UAV" },
			EnumTextEntry{ RGTextureUsage::CopySrc, "CopySrc" },
			EnumTextEntry{ RGTextureUsage::CopyDst, "CopyDst" },
			EnumTextEntry{ RGTextureUsage::Present, "Present" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	template<>
	struct EnumTextTraits<RGBufferUsage>
	{
		static constexpr std::array Entries = {
			EnumTextEntry{ RGBufferUsage::Vertex, "Vertex" },
			EnumTextEntry{ RGBufferUsage::Index, "Index" },
			EnumTextEntry{ RGBufferUsage::Constant, "Constant" },
			EnumTextEntry{ RGBufferUsage::UAV, "UAV" },
			EnumTextEntry{ RGBufferUsage::CopySrc, "CopySrc" },
			EnumTextEntry{ RGBufferUsage::CopyDst, "CopyDst" },
		};
		static constexpr std::string_view NoneText = "None";
	};

	[[nodiscard]] inline std::string BarrierStateText(const RGBarrierState& state)
	{
		return std::format("sync={}, access={}, layout={}",
			EnumFlagsTextWithBits(state.m_Sync),
			EnumFlagsTextWithBits(state.m_Access),
			EnumValueTextWithBits(state.m_Layout));
	}
}
