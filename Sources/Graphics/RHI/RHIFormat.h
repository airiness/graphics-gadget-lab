#pragma once
#include "Graphics/RHI/RHITypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	enum class RHIFormatFamily : uint8_t
	{
		Unknown,
		R8G8B8A8,
		R16G16,
		R16G16B16A16,
		R32G32,
		R32G32B32,
		R32G32B32A32,
		R32,
		D24S8,
	};

	struct RHIFormatInfo
	{
		RHIFormat m_Format = RHIFormat::Unknown;
		const char* m_Name = "Unknown";
		RHIFormatFamily m_Family = RHIFormatFamily::Unknown;
		RHITextureAspect m_Aspects = RHITextureAspect::None;
		RHITextureAspect m_DepthStencilAspects = RHITextureAspect::None;
		RHIFormat m_DepthStencilViewFormat = RHIFormat::Unknown;
		uint8_t m_PlaneCount = 0;
		uint8_t m_BytesPerBlock = 0;
		bool m_IsTypeless = false;
	};

	namespace detail
	{
		inline constexpr std::array<RHIFormatInfo, static_cast<size_t>(RHIFormat::Count)> RHIFormatInfos =
		{
			RHIFormatInfo{ RHIFormat::Unknown, "Unknown", RHIFormatFamily::Unknown, RHITextureAspect::None, RHITextureAspect::None, RHIFormat::Unknown, 0, 0, false },
			RHIFormatInfo{ RHIFormat::R8G8B8A8Typeless, "R8G8B8A8Typeless", RHIFormatFamily::R8G8B8A8, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 4, true },
			RHIFormatInfo{ RHIFormat::R8G8B8A8Unorm, "R8G8B8A8Unorm", RHIFormatFamily::R8G8B8A8, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 4, false },
			RHIFormatInfo{ RHIFormat::R8G8B8A8UnormSrgb, "R8G8B8A8UnormSrgb", RHIFormatFamily::R8G8B8A8, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 4, false },
			RHIFormatInfo{ RHIFormat::R16G16Float, "R16G16Float", RHIFormatFamily::R16G16, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 4, false },
			RHIFormatInfo{ RHIFormat::R16G16B16A16Float, "R16G16B16A16Float", RHIFormatFamily::R16G16B16A16, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 8, false },
			RHIFormatInfo{ RHIFormat::R32G32Float, "R32G32Float", RHIFormatFamily::R32G32, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 8, false },
			RHIFormatInfo{ RHIFormat::R32G32B32Float, "R32G32B32Float", RHIFormatFamily::R32G32B32, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 12, false },
			RHIFormatInfo{ RHIFormat::R32G32B32A32Float, "R32G32B32A32Float", RHIFormatFamily::R32G32B32A32, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 16, false },
			RHIFormatInfo{ RHIFormat::R32Typeless, "R32Typeless", RHIFormatFamily::R32, RHITextureAspect::Color, RHITextureAspect::Depth, RHIFormat::D32Float, 1, 4, true },
			RHIFormatInfo{ RHIFormat::R32Float, "R32Float", RHIFormatFamily::R32, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 4, false },
			RHIFormatInfo{ RHIFormat::R32Uint, "R32Uint", RHIFormatFamily::R32, RHITextureAspect::Color, RHITextureAspect::None, RHIFormat::Unknown, 1, 4, false },
			RHIFormatInfo{ RHIFormat::D24UnormS8Uint, "D24UnormS8Uint", RHIFormatFamily::D24S8, RHITextureAspect::DepthStencil, RHITextureAspect::DepthStencil, RHIFormat::D24UnormS8Uint, 2, 4, false },
			RHIFormatInfo{ RHIFormat::D32Float, "D32Float", RHIFormatFamily::R32, RHITextureAspect::Depth, RHITextureAspect::Depth, RHIFormat::D32Float, 1, 4, false },
		};

		consteval bool ValidateRHIFormatInfos() noexcept
		{
			for (size_t index = 0; index < RHIFormatInfos.size(); ++index)
			{
				const RHIFormatInfo& info = RHIFormatInfos[index];
				if (info.m_Format != static_cast<RHIFormat>(index))
				{
					return false;
				}
				if (index != static_cast<size_t>(RHIFormat::Unknown) &&
					(info.m_Family == RHIFormatFamily::Unknown ||
						info.m_Aspects == RHITextureAspect::None ||
						info.m_PlaneCount == 0 ||
						info.m_BytesPerBlock == 0))
				{
					return false;
				}
			}
			return true;
		}

		static_assert(ValidateRHIFormatInfos(), "RHI format metadata must match every RHIFormat entry.");
	}

	[[nodiscard]] constexpr inline const RHIFormatInfo& GetRHIFormatInfo(RHIFormat format) noexcept
	{
		const size_t index = static_cast<size_t>(format);
		return index < detail::RHIFormatInfos.size() ?
			detail::RHIFormatInfos[index] :
			detail::RHIFormatInfos[static_cast<size_t>(RHIFormat::Unknown)];
	}

	[[nodiscard]] constexpr inline bool AreRHIFormatsInSameFamily(RHIFormat lhs, RHIFormat rhs) noexcept
	{
		const RHIFormatFamily family = GetRHIFormatInfo(lhs).m_Family;
		return family != RHIFormatFamily::Unknown && family == GetRHIFormatInfo(rhs).m_Family;
	}
}
