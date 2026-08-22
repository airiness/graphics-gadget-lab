#include "RuntimePaths.h"

#include <array>

namespace gglab
{
	bool RuntimePaths::IsValid() const noexcept
	{
		const std::array paths{
			&m_RuntimeRoot,
			&m_AssetRoot,
			&m_ShaderSourceRoot,
			&m_ShaderCacheRoot,
			&m_IblDerivedDataRoot,
			&m_TextureDerivedDataRoot,
			&m_EnvironmentAssetRoot,
			&m_SettingsRoot,
		};
		for (const std::filesystem::path* path : paths)
		{
			if (!path || path->empty() || !path->is_absolute())
			{
				return false;
			}
		}
		return true;
	}
}
