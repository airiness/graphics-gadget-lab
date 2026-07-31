#pragma once
#include "Graphics/RHI/RHIDescriptor.h"

#include <imgui.h>

namespace gglab
{
	class DevelopGuiSystem;
}

namespace gglab::devtools
{
	[[nodiscard]] ImTextureID ResolveImGuiTextureId(
		const DevelopGuiSystem* developGuiSystem, RHIDescriptorHandle descriptor) noexcept;
}
