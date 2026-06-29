#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITypes.h"

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

	struct RHIResourceDesc
	{
		RHIResourceType m_Type = RHIResourceType::Unknown;
		RHIResourceState m_InitialState{};
		std::optional<RHIClearValue> m_ClearValue = std::nullopt;
		const char* m_DebugName = nullptr;
	};

	struct RHIExternalResourceDesc
	{
		RHIResourceState m_InitialState{};
		const char* m_DebugName = nullptr;
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
				m_BaseMip,
				m_MipCount,
				m_BaseArraySlice,
				m_ArraySliceCount,
				m_Aspects);
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
