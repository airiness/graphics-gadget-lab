#pragma once
#include "Graphics/GraphicsTypes.h"

#include <string_view>

namespace gglab
{
	enum class AssetLoadKind : uint8_t
	{
		Generic,
		Model,
		Texture,
		Mesh,
	};

	struct AssetLoadProgress
	{
		AssetState m_State = AssetState::Unloaded;
		float m_Fraction = 0.0f;
		std::string_view m_Stage;

		[[nodiscard]] bool IsReady() const noexcept
		{
			return m_State == AssetState::Ready;
		}

		[[nodiscard]] bool HasFailed() const noexcept
		{
			return m_State == AssetState::Failed || m_State == AssetState::Cancelled;
		}
	};

	[[nodiscard]] AssetLoadProgress GetAssetLoadProgress(
		AssetState state,
		AssetLoadKind kind = AssetLoadKind::Generic) noexcept;
}
