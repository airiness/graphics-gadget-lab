#include "Graphics/Asset/AssetPaths.h"
#include "GGLabFoundation/IO/PathUtils.h"

namespace gglab
{
	std::filesystem::path ResolveAssetPath(
		const std::filesystem::path& assetRoot, const std::filesystem::path& path) noexcept
	{
		if (path.empty())
		{
			return {};
		}
		if (path.is_absolute())
		{
			return utils::Canonical(path);
		}

		std::filesystem::path logicalPath = path.lexically_normal();
		if (!logicalPath.empty() && logicalPath.begin()->generic_string() == "Assets")
		{
			std::filesystem::path withoutRoot;
			auto component = logicalPath.begin();
			++component;
			for (; component != logicalPath.end(); ++component)
			{
				withoutRoot /= *component;
			}
			logicalPath = std::move(withoutRoot);
		}
		for (const auto& component : logicalPath)
		{
			if (component == "..")
			{
				return {};
			}
		}
		return utils::Canonical(assetRoot / logicalPath);
	}
}
