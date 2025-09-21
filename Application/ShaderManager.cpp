#include "Precompiled.h"
#include "ShaderManager.h"


namespace gglab
{
	ShaderManager::ShaderManager() noexcept
	{
	}

	ShaderId ShaderManager::LoadCSO(const std::filesystem::path& path, ShaderStage stage) noexcept
	{
		return ShaderId();
	}

	D3D12_SHADER_BYTECODE ShaderManager::GetBytecode(ShaderId shaderId) const noexcept
	{
		return D3D12_SHADER_BYTECODE();
	}

	const ComPtr<ID3DBlob>& ShaderManager::GetBlob(ShaderId shaderId) const noexcept
	{
		// TODO: insert return statement here
		return nullptr;
	}

	ShaderHash128 ShaderManager::GetHash(ShaderId shaderId) const noexcept
	{
		return ShaderHash128();
	}

	uint64_t ShaderManager::GetGeneration() const noexcept
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

	ShaderId ShaderManager::InsertOrGet(const ShaderKey& key, ComPtr<ID3DBlob>&& blob, ShaderStage stage) noexcept
	{
		return ShaderId();
	}

	bool ShaderManager::GetDxilValidatorHash(const void* data, size_t size, ShaderHash128& outHash) noexcept
	{
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

	ShaderHash128 ShaderManager::HasBlob(ID3DBlob* blob) noexcept
	{
		ShaderHash128 key{};
		if (!blob)
		{
			return key;	
		}

		

		const auto* ptr = static_cast<const uint8_t*>(blob->GetBufferPointer());
		const auto size = blob->GetBufferSize();

		//key.m_LowBits = FNV1a64::HashValue(ptr);

	}
}
