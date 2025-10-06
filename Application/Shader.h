#pragma once
#include "PSOKey.h"
#include "EnumFlags.h"

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

	enum class ShaderCompileFlag : uint32_t
	{
		None = 0u,
		Debug = 1u << 0,
		Optimization = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(ShaderCompileFlag);

	struct ShaderDefine
	{
		std::wstring m_Name;
		std::wstring m_Value;
	};

	struct ShaderDesc
	{
		std::filesystem::path m_SourcePath;
		ShaderStage m_Stage;
		ShaderModel m_Model;
		std::wstring m_Entry = L"";
		std::vector<std::filesystem::path> m_IncludeDirs;
		std::vector<ShaderDefine> m_Defines;
		ShaderCompileFlag m_Flags;
		std::vector<std::wstring> m_ExtraArgs;
		std::wstring m_HlslVersion = L"2021";
		std::wstring m_OptLevel = L"03";

	};

	struct ShaderCompileSnapshot
	{
		std::filesystem::path m_DxilPath;
		std::filesystem::path m_MetaPath;
		ComPtr<ShaderBlob> m_Blob;
		ShaderHash128 m_Hash;
		bool m_FromCache = false;
		std::filesystem::file_time_type m_SourceTimeStamp{};
	};

	class ShaderCompiler;

	class Shader
	{
	public:
		explicit Shader(const ShaderDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(Shader);
		~Shader() = default;

		bool EnsureCompiled(ShaderCompiler& compiler) noexcept;

		bool ForceRecompile(ShaderCompiler& compiler) noexcept;

		D3D12_SHADER_BYTECODE GetByteCode() const noexcept;
		const ShaderDesc& GetDesc() const noexcept { return m_Desc; }
		const ShaderCompileSnapshot& GetSnapshot() const noexcept { return m_Snapshot; }
		uint64_t GetGeneration() const noexcept { return m_Generation; }
		bool IsValid() const noexcept { return m_Snapshot.m_Blob != nullptr; }

	private:
		ShaderDesc m_Desc;
		ShaderCompileSnapshot m_Snapshot;
		uint64_t m_Generation = 0;
	};
}
