#include "Core/Precompiled.h"
#include "Graphics/Asset/TextureArtifact.h"

#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace gglab
{
	namespace
	{
		class Sha256Builder
		{
		public:
			Sha256Builder() noexcept
			{
				if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
					&m_Algorithm,
					BCRYPT_SHA256_ALGORITHM,
					nullptr,
					0)))
				{
					return;
				}
				DWORD objectBytes = 0;
				DWORD resultBytes = 0;
				if (!BCRYPT_SUCCESS(BCryptGetProperty(
					m_Algorithm,
					BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&objectBytes),
					sizeof(objectBytes),
					&resultBytes,
					0)))
				{
					return;
				}
				m_Object.resize(objectBytes);
				if (!BCRYPT_SUCCESS(BCryptCreateHash(
					m_Algorithm,
					&m_Hash,
					m_Object.data(),
					static_cast<ULONG>(m_Object.size()),
					nullptr,
					0,
					0)))
				{
					m_Hash = nullptr;
				}
			}

			~Sha256Builder()
			{
				if (m_Hash)
				{
					BCryptDestroyHash(m_Hash);
				}
				if (m_Algorithm)
				{
					BCryptCloseAlgorithmProvider(m_Algorithm, 0);
				}
			}

			[[nodiscard]] bool IsValid() const noexcept { return m_Hash != nullptr; }

			template<class T>
			bool AddScalar(T value) noexcept
			{
				static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
				if constexpr (std::is_enum_v<T>)
				{
					using UnsignedType = std::make_unsigned_t<std::underlying_type_t<T>>;
					const UnsignedType encoded = static_cast<UnsignedType>(value);
					static_assert(std::endian::native == std::endian::little);
					return AddBytes(std::as_bytes(std::span{ &encoded, 1 }));
				}
				else
				{
					using UnsignedType = std::make_unsigned_t<T>;
					const UnsignedType encoded = static_cast<UnsignedType>(value);
					static_assert(std::endian::native == std::endian::little);
					return AddBytes(std::as_bytes(std::span{ &encoded, 1 }));
				}
			}

			bool AddBytes(std::span<const std::byte> bytes) noexcept
			{
				if (!m_Hash)
				{
					return false;
				}
				while (!bytes.empty())
				{
					const size_t chunkSize = std::min<size_t>(
						bytes.size(),
						std::numeric_limits<ULONG>::max());
					if (!BCRYPT_SUCCESS(BCryptHashData(
						m_Hash,
						reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
						static_cast<ULONG>(chunkSize),
						0)))
					{
						return false;
					}
					bytes = bytes.subspan(chunkSize);
				}
				return true;
			}

			[[nodiscard]] ArtifactContentDigest Finish() noexcept
			{
				ArtifactContentDigest digest{};
				if (!m_Hash || !BCRYPT_SUCCESS(BCryptFinishHash(
					m_Hash,
					reinterpret_cast<PUCHAR>(digest.m_Value.data()),
					static_cast<ULONG>(digest.m_Value.size()),
					0)))
				{
					return {};
				}
				return digest;
			}

		private:
			BCRYPT_ALG_HANDLE m_Algorithm = nullptr;
			BCRYPT_HASH_HANDLE m_Hash = nullptr;
			std::vector<UCHAR> m_Object;
		};
	}

	ArtifactContentDigest ComputeTextureArtifactContentDigest(
		const TextureAssetData& textureData) noexcept
	{
		if (!textureData.IsValid())
		{
			return {};
		}

		Sha256Builder builder;
		bool succeeded = builder.IsValid();
		succeeded &= builder.AddScalar(static_cast<uint32_t>(textureData.m_ResourceFormat));
		succeeded &= builder.AddScalar(static_cast<uint32_t>(textureData.m_ViewFormat));
		succeeded &= builder.AddScalar(static_cast<uint32_t>(textureData.m_SrvDimension));
		succeeded &= builder.AddScalar(textureData.m_Extent.m_Width);
		succeeded &= builder.AddScalar(textureData.m_Extent.m_Height);
		succeeded &= builder.AddScalar(textureData.m_Extent.m_Depth);
		succeeded &= builder.AddScalar(textureData.m_ArraySize);
		succeeded &= builder.AddScalar(textureData.m_MipLevels);
		succeeded &= builder.AddScalar(static_cast<uint32_t>(textureData.m_ColorSpace));
		succeeded &= builder.AddScalar(static_cast<uint64_t>(textureData.m_Subresources.size()));
		for (const TextureAssetSubresource& subresource : textureData.m_Subresources)
		{
			succeeded &= builder.AddScalar(subresource.m_DataOffset);
			succeeded &= builder.AddScalar(subresource.m_DataSize);
			succeeded &= builder.AddScalar(subresource.m_RowPitch);
			succeeded &= builder.AddScalar(subresource.m_SlicePitch);
			succeeded &= builder.AddScalar(subresource.m_Width);
			succeeded &= builder.AddScalar(subresource.m_Height);
			succeeded &= builder.AddScalar(subresource.m_Depth);
			succeeded &= builder.AddScalar(subresource.m_MipLevel);
			succeeded &= builder.AddScalar(subresource.m_ArraySlice);
		}
		succeeded &= builder.AddBytes(textureData.m_Pixels);
		if (!succeeded)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to compute a SHA-256 texture artifact digest.");
			return {};
		}
		return builder.Finish();
	}
}
