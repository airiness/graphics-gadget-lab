#include "Precompiled.h"
#include "Shader.h"
#include "ShaderCompiler.h"

namespace gglab
{
	Shader::Shader(const ShaderDesc& desc) noexcept :
		m_Desc(desc)
	{
	}

	D3D12_SHADER_BYTECODE Shader::GetByteCode() const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "Shader must be compiled.");

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = m_Artifact.m_DxilBlob->GetBufferPointer();
		byteCode.BytecodeLength = m_Artifact.m_DxilBlob->GetBufferSize();

		return byteCode;
	}

	bool Shader::EnsureCompiled(ShaderCompiler& compiler) noexcept
	{
		ShaderDesc desc = m_Desc;

		if (desc.m_Entry.empty())
		{
			desc.m_Entry = DefaultEntry(desc.m_Stage);
		}

		auto artifact = compiler.EnsureCompiled(desc);

		auto newHash = CalculateFromBlob(artifact.m_DxilBlob.Get());
		auto isChanged = (m_Generation == 0) || (newHash != m_Artifact.m_Hash);

		m_Artifact = std::move(artifact);
		m_Artifact.m_Hash = newHash;

		std::error_code errorCode;
		m_Artifact.m_SourceTimeStamp = std::filesystem::exists(m_Desc.m_SourcePath, errorCode) ?
			std::filesystem::last_write_time(m_Desc.m_SourcePath, errorCode) :
			std::filesystem::file_time_type{};

		if (isChanged)
		{
			++m_Generation;
		}
		return isChanged;
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

	bool Shader::GetDxilContainerHash(const void* data, size_t size, ShaderHash128& outHash) noexcept
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

	ShaderHash128 Shader::CalculateFromBlob(ShaderBlob* blob) noexcept
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
}
