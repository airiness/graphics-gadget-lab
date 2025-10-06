#include "Precompiled.h"
#include "ShaderCompiler.h"
#include "HResult.h"
#include "StringId.h"
#include "StringUtils.h"
#include "FNV1a.h"

namespace gglab
{
	ShaderCompiler::ShaderCompiler() noexcept
	{
		GGLAB_HR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_Utils)));
		GGLAB_HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Compiler)));

		std::filesystem::path defaultRootDir = std::filesystem::path{ SOLUTION_DIR } /
			L"Build" / L"ShaderCache" / (IsDebuggerPresent() ? L"Debug" : L"Release");

		SetCacheRootDirectory(defaultRootDir);
	}

	void ShaderCompiler::SetCacheRootDirectory(std::filesystem::path root) noexcept
	{
		m_CacheRootDir = utils::Canonical(root);
		const auto result = utils::CreateDirectoryIfNotExist(m_CacheRootDir);

		GGLAB_ASSERT_MSG(result, "Create Shader cache root directory failed.");
	}

	ShaderCompileResult ShaderCompiler::EnsureCompiled(const ShaderDesc& desc) noexcept
	{
		ShaderCompileResult result{};
		const auto keyStr = BuildKeyString(desc);
		const auto keyHex = HashKeyString(keyStr);
		const auto dxil = MakeCacheDxilPath(keyHex, desc.m_Stage);

		auto meta = dxil;
		meta.replace_extension(L"meta.txt");

		result.m_DxilPath = dxil;
		result.m_MetaPath = meta;

		// Exist
		std::error_code errorCode;
		if (std::filesystem::exists(dxil, errorCode) && std::filesystem::exists(meta, errorCode))
		{
			if (IsMetaUpToDate(meta))
			{
				ComPtr<IDxcBlobEncoding> blobEncoding;
				GGLAB_HR(m_Utils->LoadFile(dxil.c_str(), nullptr, &blobEncoding));
				result.m_DxilBlob = blobEncoding;
				result.m_FromCache = true;
				return result;
			}
		}

		// Compile is not exist
		std::vector<std::filesystem::path> deps;
		auto dxilBlob = CompileShader(desc, deps);

		// Save binary
		utils::WriteFileBinary(dxil, dxilBlob->GetBufferPointer(), dxilBlob->GetBufferSize());
		WriteMeta(meta, desc, deps);

		// result
		ComPtr<IDxcBlobEncoding> blobEncoding;
		GGLAB_HR(m_Utils->LoadFile(dxil.c_str(), nullptr, &blobEncoding));
		result.m_DxilBlob = blobEncoding;
		result.m_FromCache = false;
		return result;
	}

	std::filesystem::path ShaderCompiler::MakeCacheDxilPath(const std::wstring& keyHex, ShaderStage stage) const noexcept
	{
		std::wstring extension;
		switch (stage)
		{
		case ShaderStage::Vertex:
			extension = L"vs.dxil";
			break;
		case ShaderStage::Pixel:
			extension = L"ps.dxil";
			break;
		case ShaderStage::Hull:
			extension = L"hs.dxil";
			break;
		case ShaderStage::Domain:
			extension = L"ds.dxil";
			break;
		case ShaderStage::Geometry:
			extension = L"gs.dxil";
			break;
		case ShaderStage::Mesh:
			extension = L"ms.dxil";
			break;
		case ShaderStage::Compute:
			extension = L"cs.dxil";
			break;
		default:
			extension = L".dxil";
			break;
		}

		auto path = m_CacheRootDir / keyHex.substr(0, 2) / keyHex.substr(2, 2) / (keyHex + extension);
		utils::CreateParentDirectoryIfNotExist(path);
		return path;
	}

	ComPtr<IDxcBlob> ShaderCompiler::CompileShader(const ShaderDesc& desc, std::vector<std::filesystem::path>& outDeps) const noexcept
	{
		ComPtr<IDxcBlobEncoding> src;
		GGLAB_HR(m_Utils->LoadFile(utils::Canonical(desc.m_SourcePath).c_str(), nullptr, &src));

		DxcBuffer buffer{};
		buffer.Ptr = src->GetBufferPointer();
		buffer.Size = src->GetBufferSize();
		buffer.Encoding = DXC_CP_UTF8;

		ComPtr<ShaderIncludeHandler> includeHandler;
		GGLAB_HR(MakeAndInitialize<ShaderIncludeHandler>(&includeHandler, m_Utils, desc.m_IncludeDirs));

		std::vector<const wchar_t*> args;
		const std::wstring entry = desc.m_Entry.empty() ? L"Main" : desc.m_Entry;
		const std::wstring target = ToTarget(desc.m_Stage, desc.m_Model);

		args.push_back(L"-E");
		args.push_back(entry.c_str());

		args.push_back(L"-T");
		args.push_back(target.c_str());

		args.push_back(L"-HV");
		args.push_back(desc.m_HlslVersion.c_str());

		if (Test(desc.m_Flags, ShaderCompileFlag::Debug))
		{
			args.insert(args.end(), { DXC_ARG_DEBUG, L"-Qembed_debug" });
		}
		else
		{
			args.insert(args.end(), { L"-Qstrip_debug", L"-Qstrip_reflect" });
		}

		if (Test(desc.m_Flags, ShaderCompileFlag::Optimization))
		{
			const std::wstring optFlag = L"-" + desc.m_OptLevel;
			args.push_back(optFlag.c_str());
		}
		else
		{
			args.push_back(DXC_ARG_SKIP_OPTIMIZATIONS);
		}

		// -I
		std::vector<std::wstring> includes;
		includes.reserve(desc.m_IncludeDirs.size());
		for (const auto& path : desc.m_IncludeDirs)
		{
			includes.push_back(utils::Canonical(path).wstring());
			args.push_back(L"-I");
			args.push_back(includes.back().c_str());
		}

		// -D
		std::vector<std::wstring> defines;
		defines.resize(desc.m_Defines.size());
		for (const auto& define : desc.m_Defines)
		{
			std::wstring arg = define.m_Name;
			if (!define.m_Value.empty()) { arg += L"=" + define.m_Value; }
			defines.push_back(std::move(arg));
		}

		for (auto& define : defines)
		{
			args.push_back(L"-D");
			args.push_back(define.c_str());
		}

		// Extra args
		for (auto& extra : desc.m_ExtraArgs)
		{
			args.push_back(extra.c_str());
		}

		// Compile
		ComPtr<IDxcResult> result;
		GGLAB_HR(m_Compiler->Compile(&buffer, args.data(), (UINT32)args.size(), includeHandler.Get(), IID_PPV_ARGS(&result)));

		HRESULT status = S_OK;
		result->GetStatus(&status);
		if (FAILED(status))
		{
			ComPtr<IDxcBlobUtf8> log;
			result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&log), nullptr);
			if (log && log->GetStringLength())
			{
				GGLAB_LOG_GRAPHICS_ERROR("DXC error:\n{}", static_cast<const char*>(log->GetBufferPointer()));
			}
			GGLAB_HR(status);
		}

		// Record includes
		outDeps = includeHandler->Includes();

		ComPtr<IDxcBlob> dxilBlob;
		GGLAB_HR(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxilBlob), nullptr));

		return dxilBlob;
	}

	void ShaderCompiler::WriteMeta(const std::filesystem::path& meta, const ShaderDesc& desc,
		const std::vector<std::filesystem::path>& deps) const noexcept
	{
		const auto created = utils::CreateParentDirectoryIfNotExist(meta);

		GGLAB_ASSERT_MSG(created, "Create shader meta failed.");

		std::ofstream out(meta, std::ios::binary);
		if (!out) { return; }

		const auto src = utils::Canonical(desc.m_SourcePath).string();
		const auto entry = utils::ToString(desc.m_Entry);
		const auto target = utils::ToString(ToTarget(desc.m_Stage, desc.m_Model));

		out << "src=" << src << "\n";
		out << "entry=" << entry << "\n";
		out << "target=" << target << "\n";

		out << "defines=";
		for (size_t i = 0; i < desc.m_Defines.size(); ++i)
		{
			const auto& d = desc.m_Defines[i];
			out << utils::ToString(d.m_Name) << "=" << utils::ToString(d.m_Value);
			if (i + 1 < desc.m_Defines.size())
			{
				out << ";";
			}
		}
		out << "\n";

		out << "includes=";
		for (size_t i = 0; i < desc.m_IncludeDirs.size(); ++i)
		{
			out << utils::Canonical(desc.m_IncludeDirs[i]).string();
			if (i + 1 < desc.m_IncludeDirs.size())
			{
				out << ";";
			}
		}
		out << "\n";

		out << "extra=";
		for (size_t i = 0; i < desc.m_ExtraArgs.size(); ++i)
		{
			out << utils::ToString(desc.m_ExtraArgs[i]);
			if (i + 1 < desc.m_ExtraArgs.size())
			{
				out << ";";
			}
		}
		out << "\n";

		for (const auto& d : deps)
		{
			out << "dep=" << d.string() << "|mtime=" << utils::LastWriteTimeTicks(d) << "\n";
		}
	}

	bool ShaderCompiler::IsMetaUpToDate(const std::filesystem::path& meta) const noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(meta, errorCode)) { return false; }

		std::ifstream in(meta, std::ios::binary);
		if (!in) { return false; }

		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("dep=", 0) != 0)
			{
				continue;
			}

			const auto bar = line.find("|");
			const auto eq = line.find("mtime=", bar == std::string::npos ? 0 : bar);

			if (bar == std::string::npos || eq == std::string::npos)
			{
				continue;
			}

			const std::string p = line.substr(4, bar - 4);
			const long long ticksSaved = std::strtoll(line.c_str() + eq + 6, nullptr, 10);

			const auto fp = utils::Canonical(std::filesystem::path(p));
			if (!std::filesystem::exists(fp, errorCode))
			{
				return false;
			}

			if (utils::LastWriteTimeTicks(fp) != ticksSaved)
			{
				return false;
			}
		}
		return true;
	}

	std::wstring ShaderCompiler::ToTarget(ShaderStage stage, ShaderModel model) noexcept
	{
		std::wstring target;
		switch (stage)
		{
		case ShaderStage::Vertex:
			target += L"vs_";
			break;
		case ShaderStage::Pixel:
			target += L"ps_";
			break;
		case ShaderStage::Hull:
			target += L"hs_";
			break;
		case ShaderStage::Domain:
			target += L"ds_";
			break;
		case ShaderStage::Geometry:
			target += L"gs_";
			break;
		case ShaderStage::Mesh:
			target += L"ms_";
			break;
		case ShaderStage::Compute:
			target += L"cs_";
			break;
		default:
			GGLAB_UNREACHABLE("Unknown ShaderStage.");
		}

		switch (model)
		{
		case ShaderModel::SM_6_6:
			target += L"6_6";
			break;
		case ShaderModel::SM_6_7:
			target += L"6_7";
			break;
		case ShaderModel::SM_6_8:
			target += L"6_8";
			break;
		default:
			GGLAB_UNREACHABLE("Unknown ShaderModel.");
		}

		return target;
	}

	std::wstring ShaderCompiler::BuildKeyString(const ShaderDesc& desc) noexcept
	{
		const auto src = utils::Canonical(desc.m_SourcePath).wstring();
		const auto entry = desc.m_Entry.empty() ? std::wstring(L"Main") : desc.m_Entry;
		const auto target = ToTarget(desc.m_Stage, desc.m_Model);

		std::wstring str;
		str.reserve(1024);

		str += L"src:" + src + L";";
		str += L"entry:" + entry + L";";
		str += L"target:" + target + L";";
		str += L"hv:" + desc.m_HlslVersion + L";";
		str += L"optimization:" + desc.m_OptLevel + L";";

		// defines
		std::vector<ShaderDefine> defines = desc.m_Defines;
		std::sort(defines.begin(), defines.end(),
			[](const auto& a, const auto& b)
			{
				return (a.m_Name < b.m_Name) || (a.m_Name == b.m_Name && a.m_Value < b.m_Value);
			});
		str += L"defines:";
		for (auto& define : defines)
		{
			str += define.m_Name + L"=" + define.m_Value + L",";
		}

		// include dirs
		std::vector<std::filesystem::path> includes = desc.m_IncludeDirs;
		std::sort(includes.begin(), includes.end());
		str += L";includes:";
		for (auto& path : includes)
		{
			str += utils::Canonical(path).wstring() + L",";
		}

		// extra args
		for (auto& arg : desc.m_ExtraArgs)
		{
			str += arg + L",";
		}

		return str;
	}

	std::wstring ShaderCompiler::HashKeyString(std::wstring_view keyStr) noexcept
	{
		const auto* bytes = reinterpret_cast<const uint8_t*>(keyStr.data());
		size_t size = keyStr.size();

		uint64_t low = Crc64(keyStr);
		uint64_t high = FNV1a64::HashBytes64(bytes, size, 0x9ae16a3b2f90404full);

		std::wstring heyHash = std::format(L"{:016x}{:016x}", high, low);
		return heyHash;
	}
}
