#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/DX12/Cache/PSOKey.h"
#include "Core/EnumFlags.h"

namespace gglab
{
	using ShaderBlob = IDxcBlobEncoding;

	enum class ShaderStage : uint32_t
	{
		Vertex,
		Pixel,
		Hull,
		Domain,
		Geometry,
		Mesh,
		Compute
	};

	enum class ShaderModel : uint32_t
	{
		SM_6_6,
		SM_6_7,
		SM_6_8
	};

	enum class ShaderCompileFlags : uint32_t
	{
		None = 0u,
		Debug = 1u << 0,
		Optimization = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(ShaderCompileFlags);

	struct ShaderDefine
	{
		std::wstring m_Name{};
		std::wstring m_Value{};

		bool operator==(const ShaderDefine&) const noexcept = default;
		auto operator<=>(const ShaderDefine&) const noexcept = default;
	};

	struct ShaderDesc
	{
		std::filesystem::path m_SourcePath{};
		ShaderStage m_Stage = ShaderStage::Pixel;
		ShaderModel m_Model = ShaderModel::SM_6_7;
		std::wstring m_Entry{};
		std::vector<std::filesystem::path> m_IncludeDirs;
		std::vector<ShaderDefine> m_Defines;
		ShaderCompileFlags m_Flags = ShaderCompileFlags::None;
		std::vector<std::wstring> m_ExtraArgs;
		std::wstring m_HlslVersion = L"2021";
		std::wstring m_OptLevel = L"O3";
	};

	struct ShaderCompileArtifact
	{
		std::filesystem::path m_DxilPath{};
		std::filesystem::path m_MetaPath{};
		ComPtr<ShaderBlob> m_DxilBlob{};
		ShaderHash128 m_Hash{};
		std::filesystem::file_time_type m_SourceTimeStamp{};
		bool m_FromCache = false;

		void Reset() noexcept
		{
			m_DxilPath.clear();
			m_MetaPath.clear();
			m_DxilBlob.Reset();
			m_Hash = {};
			m_SourceTimeStamp = {};
			m_FromCache = false;
		}
	};

	class ShaderManager;
	class Shader
	{
	public:
		explicit Shader(const ShaderDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Shader);
		~Shader() = default;

		D3D12_SHADER_BYTECODE GetByteCode() const noexcept;
		const ShaderDesc& GetDesc() const noexcept { return m_Desc; }
		const ShaderCompileArtifact& GetCompileArtifact() const noexcept { return m_Artifact; }
		uint64_t GetGeneration() const noexcept { return m_Generation; }
		bool IsValid() const noexcept { return m_Artifact.m_DxilBlob != nullptr; }

	private:
		void SetCompileArtifact(ShaderCompileArtifact artifact, bool changed) noexcept;

	private:
		ShaderDesc m_Desc;
		ShaderCompileArtifact m_Artifact;
		uint64_t m_Generation = 0;

		friend class ShaderManager;
	};
}
