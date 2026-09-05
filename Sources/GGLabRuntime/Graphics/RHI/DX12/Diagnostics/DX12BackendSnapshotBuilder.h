#pragma once

namespace gglab
{
	class DX12Context;
	struct DX12BackendSnapshot;

	void BuildDX12BackendSnapshot(const DX12Context& context,
		DX12BackendSnapshot& outSnapshot) noexcept;
}
