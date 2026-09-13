#pragma once
#include "Graphics/Asset/DerivedData/LocalDerivedDataPlatform.h"

#include <memory>
#include <string>

namespace gglab
{
	[[nodiscard]] std::unique_ptr<LocalDerivedDataPlatformBase>
		CreateWin32LocalDerivedDataPlatform() noexcept;
	[[nodiscard]] std::wstring MakeWin32LocalDerivedDataMaintenanceMutexName(
		const LocalDerivedDataRootIdentity& identity);
}
