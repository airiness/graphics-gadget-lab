#include "Precompiled.h"
#include "DX12FencePoint.h"
#include "DX12Fence.h"

namespace gglab
{
	DX12FencePoint::DX12FencePoint(DX12Fence* fence, uint64_t fenceValue) noexcept :
		m_FencePtr(fence),
		m_PointValue(fenceValue)
	{
	}

	bool DX12FencePoint::IsCompleted() const noexcept
	{
		return m_FencePtr && m_FencePtr->IsCompleted(m_PointValue);
	}

	void DX12FencePoint::Wait(uint32_t timeout) noexcept
	{
		if (m_FencePtr)
		{
			m_FencePtr->WaitCompletion(m_PointValue);
		}
	}

	void DX12FencePoint::Reset() noexcept
	{
		m_FencePtr = nullptr;
		m_PointValue = 0;
	}
}
