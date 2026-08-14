#pragma once
#include "Diagnostics/Snapshots/AssetSnapshot.h"

namespace gglab::devtools
{
	[[nodiscard]] std::string ModelDisplayName(const AssetSnapshot::Model& model);
	[[nodiscard]] std::string TextureDisplayName(const AssetSnapshot::Texture& texture);
}
