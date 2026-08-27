#include "GGLabFoundation/Platform/PlatformDefines.h"
#include "Hash/Sha256Backend.h"

#if GGLAB_PLATFORM_WINDOWS
#include "Platform/Win/Hash/Win32Sha256Backend.h"
#endif

namespace gglab::foundation::detail
{
	std::unique_ptr<Sha256Backend> CreateSha256Backend() noexcept
	{
#if GGLAB_PLATFORM_WINDOWS
		if (auto backend = CreateWin32Sha256Backend())
		{
			return backend;
		}
#endif
		return CreatePortableSha256Backend();
	}
}
