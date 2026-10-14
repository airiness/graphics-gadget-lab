#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"
#include "GGLabRuntime/Graphics/Resource/GpuBufferLayout.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	struct ResourceExportFrameContext
	{
		float m_PreExposure = 1.0f;
		bool m_ReversedDepth = false;
	};
	using ResourceExportScalar = GpuBufferScalar;
	struct ResourceExportField
	{
		std::string m_Name;
		uint32_t m_Offset = 0;
		ResourceExportScalar m_Scalar = ResourceExportScalar::Uint32;
		uint32_t m_Count = 1;
	};
	struct ResourceExportSemantics
	{
		std::string m_Role;
		std::string m_Units;
		std::string m_Space;
		std::string m_Description;
		std::optional<float> m_PreExposure;
		std::optional<bool> m_ReversedDepth;
		std::optional<uint32_t> m_WatchTier;
	};
	struct ResourceExportArray
	{
		RGResourceAddress m_Address;
		RGResourceType m_Type = RGResourceType::RGTexture;
		uint32_t m_ResourceNodeIndex = 0;
		uint16_t m_Version = 0;
		int32_t m_WriterPassIndex = -1;
		std::string m_WriterPass;
		uint32_t m_WriterOccurrence = 0;
		RHIFormat m_PhysicalFormat = RHIFormat::Unknown;
		RHIFormat m_ViewFormat = RHIFormat::Unknown;
		bool m_RawTypeless = false;
		ResourceExportScalar m_Scalar = ResourceExportScalar::Uint32;
		std::vector<uint64_t> m_Shape;
		uint32_t m_Stride = 0;
		uint64_t m_ElementCount = 0;
		std::string m_Encoding;
		std::string m_ChannelOrder;
		std::vector<ResourceExportField> m_Fields;
		std::optional<ResourceExportSemantics> m_Semantics;
		uint32_t m_OriginalLastUse = 0;
		uint32_t m_ObservedLastUse = 0;
		std::vector<uint8_t> m_Data;
	};
	struct ResourceExportPayload
	{
		std::vector<ResourceExportArray> m_Arrays;
	};
	[[nodiscard]] std::string_view GetResourceExportDtype(ResourceExportScalar scalar) noexcept;
}
