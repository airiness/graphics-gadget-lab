#pragma once

#include "FoundationPrivateAccess.h"
#include "Hash/Sha256Backend.h"

namespace gglab::foundation::detail
{
	[[nodiscard]] std::unique_ptr<Sha256Backend> CreateWin32Sha256Backend() noexcept;
}
