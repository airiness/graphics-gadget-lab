#pragma once

namespace gglab
{
	class DX12Fence;
	class DX12FencePoint final
	{
	public:
		DX12FencePoint() noexcept = default;
		explicit DX12FencePoint(DX12Fence* fence, uint64_t fenceValue) noexcept;
		~DX12FencePoint() noexcept;

		bool IsValid() const noexcept { return m_FencePtr != nullptr; }

		const DX12Fence* GetFence() const noexcept { return m_FencePtr; }
		uint64_t GetValue() const noexcept { return m_PointValue; }

		bool IsCompleted() const noexcept;
		void Wait() noexcept;

	private:
		DX12Fence* m_FencePtr = nullptr;
		uint64_t m_PointValue = 0;
	};
}