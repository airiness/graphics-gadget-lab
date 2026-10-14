#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	enum class RGObservationPosition : uint8_t { Before, After, Final };
	enum class RGObservationFailure : uint8_t
	{
		None, UnknownResource, UnknownPass, AmbiguousPass, NotAccessedByPass,
		Culled, UndefinedContent, UnsupportedResource, UnsupportedFormat, TooLarge, Busy,
	};

	// Names and occurrences refer to the original declarations, including culled passes.
	struct RGResourceAddress
	{
		std::string m_Resource;
		RGObservationPosition m_Position = RGObservationPosition::Final;
		std::string m_Pass;
		std::optional<uint32_t> m_Occurrence;
		uint32_t m_Mip = 0;
		uint32_t m_FirstSlice = 0;
		std::optional<uint32_t> m_SliceCount;
		// None selects the color plane, or depth for a depth/stencil texture.
		RHITextureAspect m_Aspect = RHITextureAspect::None;
	};

	struct RGObservationTarget
	{
		RGResourceAddress m_Address;
		RGObservationFailure m_Failure = RGObservationFailure::None;
		RGResourceType m_Type = RGResourceType::RGTexture;
		std::optional<RGResourceHandle> m_Resource;
		RGResourceNodeIndex m_Node;
		RGVirtualResourceIndex m_VirtualResource;
		RGPassNodeIndex m_Writer;
		// Insert before this position in the original execution order. Its size means final.
		uint32_t m_InsertionPosition = 0;
		uint32_t m_OriginalLastUse = 0;
		RHITextureDesc m_TextureDesc{};
		RHIBufferDesc m_BufferDesc{};
		RHISubresourceRange m_Subresources{};
		RHIFormat m_ViewFormat = RHIFormat::Unknown;
		bool m_RawTypeless = false;
		std::string m_WriterName;
		uint32_t m_WriterOccurrence = 0;
	};

	struct RGObservationCopy
	{
		RGObservationTarget m_Target;
		RHIBufferHandle m_Readback;
		uint64_t m_ReadbackBytes = 0;
		RHITextureCopyFootprint m_Footprint{};
		uint64_t m_SlicePitch = 0;
	};

	[[nodiscard]] std::string_view GetRGObservationFailureName(RGObservationFailure failure) noexcept;
}
