#include "Precompiled.h"
#include "ShaderManager.h"
#include "ShaderCompiler.h"
#include "HResult.h"
#include "PathUtils.h"

namespace gglab
{
	ShaderManager::ShaderManager() noexcept
	{
		m_Compiler = std::make_unique<ShaderCompiler>();
	}

	ShaderId ShaderManager::LoadShader(const ShaderDesc& desc) noexcept
	{
		auto merge = MergeDefaultShaderDesc(desc);


		return ShaderId();
	}



	ShaderId ShaderManager::LoadShader(const std::filesystem::path& path, ShaderStage stage) noexcept
	{
		// Path check
		if (!std::filesystem::exists(path))
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::LoadShader: File not found: {}", path.string());
			return ShaderId();
		}

		// Make path canonical
		auto normalPath = utils::Canonical(path);


		ShaderDesc desc{};
		desc.m_SourcePath = normalPath;
		desc.m_Stage = stage;


		ShaderKey key{};
		key.m_PathHash = StringId(normalPath.string());
		key.m_Stage = stage;

		{
			std::shared_lock lock(m_Mutex);
			if (auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}
		}

		ComPtr<ShaderBlob> blob;
		GGLAB_HR(m_DxcUtils->LoadFile(path.wstring().c_str(), nullptr, &blob));

		auto timestamp = std::filesystem::last_write_time(path);

		{
			std::unique_lock lock(m_Mutex);
			if (auto it = m_KeyIdMap.find(key); it != m_KeyIdMap.end())
			{
				return it->second;
			}

			Shader shader{};

			ShaderDesc shaderDesc{};
			shaderDesc.

			record.m_Key = key;
			record.m_Path = path;
			record.m_Stage = stage;
			record.m_Blob = std::move(blob);
			record.m_Timestamp = timestamp;
			record.m_Hash = HashBlob(record.m_Blob.Get());

			ShaderId shaderId{ static_cast<uint32_t>(m_Records.size()) };
			m_Records.push_back(std::move(record));
			m_KeyIdMap.emplace(key, shaderId);
			return shaderId;
		}
	}

	D3D12_SHADER_BYTECODE ShaderManager::GetBytecode(ShaderId shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		D3D12_SHADER_BYTECODE bytecode{};
		if (shaderId.IsValid() && shaderId.Value() < m_Records.size())
		{
			const auto& record = m_Records[shaderId.Value()];
			if (record.m_Blob)
			{
				bytecode.pShaderBytecode = record.m_Blob->GetBufferPointer();
				bytecode.BytecodeLength = record.m_Blob->GetBufferSize();
				return bytecode;
			}
			else
			{
				GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::GetBytecode: Shader blob is null for shader ID {}", shaderId.Value());
			}
		}
		else
		{
			GGLAB_LOG_GRAPHICS_ERROR("ShaderManager::GetBytecode: Invalid shader ID {}", shaderId.Value());
		}

		return bytecode;
	}

	ShaderBlob* ShaderManager::GetBlob(ShaderId shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Records.size())
		{
			return m_Records[shaderId.Value()].m_Blob.Get();
		}
		return nullptr;
	}

	ShaderHash128 ShaderManager::GetHash(ShaderId shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Records.size())
		{
			return m_Records[shaderId.Value()].m_Hash;
		}
		return {};
	}

	uint64_t ShaderManager::GetGeneration(ShaderId shaderId) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (shaderId.IsValid() && shaderId.Value() < m_Records.size())
		{
			return m_Records[shaderId.Value()].m_Generation;
		}
		return 0;
	}


	void ShaderManager::Preload(const std::vector<std::pair<std::filesystem::path, ShaderStage>>& shaders) noexcept
	{
		for (const auto& [path, stage] : shaders)
		{
			LoadShader(path, stage);
		}
	}

	std::wstring Shader::DefaultEntry(ShaderStage& stage) noexcept
	{
		switch (stage)
		{
		case ShaderStage::Vertex:
			return L"VSMain";
		case ShaderStage::Pixel:
			return L"PSMain";
		case ShaderStage::Hull:
			return L"HSMain";
		case ShaderStage::Domain:
			return L"DSMain";
		case ShaderStage::Geometry:
			return L"GSMain";
		case ShaderStage::Mesh:
			return L"MSMain";
		case ShaderStage::Compute:
			return L"CSMain";
		default:
			break;
		}
		return L"Main";
	}

	bool ShaderManager::GetDxilContainerHash(const void* data, size_t size, ShaderHash128& outHash) noexcept
	{
		constexpr size_t MinDxilSize = 20;
		if (data == nullptr || size < MinDxilSize)
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

		if (GetDxilContainerHash(ptr, size, hash))
		{
			return hash;
		}

		// FNV-1a 64-bit hash
		GGLAB_LOG_GRAPHICS_WARN("ShaderManager::HashBlob: Failed to get DXIL validator hash, fallback to FNV-1a hash.");
		hash.m_LowBits = FNV1a64::HashBytes64(ptr, size);
		hash.m_HighBits = FNV1a64::HashBytes64(ptr, size, 0x9ae16a3b2f90404full);
		return hash;
	}

	ShaderDesc ShaderManager::MergeDefaultShaderDesc(const ShaderDesc& desc) noexcept
	{
		ShaderDesc merge = desc;
		if (merge.m_Entry.empty())
		{
			merge.m_Entry = DefaultEntry(merge.m_Stage);
		}

		merge.m_IncludeDirs.insert(merge.m_IncludeDirs.end(), { L"Shaders", L"Shaders/Common", L"Shaders/PBR", L"Shaders/Passes" });
		//merge.m_Defines.insert(merge.m_Defines.end(), {});
		if (merge.m_HlslVersion.empty()) { merge.m_HlslVersion = L"2021"; }
		if (merge.m_OptLevel.empty()) { merge.m_OptLevel = L"O3"; }
		if (merge.m_Flags == ShaderCompileFlag::None)
		{
			merge.m_Flags = ShaderCompileFlag::Optimization | (IsDebuggerPresent() ? ShaderCompileFlag::Debug : ShaderCompileFlag::None);
		}
		return merge;
	}
}
