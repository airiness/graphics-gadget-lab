#pragma once
#include "Graphics/RHI/RHIFence.h"

namespace gglab
{
	class DX12Fence;
	class DX12FencePoint final
	{
	public:
		DX12FencePoint() noexcept = default;
		explicit DX12FencePoint(DX12Fence* fence, uint64_t fenceValue) noexcept;
		~DX12FencePoint() = default;

		bool operator==(const DX12FencePoint& other) const noexcept;
		bool operator<(const DX12FencePoint& other) const noexcept;

		bool IsValid() const noexcept { return m_FencePtr != nullptr; }
		explicit operator bool() const noexcept { return IsValid(); }

		const DX12Fence* GetFence() const noexcept { return m_FencePtr; }
		uint64_t GetValue() const noexcept { return m_PointValue; }
		RHIFencePoint ToRHI() const noexcept;

		bool IsCompleted() const noexcept;
		void Wait(uint32_t timeout = GGLAB_INFINITE) const noexcept;
		void Reset() noexcept;

	private:
		DX12Fence* m_FencePtr = nullptr;
		uint64_t m_PointValue = 0;
	};
}
