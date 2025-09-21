#include "Precompiled.h"
#include "ShaderManager.h"
#include "Utility.h"

namespace gglab
{
	ShaderManager::ShaderManager() noexcept
	{
		utility::ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_DxcUtils)));
	}

	ShaderId ShaderManager::LoadShader(const std::filesystem::path& path, ShaderStage stage) noexcept
	{
		// path check
		if (!std::filesystem::exists(path))
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::LoadShader: File not found: {}", path.string());
			return ShaderId();
		}

		ShaderKey key{};
		key.m_PathHash = StringId(path.string());
		key.m_Stage = stage;

		{
			std::shared_lock lock(m_Mutex);
			if(auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}
		}

		ComPtr<ShaderBlob> blob;
		utility::ThrowIfFailed(m_DxcUtils->LoadFile(path.wstring().c_str(), nullptr, &blob));

		auto timestamp = std::filesystem::last_write_time(path);

		{
			std::unique_lock lock(m_Mutex);
			if(auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}

			ShaderRecord record{};
			record.m_Key = key;
			record.m_Path = path;
			record.m_Stage = stage;
			record.m_Blob = std::move(blob);
			record.m_Timestamp = timestamp;
			record.m_Hash = HashBlob(record.m_Blob.Get());

			const auto shaderId = static_cast<ShaderId>(m_Records.size());
			m_Records.push_back(std::move(record));
			m_KeyIdMap.emplace(key, shaderId);
			return shaderId;
		}
	}

	D3D12_SHADER_BYTECODE ShaderManager::GetBytecode(ShaderId shaderId) const noexcept
	{
		return D3D12_SHADER_BYTECODE();
	}

	const ComPtr<ShaderBlob>& ShaderManager::GetBlob(ShaderId shaderId) const noexcept
	{
		// TODO: insert return statement here
		return nullptr;
	}

	ShaderHash128 ShaderManager::GetHash(ShaderId shaderId) const noexcept
	{
		return ShaderHash128();
	}

	uint64_t ShaderManager::GetGeneration(ShaderId shaderId) const noexcept
	{
		return 0;
	}

	void ShaderManager::Preload(const std::vector<std::pair<std::filesystem::path, ShaderStage>>& shaders) noexcept
	{
	}

	int32_t ShaderManager::ReloadChanged() noexcept
	{
		return 0;
	}

	ShaderId ShaderManager::InsertOrGet(const ShaderKey& key, ComPtr<ShaderBlob>&& blob, ShaderStage stage) noexcept
	{
		if(auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
		{
			return it->second;
		}

		ShaderRecord record{};
		record.m_Key = key;
		record.m_Path = key.m_PathHash.GetString();


		return ShaderId();
	}

	bool ShaderManager::GetDxilValidatorHash(const void* data, size_t size, ShaderHash128& outHash) noexcept
	{
		constexpr size_t MinDxilSize = 20;
		if(data == nullptr || size < MinDxilSize)
		{
			return false;
		}

		// magic number for DirectX Container
		// https://llvm.org/docs/DirectX/DXContainer.html?utm_source=chatgpt.com#file-header
		static const unsigned char DXBCMagicNumber[] = { 'D', 'X', 'B', 'C' };
		if (std::memcmp(data, DXBCMagicNumber, 4) != 0)
		{
			return false;
		}
		const unsigned char* ptr = static_cast<const unsigned char*>(data);
		std::memcpy(&outHash.m_LowBits, ptr + 4, sizeof(uint64_t));
		std::memcpy(&outHash.m_HighBits, ptr + 12, sizeof(uint64_t));

		return true;
	}

	ShaderHash128 ShaderManager::HashBlob(ShaderBlob* blob) noexcept
	{
		ShaderHash128 hash{};
		if (!blob)
		{
			GGLAB_LOG_GRAPHICS_WARN("ShaderManager::HashBlob: blob is null.");
			return hash;
		}

		const auto* ptr = static_cast<const uint8_t*>(blob->GetBufferPointer());
		const auto size = blob->GetBufferSize();

		if(GetDxilValidatorHash(ptr, size, hash))
		{
			return hash;
		}

		// FNV-1a 64-bit hash
		GGLAB_LOG_GRAPHICS_WARN("ShaderManager::HashBlob: Failed to get DXIL validator hash, fallback to FNV-1a hash.");
		hash.m_LowBits = FNV1a64::HashBytes64(ptr, size);
		hash.m_HighBits = FNV1a64::HashBytes64(ptr, size, 0x9ae16a3b2f90404full);
		return hash;
	}
}
