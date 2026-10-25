#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"

namespace gglab::devtools
{
	ImTextureID ResolveImGuiTextureId(
		const DevelopGuiSystem* developGuiSystem, RHIDescriptorHandle descriptor) noexcept
	{
		return developGuiSystem ? developGuiSystem->ResolveTextureId(descriptor) : ImTextureID{};
	}
}
