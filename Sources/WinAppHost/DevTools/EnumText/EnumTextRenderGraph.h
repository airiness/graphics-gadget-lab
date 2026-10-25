#pragma once
#include "DevTools/EnumText/EnumText.h"
#include "GGLabRuntime/Diagnostics/RenderGraphEnumText.h"

namespace gglab::devtools
{
	template <> struct EnumTextTraits<RGPassEncoderType> : diagnostics::RGEnumTextTraits<RGPassEncoderType> {};
	template <> struct EnumTextTraits<RGResourceType> : diagnostics::RGEnumTextTraits<RGResourceType> {};
	template <> struct EnumTextTraits<RGOrderingRequirement> : diagnostics::RGEnumTextTraits<RGOrderingRequirement> {};
	template <> struct EnumTextTraits<RGDependencyAccess> : diagnostics::RGEnumTextTraits<RGDependencyAccess> {};
	template <> struct EnumTextTraits<RGDependencyReason> : diagnostics::RGEnumTextTraits<RGDependencyReason> {};
	template <> struct EnumTextTraits<RGBarrierKind> : diagnostics::RGEnumTextTraits<RGBarrierKind> {};
	template <> struct EnumTextTraits<RGBarrierReason> : diagnostics::RGEnumTextTraits<RGBarrierReason> {};
	template <> struct EnumTextTraits<RGTextureAccess> : diagnostics::RGEnumTextTraits<RGTextureAccess> {};
	template <> struct EnumTextTraits<RGBufferAccess> : diagnostics::RGEnumTextTraits<RGBufferAccess> {};
	template <> struct EnumTextTraits<RHITextureUsage> : diagnostics::RGEnumTextTraits<RHITextureUsage> {};
	template <> struct EnumTextTraits<RHIBufferUsage> : diagnostics::RGEnumTextTraits<RHIBufferUsage> {};
	template <> struct EnumTextTraits<RHIStage> : diagnostics::RGEnumTextTraits<RHIStage> {};
	template <> struct EnumTextTraits<RHITextureAspect> : diagnostics::RGEnumTextTraits<RHITextureAspect> {};
	template <> struct EnumTextTraits<RHIAccess> : diagnostics::RGEnumTextTraits<RHIAccess> {};
	template <> struct EnumTextTraits<RHILayout> : diagnostics::RGEnumTextTraits<RHILayout> {};

	[[nodiscard]] inline std::string BarrierStateText(const RHIResourceState& state)
	{
		return std::format("stage={}, access={}, layout={}", EnumFlagsTextWithBits(state.m_Stages),
			EnumFlagsTextWithBits(state.m_Access), EnumValueTextWithBits(state.m_Layout));
	}

	[[nodiscard]] inline std::string SubresourceRangeText(
		const std::optional<RHISubresourceRange>& range)
	{
		if (!range)
		{
			return "All";
		}

		auto countText = [](uint32_t count)
			{
				return count == RHISubresourceRange::Remaining ?
					std::string("Remaining") :
					std::to_string(count);
			};
		return std::format("mip {}+{}, array {}+{}, aspects {}", range->m_BaseMip,
			countText(range->m_MipCount), range->m_BaseArraySlice,
			countText(range->m_ArraySliceCount), EnumFlagsTextWithBits(range->m_Aspects));
	}
}
