#pragma once
#include "Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <string_view>

namespace gglab
{
	enum class RHITextureValidationError : uint8_t
	{
		None,
		InvalidFormat,
		InvalidUsage,
		InvalidExtent,
		InvalidArraySize,
		InvalidMipLevelCount,
		InvalidSampleCount,
		InvalidDimension,
		InvalidClearValue,
		IncompatibleViewFormat,
		IncompatibleViewDimension,
		InvalidSubresourceRange,
		UnsupportedUploadFormat,
		InvalidUploadSubresourceCount,
		InvalidUploadSubresourceData,
		InvalidUploadRowPitch,
		InvalidUploadSlicePitch,
	};

	struct RHITextureValidationResult
	{
		RHITextureValidationError m_Error = RHITextureValidationError::None;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Error == RHITextureValidationError::None;
		}
	};

	struct RHITextureSupportResult
	{
		RHITextureValidationError m_ValidationError = RHITextureValidationError::None;
		bool m_Supported = false;

		[[nodiscard]] constexpr bool IsDescriptionValid() const noexcept
		{
			return m_ValidationError == RHITextureValidationError::None;
		}

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return IsDescriptionValid() && m_Supported;
		}
	};

	[[nodiscard]] std::string_view RHITextureValidationErrorText(
		RHITextureValidationError error) noexcept;
	[[nodiscard]] RHITextureValidationResult ValidateRHITextureDesc(
		const RHITextureDesc& desc) noexcept;
	[[nodiscard]] RHITextureValidationResult ValidateRHITextureViewDesc(
		const RHITextureDesc& textureDesc, const RHITextureViewDesc& viewDesc) noexcept;
	[[nodiscard]] RHITextureValidationResult ValidateRHITextureUploadData(
		const RHITextureDesc& textureDesc, const RHITextureUploadData& uploadData) noexcept;
}
