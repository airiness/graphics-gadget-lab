#pragma once
#include "Graphics/Buffer/Buffer.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHIResource.h"

#include <string>
#include <vector>

namespace gglab
{
	template<typename T>
	class PersistentStructuredBuffer
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			uint32_t m_ElementCapacity = 0;
			uint32_t m_BufferCount = 1;
			const char* m_DebugName = "PersistentStructuredBuffer";
		};

		explicit PersistentStructuredBuffer(const CreateInfo& createInfo) noexcept :
			m_Device(createInfo.m_Device),
			m_ElementCapacity(createInfo.m_ElementCapacity),
			m_Buffers(createInfo.m_BufferCount)
		{
			GGLAB_ASSERT_MSG(m_Device != nullptr, "PersistentStructuredBuffer requires an RHI device.");
			GGLAB_ASSERT_MSG(m_ElementCapacity > 0 && m_ElementCapacity <= UINT32_MAX / sizeof(T),
				"PersistentStructuredBuffer capacity is invalid.");
			GGLAB_ASSERT_MSG(!m_Buffers.empty(), "PersistentStructuredBuffer requires at least one physical buffer.");

			for (uint32_t bufferIndex = 0; bufferIndex < m_Buffers.size(); ++bufferIndex)
			{
				RHIBufferOwner& buffer = m_Buffers[bufferIndex];
				std::string debugName = createInfo.m_DebugName ? createInfo.m_DebugName : "PersistentStructuredBuffer";
				debugName += "[" + std::to_string(bufferIndex) + "]";
				RHIBufferDesc desc{};
				desc.m_SizeInBytes = static_cast<uint64_t>(sizeof(T)) * m_ElementCapacity;
				desc.m_StrideInBytes = static_cast<uint32_t>(sizeof(T));
				desc.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest;
				desc.m_MemoryUsage = RHIMemoryUsage::GpuOnly;
				desc.m_DebugName = debugName.c_str();
				buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc));
				GGLAB_ASSERT_MSG(buffer, "PersistentStructuredBuffer failed to create an RHI buffer.");
			}
		}

		[[nodiscard]] RHIBufferHandle GetBufferHandle(uint32_t bufferIndex = 0) const noexcept
		{
			GGLAB_ASSERT(bufferIndex < m_Buffers.size());
			return bufferIndex < m_Buffers.size() ? m_Buffers[bufferIndex].Get() : RHIBufferHandle{};
		}
		[[nodiscard]] uint32_t GetElementCapacity() const noexcept { return m_ElementCapacity; }
		[[nodiscard]] uint32_t GetBufferCount() const noexcept { return static_cast<uint32_t>(m_Buffers.size()); }
		[[nodiscard]] BufferAllocationType GetAllocationType() const noexcept { return BufferAllocationType::Persistent; }

	private:
		RHIDevice* m_Device = nullptr;
		uint32_t m_ElementCapacity = 0;
		std::vector<RHIBufferOwner> m_Buffers;
	};
}
