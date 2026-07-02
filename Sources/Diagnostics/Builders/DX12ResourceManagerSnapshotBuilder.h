#pragma once

namespace gglab
{
	class DX12ResourceManager;
	struct DX12ResourceManagerSnapshot;

	void BuildDX12ResourceManagerSnapshot(
		const DX12ResourceManager& manager,
		DX12ResourceManagerSnapshot& outSnapshot) noexcept;
}
