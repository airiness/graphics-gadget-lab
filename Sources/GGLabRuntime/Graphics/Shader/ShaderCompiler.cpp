#include "Graphics/Shader/ShaderCompiler.h"
#include "Core/CoreMacros.h"
#include "Core/Hash/KeyHash.h"
#include "Core/Platform/Win/HResult.h"
#include "Core/Log/LogMacros.h"
#include "Core/Platform/Win/ComTypes.h"
#include "Core/Platform/Win/Win32StringUtils.h"
#include "Core/StringId.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/StringUtils.h"
#include "Graphics/RHI/Vulkan/VulkanCoordinatePolicy.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"

#include <dxcapi.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gglab
{
	struct ShaderCompiler::Impl
	{
		ComPtr<IDxcUtils> m_Utils;
		ComPtr<IDxcCompiler3> m_Compiler;
	};

	namespace
	{
		[[nodiscard]] constexpr std::string_view ShaderBinaryFormatText(
			ShaderBinaryFormat format) noexcept
		{
			switch (format)
			{
			case ShaderBinaryFormat::Dxil:
				return "dxil";
			case ShaderBinaryFormat::SpirV:
				return "spirv";
			case ShaderBinaryFormat::Unknown:
				break;
			}
			return "unknown";
		}

		[[nodiscard]] constexpr std::string_view SpirVTargetEnvironmentText(
			ShaderSpirVTargetEnvironment environment) noexcept
		{
			switch (environment)
			{
			case ShaderSpirVTargetEnvironment::None:
				return "none";
			case ShaderSpirVTargetEnvironment::Vulkan1_3:
				return "vulkan1.3";
			}
			return "unknown";
		}

		[[nodiscard]] constexpr bool IsVertexProducingStage(ShaderStage stage) noexcept
		{
			return stage == ShaderStage::Vertex || stage == ShaderStage::Domain ||
				stage == ShaderStage::Geometry || stage == ShaderStage::Mesh;
		}

		[[nodiscard]] bool IsCompilerOwnedExtraArgument(std::wstring_view argument) noexcept
		{
			if (argument.empty())
			{
				return false;
			}
			if (argument.front() == L'@')
			{
				return true;
			}

			std::wstring lower(argument);
			std::ranges::transform(lower, lower.begin(),
				[](wchar_t value) noexcept { return static_cast<wchar_t>(::towlower(value)); });
			const auto isOption = [&lower](std::wstring_view option) noexcept
				{
					return lower == option ||
						(lower.size() > option.size() && lower.starts_with(option) &&
							lower[option.size()] == L'=');
				};

			if (lower.starts_with(L"-fvk-") || lower.starts_with(L"/fvk-") ||
				lower.starts_with(L"-fspv-") || lower.starts_with(L"/fspv-"))
			{
				return true;
			}

			constexpr std::array CompilerOwnedOptions{
				L"-e", L"/e", L"-t", L"/t", L"-hv", L"/hv", L"-spirv", L"/spirv",
				L"-zi", L"/zi", L"-qembed_debug", L"/qembed_debug", L"-qstrip_debug",
				L"/qstrip_debug", L"-qstrip_reflect", L"/qstrip_reflect", L"-fcgl",
				L"/fcgl", L"-ast-dump", L"/ast-dump", L"-dumpbin", L"/dumpbin",
			};
			if (std::ranges::any_of(CompilerOwnedOptions, isOption))
			{
				return true;
			}

			constexpr std::array OptimizationOptions{
				L"-o0", L"/o0",
				L"-o1", L"/o1",
				L"-o2", L"/o2",
				L"-o3", L"/o3",
				L"-od", L"/od",
			};
			return std::ranges::find(OptimizationOptions, lower) != OptimizationOptions.end();
		}

		class ShaderIncludeHandler final
			: public Microsoft::WRL::RuntimeClass<
			Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDxcIncludeHandler>
		{
		public:
			HRESULT RuntimeClassInitialize(ComPtr<IDxcUtils> utils,
				const std::vector<std::filesystem::path>& includeDirs) noexcept
			{
				m_Utils = utils;
				m_IncludeDirs = includeDirs;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE LoadSource(_In_z_ LPCWSTR pFilename,
				_COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
			{
				const auto path = utils::Canonical(pFilename);
				ComPtr<IDxcBlobEncoding> blob;

				if (SUCCEEDED(m_Utils->LoadFile(path.c_str(), nullptr, &blob)))
				{
					m_Includes.push_back(path);
					*ppIncludeSource = blob.Detach();
					return S_OK;
				}

				for (auto& dir : m_IncludeDirs)
				{
					const auto pathInDir = utils::Canonical(dir / pFilename);
					if (SUCCEEDED(m_Utils->LoadFile(pathInDir.c_str(), nullptr, &blob)))
					{
						m_Includes.push_back(pathInDir);
						*ppIncludeSource = blob.Detach();
						return S_OK;
					}
				}

				*ppIncludeSource = nullptr;
				return E_FAIL;
			}

			const std::vector<std::filesystem::path>& Includes() const noexcept
			{
				return m_Includes;
			}

		private:
			ComPtr<IDxcUtils> m_Utils;
			std::vector<std::filesystem::path> m_IncludeDirs;
			std::vector<std::filesystem::path> m_Includes;
		};

		ShaderBinary CopyShaderBinary(IDxcBlob* blob) noexcept
		{
			if (blob == nullptr || blob->GetBufferSize() == 0)
			{
				return {};
			}

			ShaderBinary binary(blob->GetBufferSize());
			std::memcpy(binary.Data(), blob->GetBufferPointer(), blob->GetBufferSize());
			return binary;
		}
	}

	ShaderCompileValidationResult ValidateShaderDesc(
		const ShaderDesc& desc, std::wstring_view activeDxcVersion) noexcept
	{
		const auto reject = [](ShaderCompileValidationError error, std::wstring message) noexcept
			{
				return ShaderCompileValidationResult{
					.m_Error = error,
					.m_Message = std::move(message),
				};
			};

		if (desc.m_Target.m_BinaryFormat != ShaderBinaryFormat::Dxil &&
			desc.m_Target.m_BinaryFormat != ShaderBinaryFormat::SpirV)
		{
			return reject(ShaderCompileValidationError::UnsupportedBinaryFormat,
				L"Shader binary format is unsupported.");
		}
		if (desc.m_SourcePath.empty())
		{
			return reject(
				ShaderCompileValidationError::EmptySourcePath, L"Shader source path is empty.");
		}
		if (utils::Canonical(desc.m_SourcePath) != desc.m_SourcePath)
		{
			return reject(ShaderCompileValidationError::SourcePathNotCanonical,
				L"Shader source path is not canonical; normalize the descriptor before compiling.");
		}
		if (desc.m_Entry.empty())
		{
			return reject(ShaderCompileValidationError::EmptyEntryPoint,
				L"Shader entry point is empty; normalize the descriptor before compiling.");
		}
		if (activeDxcVersion.empty() || desc.m_Target.m_DxcVersion != activeDxcVersion)
		{
			return reject(ShaderCompileValidationError::CompilerIdentityMismatch,
				L"Shader target DXC identity does not match the active compiler.");
		}

		if (desc.m_Target.m_BinaryFormat == ShaderBinaryFormat::SpirV)
		{
			const ShaderCompileTarget expected = ShaderCompiler::MakeVulkanSpirVTarget(desc.m_Stage);
			if (desc.m_Target.m_SpirVTargetEnvironment != expected.m_SpirVTargetEnvironment)
			{
				return reject(ShaderCompileValidationError::UnsupportedSpirVTargetEnvironment,
					L"SPIR-V target environment does not match the active Vulkan shader contract.");
			}
			if (desc.m_Target.m_BindingABIRevision != expected.m_BindingABIRevision)
			{
				return reject(ShaderCompileValidationError::UnsupportedBindingABIRevision,
					L"Shader binding ABI revision does not match the active Vulkan contract.");
			}
			if (desc.m_Target.m_CoordinateOptions != expected.m_CoordinateOptions)
			{
				return reject(ShaderCompileValidationError::InvalidCoordinateOptions,
					L"Shader coordinate options do not match the active stage and backend.");
			}
		}
		else if (desc.m_Target.m_SpirVTargetEnvironment != ShaderSpirVTargetEnvironment::None ||
			desc.m_Target.m_BindingABIRevision != 0 ||
			desc.m_Target.m_CoordinateOptions != ShaderCoordinateOptions::None)
		{
			return reject(ShaderCompileValidationError::UnexpectedSpirVTargetState,
				L"DXIL shader target contains SPIR-V-only state.");
		}

		for (const std::wstring& argument : desc.m_ExtraArgs)
		{
			if (IsCompilerOwnedExtraArgument(argument))
			{
				return reject(ShaderCompileValidationError::ReservedExtraArgument,
					std::format(L"Shader extra argument '{}' is owned by the normalized target.",
						argument));
			}
		}
		return {};
	}

	ShaderCompiler::ShaderCompiler(
		std::filesystem::path sourceRoot, std::filesystem::path cacheRoot) noexcept :
		m_Impl(std::make_unique<Impl>())
	{
		GGLAB_HR(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_Impl->m_Utils)));
		GGLAB_HR(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_Impl->m_Compiler)));
		m_DxcVersion = QueryDxcVersion();

		SetSourceRootDirectory(std::move(sourceRoot));
		SetCacheRootDirectory(std::move(cacheRoot));
	}

	ShaderCompiler::~ShaderCompiler() = default;

	void ShaderCompiler::SetSourceRootDirectory(std::filesystem::path root) noexcept
	{
		m_SourceRootDir = utils::Canonical(root);
	}

	void ShaderCompiler::SetCacheRootDirectory(std::filesystem::path root) noexcept
	{
		m_CacheRootDir = utils::Canonical(root);
		const auto result = utils::CreateDirectoryIfNotExist(m_CacheRootDir);

		GGLAB_ASSERT_MSG(result, "Create Shader cache root directory failed.");
	}

	ShaderCompileArtifact ShaderCompiler::CompileOrLoadArtifact(const ShaderDesc& desc) noexcept
	{
		const ShaderCompileValidationResult validation = ValidateShaderDesc(desc, m_DxcVersion);
		if (!validation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Shader compile descriptor validation failed: {}",
				utils::ToString(validation.m_Message));
			return {};
		}

		ShaderCompileArtifact artifact{};
		const auto recipeHash = ComputeRecipeHash(desc);
		const auto keyHex = ToHex(recipeHash);
		const auto binaryPath =
			MakeCacheBinaryPath(keyHex, desc.m_Stage, desc.m_Target.m_BinaryFormat);

		auto meta = binaryPath;
		meta.replace_extension(L"meta.txt");

		artifact.m_BinaryPath = binaryPath;
		artifact.m_MetaPath = meta;
		artifact.m_Target = desc.m_Target;

		// Exist
		std::error_code errorCode;
		if (std::filesystem::exists(binaryPath, errorCode) &&
			std::filesystem::exists(meta, errorCode))
		{
			if (IsMetaUpToDate(meta, desc, recipeHash))
			{
				ComPtr<IDxcBlobEncoding> blobEncoding;
				GGLAB_HR(m_Impl->m_Utils->LoadFile(binaryPath.c_str(), nullptr, &blobEncoding));
				ShaderBinary cachedBinary = CopyShaderBinary(blobEncoding.Get());
				if (IsBinaryFormat(cachedBinary, artifact.GetBinaryFormat()))
				{
					artifact.m_Binary = std::move(cachedBinary);
					artifact.m_FromCache = true;
					artifact.m_Hash = ComputeHashFromBinary(
						artifact.m_Binary, artifact.GetBinaryFormat());
					return artifact;
				}
			}
		}

		// Compile is not exist
		std::vector<std::filesystem::path> deps;
		auto binary = CompileShader(desc, deps);
		if (!IsBinaryFormat(binary, artifact.GetBinaryFormat()))
		{
			return {};
		}

		// Save binary
		utils::WriteFileBinary(binaryPath, binary.Data(), binary.SizeInBytes());
		WriteMeta(meta, desc, deps, recipeHash);

		// result
		artifact.m_Binary = std::move(binary);
		artifact.m_FromCache = false;
		artifact.m_Hash = ComputeHashFromBinary(
			artifact.m_Binary, artifact.GetBinaryFormat());
		return artifact;
	}

	ShaderDesc ShaderCompiler::NormalizeShaderDesc(const ShaderDesc& userDesc) const noexcept
	{
		ShaderDesc desc = userDesc;

		if (desc.m_Entry.empty())
		{
			desc.m_Entry = DefaultEntry(desc.m_Stage);
		}

		desc.m_IncludeDirs.insert(desc.m_IncludeDirs.end(),
			m_DefaultShaderConfig.m_IncludeDirs.begin(), m_DefaultShaderConfig.m_IncludeDirs.end());
		desc.m_Defines.insert(desc.m_Defines.end(), m_DefaultShaderConfig.m_Defines.begin(),
			m_DefaultShaderConfig.m_Defines.end());
		desc.m_ExtraArgs.insert(desc.m_ExtraArgs.end(), m_DefaultShaderConfig.m_ExtraArgs.begin(),
			m_DefaultShaderConfig.m_ExtraArgs.end());
		if (desc.m_Target.m_HlslVersion.empty())
		{
			desc.m_Target.m_HlslVersion = m_DefaultShaderConfig.m_Target.m_HlslVersion;
		}
		if (desc.m_Target.m_OptimizationLevel.empty())
		{
			desc.m_Target.m_OptimizationLevel =
				m_DefaultShaderConfig.m_Target.m_OptimizationLevel;
		}
		if (desc.m_Target.m_Flags == ShaderCompileFlags::None)
		{
			desc.m_Target.m_Flags = m_DefaultShaderConfig.m_Target.m_Flags;
		}
		desc.m_Target.m_DxcVersion = m_DxcVersion;
		if (desc.m_Target.m_BinaryFormat == ShaderBinaryFormat::SpirV)
		{
			const ShaderCompileTarget vulkanTarget = MakeVulkanSpirVTarget(desc.m_Stage);
			if (desc.m_Target.m_SpirVTargetEnvironment == ShaderSpirVTargetEnvironment::None)
			{
				desc.m_Target.m_SpirVTargetEnvironment =
					vulkanTarget.m_SpirVTargetEnvironment;
			}
			if (desc.m_Target.m_BindingABIRevision == 0)
			{
				desc.m_Target.m_BindingABIRevision = vulkanTarget.m_BindingABIRevision;
			}
			desc.m_Target.m_CoordinateOptions = vulkanTarget.m_CoordinateOptions;
		}
		else
		{
			desc.m_Target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
			desc.m_Target.m_BindingABIRevision = 0;
			desc.m_Target.m_CoordinateOptions = ShaderCoordinateOptions::None;
		}

		// Source and include paths supplied by shader users are relative to the configured source root.
		if (desc.m_SourcePath.is_relative())
		{
			desc.m_SourcePath = m_SourceRootDir / desc.m_SourcePath;
		}
		desc.m_SourcePath = utils::Canonical(desc.m_SourcePath);
		for (auto& include : desc.m_IncludeDirs)
		{
			if (include.is_relative())
			{
				include = m_SourceRootDir / include;
			}
			include = utils::Canonical(include);
		}
		{
			std::unordered_set<std::wstring> seen;
			std::vector<std::filesystem::path> uniqueIncludeDirs;
			uniqueIncludeDirs.reserve(desc.m_IncludeDirs.size());
			for (auto& dir : desc.m_IncludeDirs)
			{
				auto dirStr = dir.wstring();
				if (seen.insert(dirStr).second)
				{
					uniqueIncludeDirs.emplace_back(std::move(dirStr));
				}
			}
			desc.m_IncludeDirs.swap(uniqueIncludeDirs);
		}

		// defines: sort, exclude duplicate
		std::sort(desc.m_Defines.begin(), desc.m_Defines.end());
		desc.m_Defines.erase(
			std::unique(desc.m_Defines.begin(), desc.m_Defines.end()), desc.m_Defines.end());

		// extra arguments, exclude duplicate
		{
			std::unordered_set<std::wstring> seen;
			std::vector<std::wstring> uniqueExtraArgs;
			uniqueExtraArgs.reserve(desc.m_ExtraArgs.size());
			for (auto& arg : desc.m_ExtraArgs)
			{
				if (seen.insert(arg).second)
				{
					uniqueExtraArgs.push_back(std::move(arg));
				}
			}
			desc.m_ExtraArgs.swap(uniqueExtraArgs);
		}

		return desc;
	}

	ShaderCompileTarget ShaderCompiler::MakeVulkanSpirVTarget(ShaderStage stage) noexcept
	{
		ShaderCompileTarget target{};
		target.m_BinaryFormat = ShaderBinaryFormat::SpirV;
		target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3;
		target.m_BindingABIRevision = GGLabVulkanShaderBindingABI.m_Revision;
		if (IsVertexProducingStage(stage) &&
			GGLabVulkanCoordinatePolicy.m_InvertVertexProducingStageY)
		{
			target.m_CoordinateOptions |= ShaderCoordinateOptions::InvertY;
		}
		if (stage == ShaderStage::Pixel && GGLabVulkanCoordinatePolicy.m_UseDxPositionW)
		{
			target.m_CoordinateOptions |= ShaderCoordinateOptions::UseDxPositionW;
		}
		return target;
	}

	std::vector<std::wstring> ShaderCompiler::BuildCompileArguments(const ShaderDesc& desc) noexcept
	{
		const ShaderCompileTarget& compileTarget = desc.m_Target;
		std::vector<std::wstring> args;
		args.reserve(32 + desc.m_IncludeDirs.size() * 2 + desc.m_Defines.size() * 2 +
			desc.m_ExtraArgs.size());

		args.emplace_back(L"-E");
		args.push_back(desc.m_Entry.empty() ? DefaultEntry(desc.m_Stage) : desc.m_Entry);
		args.emplace_back(L"-T");
		args.push_back(ToTarget(desc.m_Stage, compileTarget.m_Model));
		args.emplace_back(L"-HV");
		args.push_back(compileTarget.m_HlslVersion);

		if (compileTarget.m_BinaryFormat == ShaderBinaryFormat::SpirV)
		{
			args.emplace_back(L"-spirv");
			args.emplace_back(L"-fspv-target-env=vulkan1.3");
			args.emplace_back(L"-fvk-use-dx-layout");

			const auto appendRegisterShift = [&args](std::wstring_view option,
				VulkanShaderRegisterClass registerClass) noexcept
				{
					const VulkanFixedRegisterRange range =
						GetVulkanFixedRegisterRange(registerClass);
					args.emplace_back(option);
					args.push_back(std::to_wstring(range.m_BindingShift));
					args.push_back(std::to_wstring(
						GGLabVulkanShaderBindingABI.m_FixedHlslRegisterSpace));
				};
			appendRegisterShift(L"-fvk-b-shift", VulkanShaderRegisterClass::ConstantBuffer);
			appendRegisterShift(L"-fvk-t-shift", VulkanShaderRegisterClass::ShaderResource);
			appendRegisterShift(L"-fvk-u-shift", VulkanShaderRegisterClass::UnorderedAccess);
			appendRegisterShift(L"-fvk-s-shift", VulkanShaderRegisterClass::Sampler);

			args.emplace_back(L"-fvk-bind-resource-heap");
			args.push_back(std::to_wstring(GGLabVulkanShaderBindingABI.m_ResourceHeapBinding));
			args.push_back(std::to_wstring(GGLabVulkanShaderBindingABI.m_GlobalDescriptorSet));
			args.emplace_back(L"-fvk-bind-sampler-heap");
			args.push_back(std::to_wstring(GGLabVulkanShaderBindingABI.m_SamplerHeapBinding));
			args.push_back(std::to_wstring(GGLabVulkanShaderBindingABI.m_GlobalDescriptorSet));

			if (Test(compileTarget.m_CoordinateOptions, ShaderCoordinateOptions::InvertY))
			{
				args.emplace_back(L"-fvk-invert-y");
			}
			if (Test(compileTarget.m_CoordinateOptions, ShaderCoordinateOptions::UseDxPositionW))
			{
				args.emplace_back(L"-fvk-use-dx-position-w");
			}
		}

		if (Test(compileTarget.m_Flags, ShaderCompileFlags::Debug))
		{
			args.emplace_back(DXC_ARG_DEBUG);
			if (compileTarget.m_BinaryFormat == ShaderBinaryFormat::SpirV)
			{
				args.emplace_back(L"-fspv-debug=vulkan-with-source");
			}
			else
			{
				args.emplace_back(L"-Qembed_debug");
			}
		}
		else if (compileTarget.m_BinaryFormat == ShaderBinaryFormat::Dxil)
		{
			args.emplace_back(L"-Qstrip_debug");
			args.emplace_back(L"-Qstrip_reflect");
		}

		if (Test(compileTarget.m_Flags, ShaderCompileFlags::Optimization))
		{
			args.push_back(L"-" + compileTarget.m_OptimizationLevel);
		}
		else
		{
			args.emplace_back(DXC_ARG_SKIP_OPTIMIZATIONS);
		}

		for (const auto& include : desc.m_IncludeDirs)
		{
			args.emplace_back(L"-I");
			args.push_back(utils::Canonical(include).wstring());
		}
		for (const ShaderDefine& define : desc.m_Defines)
		{
			std::wstring value = define.m_Name;
			if (!define.m_Value.empty())
			{
				value += L"=" + define.m_Value;
			}
			args.emplace_back(L"-D");
			args.push_back(std::move(value));
		}
		args.insert(args.end(), desc.m_ExtraArgs.begin(), desc.m_ExtraArgs.end());
		return args;
	}

	ShaderHash128 ShaderCompiler::ComputeRecipeHash(const ShaderDesc& mergedDesc) noexcept
	{
		const auto keyString = BuildKeyString(mergedDesc);
		const auto* bytes = reinterpret_cast<const uint8_t*>(keyString.data());

		const auto keySize = keyString.size() * sizeof(wchar_t);
		ShaderHash128 hash{};
		hash.m_LowBits = Crc64(keyString);
		hash.m_HighBits = FNV1a64::HashBytes64(bytes, keySize);

		return hash;
	}

	std::filesystem::path ShaderCompiler::MakeCacheBinaryPath(
		const std::wstring& keyHex, ShaderStage stage, ShaderBinaryFormat format) const noexcept
	{
		std::wstring stageExtension;
		switch (stage)
		{
		case ShaderStage::Vertex:
			stageExtension = L".vs";
			break;
		case ShaderStage::Pixel:
			stageExtension = L".ps";
			break;
		case ShaderStage::Hull:
			stageExtension = L".hs";
			break;
		case ShaderStage::Domain:
			stageExtension = L".ds";
			break;
		case ShaderStage::Geometry:
			stageExtension = L".gs";
			break;
		case ShaderStage::Mesh:
			stageExtension = L".ms";
			break;
		case ShaderStage::Compute:
			stageExtension = L".cs";
			break;
		default:
			break;
		}
		const std::wstring formatDirectory =
			format == ShaderBinaryFormat::SpirV ? L"spirv" : L"dxil";
		const std::wstring binaryExtension =
			format == ShaderBinaryFormat::SpirV ? L".spv" : L".dxil";
		const std::wstring extension = stageExtension + binaryExtension;

		auto path = m_CacheRootDir / formatDirectory / keyHex.substr(0, 2) /
			keyHex.substr(2, 2) / (keyHex + extension);
		utils::CreateParentDirectoryIfNotExist(path);
		return path;
	}

	ShaderBinary ShaderCompiler::CompileShader(
		const ShaderDesc& desc, std::vector<std::filesystem::path>& outDeps) const noexcept
	{
		ComPtr<IDxcBlobEncoding> src;
		GGLAB_HR(
			m_Impl->m_Utils->LoadFile(utils::Canonical(desc.m_SourcePath).c_str(), nullptr, &src));

		DxcBuffer buffer{};
		buffer.Ptr = src->GetBufferPointer();
		buffer.Size = src->GetBufferSize();
		buffer.Encoding = DXC_CP_UTF8;

		ComPtr<ShaderIncludeHandler> includeHandler;
		GGLAB_HR(MakeAndInitialize<ShaderIncludeHandler>(
			&includeHandler, m_Impl->m_Utils, desc.m_IncludeDirs));

		const std::vector<std::wstring> ownedArgs = BuildCompileArguments(desc);
		std::vector<const wchar_t*> args;
		args.reserve(ownedArgs.size());
		for (const std::wstring& arg : ownedArgs)
		{
			args.push_back(arg.c_str());
		}

		// Compile
		ComPtr<IDxcResult> result;
		GGLAB_HR(m_Impl->m_Compiler->Compile(&buffer, args.data(), (UINT32)args.size(),
			includeHandler.Get(), IID_PPV_ARGS(&result)));

		HRESULT status = S_OK;
		result->GetStatus(&status);
		if (FAILED(status))
		{
			ComPtr<IDxcBlobUtf8> log;
			result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&log), nullptr);
			if (log && log->GetStringLength())
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"DXC error:\n{}", static_cast<const char*>(log->GetBufferPointer()));
			}
			GGLAB_HR(status);
		}

		// Record includes
		outDeps = includeHandler->Includes();

		ComPtr<IDxcBlob> dxilBlob;
		GGLAB_HR(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxilBlob), nullptr));

		return CopyShaderBinary(dxilBlob.Get());
	}

	void ShaderCompiler::WriteMeta(const std::filesystem::path& meta, const ShaderDesc& desc,
		const std::vector<std::filesystem::path>& deps, ShaderHash128 recipeHash) const noexcept
	{
		const auto created = utils::CreateParentDirectoryIfNotExist(meta);

		GGLAB_ASSERT_MSG(created, "Create shader meta failed.");

		std::ofstream out(meta, std::ios::binary);
		if (!out)
		{
			return;
		}

		const auto src = utils::Canonical(desc.m_SourcePath).string();
		const auto entry = utils::ToString(desc.m_Entry);
		const auto target = utils::ToString(ToTarget(desc.m_Stage, desc.m_Target.m_Model));

		out << "schema=2\n";
		out << "recipe=" << utils::ToString(ToHex(recipeHash)) << "\n";
		out << "binary_format=" << ShaderBinaryFormatText(desc.m_Target.m_BinaryFormat) << "\n";
		out << "target_environment=" <<
			SpirVTargetEnvironmentText(desc.m_Target.m_SpirVTargetEnvironment) << "\n";
		out << "binding_abi_revision=" << desc.m_Target.m_BindingABIRevision << "\n";
		out << "coordinate_options=" <<
			static_cast<uint32_t>(desc.m_Target.m_CoordinateOptions) << "\n";
		out << "dxc_version=" << utils::ToString(desc.m_Target.m_DxcVersion) << "\n";
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

		out << "dep=" << utils::Canonical(desc.m_SourcePath).string()
			<< "|mtime=" << utils::LastWriteTimeTicks(desc.m_SourcePath) << "\n";
		for (const auto& d : deps)
		{
			out << "dep=" << d.string() << "|mtime=" << utils::LastWriteTimeTicks(d) << "\n";
		}
	}

	bool ShaderCompiler::IsMetaUpToDate(const std::filesystem::path& meta,
		const ShaderDesc& desc, ShaderHash128 recipeHash) const noexcept
	{
		std::error_code errorCode;
		if (!std::filesystem::exists(meta, errorCode))
		{
			return false;
		}

		std::ifstream in(meta, std::ios::binary);
		if (!in)
		{
			return false;
		}

		std::unordered_map<std::string, std::string> values;
		size_t dependencyCount = 0;
		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("dep=", 0) != 0)
			{
				const size_t separator = line.find('=');
				if (separator != std::string::npos)
				{
					values[line.substr(0, separator)] = line.substr(separator + 1);
				}
				continue;
			}
			++dependencyCount;

			const auto bar = line.find("|");
			const auto eq = line.find("mtime=", bar == std::string::npos ? 0 : bar);

			if (bar == std::string::npos || eq == std::string::npos)
			{
				return false;
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

		const auto equals = [&values](std::string_view key, std::string_view expected) noexcept
			{
				const auto iterator = values.find(std::string(key));
				return iterator != values.end() && iterator->second == expected;
			};
		return dependencyCount > 0 && equals("schema", "2") &&
			equals("recipe", utils::ToString(ToHex(recipeHash))) &&
			equals("binary_format", ShaderBinaryFormatText(desc.m_Target.m_BinaryFormat)) &&
			equals("target_environment",
				SpirVTargetEnvironmentText(desc.m_Target.m_SpirVTargetEnvironment)) &&
			equals("binding_abi_revision",
				std::to_string(desc.m_Target.m_BindingABIRevision)) &&
			equals("coordinate_options",
				std::to_string(static_cast<uint32_t>(desc.m_Target.m_CoordinateOptions))) &&
			equals("dxc_version", utils::ToString(desc.m_Target.m_DxcVersion));
	}

	std::wstring ShaderCompiler::DefaultEntry(const ShaderStage& stage) noexcept
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

	std::wstring ShaderCompiler::ToHex(ShaderHash128 hash) noexcept
	{
		return std::format(L"{:016x}{:016x}", hash.m_HighBits, hash.m_LowBits);
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
		std::wstring str;
		str.reserve(1024);
		const auto append = [&str](std::wstring_view name, std::wstring_view value)
			{
				str.append(name);
				str.push_back(L':');
				str.append(std::to_wstring(value.size()));
				str.push_back(L':');
				str.append(value);
				str.push_back(L';');
			};
		append(L"src", src);
		append(L"binary_format",
			std::to_wstring(static_cast<uint32_t>(desc.m_Target.m_BinaryFormat)));
		append(L"target_environment",
			std::to_wstring(static_cast<uint32_t>(desc.m_Target.m_SpirVTargetEnvironment)));
		append(L"binding_abi_revision",
			std::to_wstring(desc.m_Target.m_BindingABIRevision));
		append(L"coordinate_options",
			std::to_wstring(static_cast<uint32_t>(desc.m_Target.m_CoordinateOptions)));
		append(L"dxc_version", desc.m_Target.m_DxcVersion);
		const std::vector<std::wstring> compileArguments = BuildCompileArguments(desc);
		for (const std::wstring& argument : compileArguments)
		{
			append(L"arg", argument);
		}

		return str;
	}

	std::wstring ShaderCompiler::QueryDxcVersion() const noexcept
	{
		ComPtr<IDxcVersionInfo> versionInfo;
		if (FAILED(m_Impl->m_Compiler.As(&versionInfo)))
		{
			return L"unknown";
		}

		UINT32 major = 0;
		UINT32 minor = 0;
		if (FAILED(versionInfo->GetVersion(&major, &minor)))
		{
			return L"unknown";
		}

		ComPtr<IDxcVersionInfo2> versionInfo2;
		if (FAILED(m_Impl->m_Compiler.As(&versionInfo2)))
		{
			return std::format(L"{}.{}", major, minor);
		}

		UINT32 commitCount = 0;
		char* commitHash = nullptr;
		if (FAILED(versionInfo2->GetCommitInfo(&commitCount, &commitHash)))
		{
			return std::format(L"{}.{}", major, minor);
		}
		const std::wstring commit =
			commitHash ? utils::ToWideString(commitHash) : std::wstring(L"unknown");
		CoTaskMemFree(commitHash);
		return std::format(L"{}.{}+{}.{}", major, minor, commitCount, commit);
	}

	bool ShaderCompiler::GetContainerHash(
		const void* data, size_t size, ShaderHash128& outHash) noexcept
	{
		constexpr size_t MinDxilSize = 20;
		if (data == nullptr || size < MinDxilSize)
		{
			return false;
		}

		// magic number for DirectX Container
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

	bool ShaderCompiler::IsBinaryFormat(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept
	{
		if (!binary.IsValid())
		{
			return false;
		}

		const auto* data = static_cast<const uint8_t*>(binary.Data());
		const size_t size = binary.SizeInBytes();
		switch (format)
		{
		case ShaderBinaryFormat::Dxil:
			return size >= 20 && std::memcmp(data, "DXBC", 4) == 0;
		case ShaderBinaryFormat::SpirV:
		{
			if (size < 5 * sizeof(uint32_t) || size % sizeof(uint32_t) != 0)
			{
				return false;
			}
			uint32_t magic = 0;
			std::memcpy(&magic, data, sizeof(magic));
			return magic == 0x07230203u;
		}
		case ShaderBinaryFormat::Unknown:
			break;
		}
		return false;
	}

	ShaderHash128 ShaderCompiler::ComputeHashFromBinary(
		const ShaderBinary& binary, ShaderBinaryFormat format) noexcept
	{
		ShaderHash128 hash{};
		if (!binary.IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("ShaderCompiler::ComputeHashFromBinary: binary is empty.");
			return hash;
		}

		const auto* ptr = static_cast<const uint8_t*>(binary.Data());
		const auto size = binary.SizeInBytes();

		if (format == ShaderBinaryFormat::Dxil && GetContainerHash(ptr, size, hash))
		{
			return hash;
		}

		// FNV-1a 64-bit hash
		if (format == ShaderBinaryFormat::Dxil)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"ShaderCompiler::ComputeHashFromBinary: Failed to get DXIL container hash, fallback to FNV-1a hash.");
		}
		hash.m_LowBits = FNV1a64::HashBytes64(ptr, size);
		hash.m_HighBits = FNV1a64::HashBytes64(ptr, size, 0x9ae16a3b2f90404full);
		return hash;
	}
}
