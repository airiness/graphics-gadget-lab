#pragma once
#include "Graphics/RHI/RHIResource.h"

#include <cstdint>
#include <tuple>

namespace gglab
{
	enum class RHIBufferUsage : uint32_t
	{
		None = 0,
		Vertex = 1u << 0,
		Index = 1u << 1,
		Constant = 1u << 2,
		Structured = 1u << 3,
		UnorderedAccess = 1u << 4,
		IndirectArgument = 1u << 5,
		CopySource = 1u << 6,
		CopyDest = 1u << 7,
	};
	GGLAB_ENUM_FLAGS(RHIBufferUsage);

	struct RHIBufferDesc
	{
		uint64_t m_SizeInBytes = 0;
		uint32_t m_StrideInBytes = 0;
		RHIBufferUsage m_Usage = RHIBufferUsage::None;
		const char* m_DebugName = nullptr;
	};

	struct RHIImportedBufferDesc
	{
		RHIBufferDesc m_Desc;
		RHIExternalResourceDesc m_External;
	};

	enum class RHIBufferViewType : uint8_t
	{
		ShaderResource,
		UnorderedAccess,
		ConstantBuffer,
	};

	struct RHIBufferViewDesc
	{
		RHIBufferViewType m_Type = RHIBufferViewType::ShaderResource;
		uint64_t m_OffsetInBytes = 0;
		uint64_t m_SizeInBytes = 0;
		uint32_t m_StrideInBytes = 0;
		RHIFormat m_Format = RHIFormat::Unknown;

		bool operator==(const RHIBufferViewDesc&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Type,
				m_OffsetInBytes,
				m_SizeInBytes,
				m_StrideInBytes,
				m_Format);
		}
	};
}
