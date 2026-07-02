#pragma once

namespace gglab
{
	class TransientResourcePool;
	struct TransientResourcePoolSnapshot;

	void BuildTransientResourcePoolSnapshot(
		const TransientResourcePool& pool,
		TransientResourcePoolSnapshot& outSnapshot) noexcept;
}
