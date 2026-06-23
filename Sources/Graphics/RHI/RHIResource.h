#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	class RHIDevice;

	struct RHIResourceDesc
	{
		RHIResourceType m_Type = RHIResourceType::Unknown;
		RHIResourceState m_InitialState{};
		std::optional<RHIClearValue> m_ClearValue = std::nullopt;
		const char* m_DebugName = nullptr;
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
