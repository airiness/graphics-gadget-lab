#pragma once

namespace gglab
{
	template<typename T>
	class DX12RingStructuredBuffer
	{
	public:
		DX12RingStructuredBuffer() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12RingStructuredBuffer);
		~DX12RingStructuredBuffer();
	};
}