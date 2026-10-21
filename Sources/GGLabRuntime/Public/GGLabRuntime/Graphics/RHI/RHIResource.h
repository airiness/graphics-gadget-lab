#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>

namespace gglab
{
	class RHIDevice;

	enum class RHIResourceOwnership : uint8_t
	{
		Owned,
		Borrowed,
	};

	enum class RHIMemoryUsage : uint8_t
	{
		GpuOnly,
		CpuToGpu,
		GpuToCpu,
	};

	struct RHIExternalResourceDesc
	{
		RHIResourceState m_InitialState{};
		const char* m_DebugName = nullptr;
	};

	enum class RHIAllocationFactsStatus : uint8_t
	{
		Known,
		// The resource adopted a native object that GGLab's allocator did not allocate.
		External,
		// No live resource was behind the handle when the facts were read.
		Unowned,
		// The backend does not report allocator facts.
		Unsupported,
	};

	enum class RHIMemoryClass : uint8_t
	{
		Unknown,
		DeviceLocal,
		Other,
	};

	// Allocator facts of one live resource, read on the device owner thread. Known facts are
	// reported by the backend allocator and never estimated; other statuses carry no values.
	struct RHIAllocationFacts
	{
		RHIAllocationFactsStatus m_Status = RHIAllocationFactsStatus::Unsupported;
		uint64_t m_AllocatedBytes = 0;
		uint64_t m_AlignmentBytes = 0;
		RHIMemoryClass m_MemoryClass = RHIMemoryClass::Unknown;
		// Offset of the resource within its native allocation.
		uint64_t m_OffsetBytes = 0;
		// Process-local identity of the native allocator allocation. It is never reused or
		// persisted, resources placed in one allocation share it, and it is zero unless known.
		uint64_t m_NativeAllocationId = 0;
	};

	// Allocator-wide bytes; context for a frame, not scoped to any one owner.
	struct RHIAllocatorTotals
	{
		bool m_Known = false;
		uint64_t m_BlockBytes = 0;
		uint64_t m_AllocationBytes = 0;
	};

	struct RHISubresourceRange
	{
		static constexpr uint32_t Remaining = std::numeric_limits<uint32_t>::max();

		uint32_t m_BaseMip = 0;
		uint32_t m_MipCount = Remaining;
		uint32_t m_BaseArraySlice = 0;
		uint32_t m_ArraySliceCount = Remaining;
		RHITextureAspect m_Aspects = RHITextureAspect::All;

		bool operator==(const RHISubresourceRange&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_BaseMip, m_MipCount, m_BaseArraySlice, m_ArraySliceCount, m_Aspects);
		}
	};

	class RHITextureOwner
	{
	public:
		RHITextureOwner() noexcept = default;
		RHITextureOwner(RHIDevice* device, RHITextureHandle handle) noexcept;
		~RHITextureOwner() noexcept;

		GGLAB_DELETE_COPYABLE(RHITextureOwner);

		RHITextureOwner(RHITextureOwner&& rhs) noexcept;
		RHITextureOwner& operator=(RHITextureOwner&& rhs) noexcept;

		void Reset() noexcept;

		[[nodiscard]] RHITextureHandle Get() const noexcept { return m_Handle; }
		[[nodiscard]] explicit operator bool() const noexcept { return m_Handle.IsValid(); }

	private:
		RHIDevice* m_Device = nullptr;
		RHITextureHandle m_Handle{};
	};

	class RHIBufferOwner
	{
	public:
		RHIBufferOwner() noexcept = default;
		RHIBufferOwner(RHIDevice* device, RHIBufferHandle handle) noexcept;
		~RHIBufferOwner() noexcept;

		GGLAB_DELETE_COPYABLE(RHIBufferOwner);

		RHIBufferOwner(RHIBufferOwner&& rhs) noexcept;
		RHIBufferOwner& operator=(RHIBufferOwner&& rhs) noexcept;

		void Reset() noexcept;

		[[nodiscard]] RHIBufferHandle Get() const noexcept { return m_Handle; }
		[[nodiscard]] explicit operator bool() const noexcept { return m_Handle.IsValid(); }

	private:
		RHIDevice* m_Device = nullptr;
		RHIBufferHandle m_Handle{};
	};
}
