#pragma once
#include "Graphics/RHI/RHICommandContext.h"

namespace gglab
{
	enum class BufferAllocationType : uint8_t
	{
		Persistent,
		Dynamic,
	};

	// Small inline data bound through the RHI push-constant path. On DX12 this
	// remains root constants rather than allocating a buffer resource.
	template<typename T>
	class InlineConstants
	{
	public:
		static_assert(std::is_trivially_copyable_v<T>);
		static_assert(std::is_standard_layout_v<T>);
		static_assert(sizeof(T) % sizeof(uint32_t) == 0);

		T& Get() noexcept { return m_Data; }
		const T& Get() const noexcept { return m_Data; }

		void Bind(
			RHIGraphicsCommandContext& context,
			uint32_t parameterIndex,
			uint32_t destOffset = 0) const noexcept
		{
			context.SetPushConstants(parameterIndex, m_Data, destOffset);
		}

	private:
		T m_Data{};
	};
}
