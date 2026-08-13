#pragma once
#include "Graphics/Asset/TextureAsset.h"
#include "Graphics/RHI/RHITextureValidation.h"

#include <cstdint>
#include <string_view>

namespace gglab
{
	class RHIDevice;

	struct TextureAssetValidationLimits
	{
		uint32_t m_MaxDimension = 16'384;
		uint32_t m_MaxArraySize = 2'048;
		uint64_t m_MaxSubresources = 32'768;
		uint64_t m_MaxPixelBytes = 2ull * 1024 * 1024 * 1024;
	};

	enum class TextureStructureValidationError : uint8_t
	{
		None,
		InvalidEnum,
		IncompatibleFormats,
		InvalidExtent,
		InvalidMipCount,
		InvalidArrayConfiguration,
		InvalidSubresourceCount,
		InvalidSubresourceIndex,
		DuplicateSubresource,
		MissingSubresource,
		NonCanonicalSubresourceOrder,
		InvalidSubresourceExtent,
		OffsetOverflow,
		OutOfBounds,
		InvalidRowPitch,
		InvalidSlicePitch,
		InvalidDataSize,
		ExceedsConfiguredLimit,
	};

	struct TextureStructureValidationResult
	{
		TextureStructureValidationError m_Error = TextureStructureValidationError::None;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Error == TextureStructureValidationError::None;
		}
	};

	enum class TextureUploadValidationDisposition : uint8_t
	{
		Valid,
		Corrupt,
		Unsupported,
	};

	struct TextureUploadValidationResult
	{
		TextureUploadValidationDisposition m_Disposition =
			TextureUploadValidationDisposition::Corrupt;
		TextureStructureValidationError m_StructureError = TextureStructureValidationError::None;
		RHITextureValidationError m_RHIError = RHITextureValidationError::None;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Disposition == TextureUploadValidationDisposition::Valid;
		}
	};

	[[nodiscard]] std::string_view TextureStructureValidationErrorText(
		TextureStructureValidationError error) noexcept;
	[[nodiscard]] std::string_view TextureUploadValidationDispositionText(
		TextureUploadValidationDisposition disposition) noexcept;

	// Validates decoded scalar declarations before any attacker-controlled
	// subresource or pixel allocation takes place.
	[[nodiscard]] TextureStructureValidationResult ValidateTextureAssetMetadata(
		const TextureAssetData& data, uint64_t declaredSubresourceCount,
		uint64_t declaredPixelBytes, TextureAssetValidationLimits limits = {}) noexcept;
	[[nodiscard]] TextureStructureValidationResult ValidateTextureAssetStructure(
		const TextureAssetData& data, TextureAssetValidationLimits limits = {}) noexcept;

	[[nodiscard]] RHITextureDesc BuildTextureRHITextureDesc(const TextureAssetData& data) noexcept;
	[[nodiscard]] RHITextureViewDesc BuildTextureRHISRVDesc(const TextureAssetData& data) noexcept;
	[[nodiscard]] TextureUploadValidationResult ValidateTextureUploadForDevice(
		const TextureAssetData& data, const RHIDevice& device,
		TextureAssetValidationLimits limits = {}) noexcept;
}
