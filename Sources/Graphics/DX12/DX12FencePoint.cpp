#include "Core/Precompiled.h"
#include "Graphics/DX12/DX12FencePoint.h"
#include "Graphics/DX12/DX12Fence.h"

namespace gglab
{
	DX12FencePoint::DX12FencePoint(DX12Fence* fence, uint64_t fenceValue) noexcept :
		m_FencePtr(fence),
		m_PointValue(fenceValue)
	{
	}

	bool DX12FencePoint::operator==(const DX12FencePoint& other) const noexcept
	{
		return m_FencePtr == other.m_FencePtr &&
			m_PointValue == other.m_PointValue;
	}

	bool DX12FencePoint::operator<(const DX12FencePoint& other) const noexcept
	{
		return m_FencePtr == other.m_FencePtr ?
			m_PointValue < other.m_PointValue :
			m_FencePtr < other.m_FencePtr;
	}

	bool DX12FencePoint::IsCompleted() const noexcept
	{
		return m_FencePtr && m_FencePtr->IsCompleted(m_PointValue);
	}

	void DX12FencePoint::Wait(uint32_t timeout) const noexcept
	{
		if (m_FencePtr)
		{
			m_FencePtr->WaitCompletion(m_PointValue, timeout);
		}
	}

	void DX12FencePoint::Reset() noexcept
	{
		m_FencePtr = nullptr;
		m_PointValue = 0;
	}
}