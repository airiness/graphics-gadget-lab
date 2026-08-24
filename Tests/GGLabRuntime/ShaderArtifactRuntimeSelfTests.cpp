#include "ShaderArtifactRuntimeSelfTests.h"

#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include "GGLabFoundation/Hash/Sha256.h"
#include "Graphics/Shader/ShaderManager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <utility>

namespace gglab
{
	namespace
	{
		class FixtureArtifactReader final : public ShaderArtifactReaderBase
		{
		public:
			[[nodiscard]] ShaderArtifactReadResult ReadArtifact(
				const ShaderArtifactRef& artifactRef) noexcept override
			{
				++m_ReadCount;
				m_LastRef = artifactRef;
				return m_Result;
			}

			ShaderArtifactReadResult m_Result{};
			ShaderArtifactRef m_LastRef{};
			uint32_t m_ReadCount = 0;
		};

		[[nodiscard]] ShaderBinary MakeDxilBinary() noexcept
		{
			ShaderBinary binary(20);
			std::memset(binary.Data(), 0, binary.SizeInBytes());
			std::memcpy(binary.Data(), "DXBC", 4);
			return binary;
		}

		[[nodiscard]] ShaderBinary MakeSpirVBinary() noexcept
		{
			constexpr std::array<uint32_t, 5> Header{
				0x07230203u,
				0x00010600u,
				0u,
				1u,
				0u,
			};
			ShaderBinary binary(sizeof(Header));
			std::memcpy(binary.Data(), Header.data(), sizeof(Header));
			return binary;
		}

		void RefreshArtifactIdentity(ShaderRuntimeArtifact& artifact) noexcept
		{
			const auto bytes = std::span(
				static_cast<const std::byte*>(artifact.m_Binary.Data()),
				artifact.m_Binary.SizeInBytes());
			artifact.m_Manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(bytes);
			artifact.m_Manifest.m_ArtifactId = ComputeShaderArtifactId(artifact.m_Manifest);
		}

		[[nodiscard]] ShaderRuntimeArtifact MakeDxilArtifact() noexcept
		{
			ShaderRuntimeArtifact artifact{};
			artifact.m_Manifest.m_TargetProfile = ShaderTargetProfile::GGLabDX12;
			artifact.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			artifact.m_Manifest.m_SpirVTargetEnvironment =
				ShaderSpirVTargetEnvironment::None;
			artifact.m_Manifest.m_BindingABIRevision = 0;
			artifact.m_Manifest.m_CoordinateOptions = ShaderCoordinateOptions::None;
			artifact.m_Manifest.m_Stage = ShaderStage::Vertex;
			artifact.m_Manifest.m_EntryPoint = "VSMain";
			artifact.m_Binary = MakeDxilBinary();
			RefreshArtifactIdentity(artifact);
			return artifact;
		}

		[[nodiscard]] ShaderRuntimeArtifact MakeVulkanArtifact() noexcept
		{
			ShaderRuntimeArtifact artifact{};
			artifact.m_Manifest.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13;
			artifact.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::SpirV;
			artifact.m_Manifest.m_SpirVTargetEnvironment =
				ShaderSpirVTargetEnvironment::Vulkan1_3;
			artifact.m_Manifest.m_BindingABIRevision = 1;
			artifact.m_Manifest.m_CoordinateOptions = ShaderCoordinateOptions::InvertY;
			artifact.m_Manifest.m_Stage = ShaderStage::Vertex;
			artifact.m_Manifest.m_EntryPoint = "VSMain";
			artifact.m_Binary = MakeSpirVBinary();
			RefreshArtifactIdentity(artifact);
			return artifact;
		}

		[[nodiscard]] ShaderArtifactCompatibilityRequest MakeDxilRequest() noexcept
		{
			return {};
		}

		[[nodiscard]] ShaderArtifactCompatibilityRequest MakeVulkanRequest() noexcept
		{
			return {
				.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
				.m_BinaryFormat = ShaderBinaryFormat::SpirV,
				.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3,
				.m_BindingABIRevision = 1,
				.m_CoordinateOptions = ShaderCoordinateOptions::InvertY,
				.m_Stage = ShaderStage::Vertex,
			};
		}

		[[nodiscard]] ShaderArtifactRef MakeRef(
			const ShaderRuntimeArtifact& artifact) noexcept
		{
			return { .m_ArtifactId = artifact.m_Manifest.m_ArtifactId };
		}

		[[nodiscard]] bool WriteBytes(
			const std::filesystem::path& path, std::span<const std::byte> bytes) noexcept
		{
			try
			{
				std::filesystem::create_directories(path.parent_path());
				std::ofstream stream(path, std::ios::binary | std::ios::trunc);
				stream.write(reinterpret_cast<const char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
				return stream.good();
			}
			catch (...)
			{
				return false;
			}
		}

		void RunIdentityTests(SelfTestContext& context) noexcept
		{
			ShaderRuntimeArtifact baseline = MakeDxilArtifact();
			ShaderRuntimeArtifact changedBinary = baseline;
			static_cast<std::byte*>(changedBinary.m_Binary.Data())[19] = std::byte{ 1 };
			RefreshArtifactIdentity(changedBinary);
			context.Check(
				baseline.m_Manifest.m_ArtifactId != changedBinary.m_Manifest.m_ArtifactId,
				"Runtime ArtifactId changes when the exact binary bytes change");

			ShaderRuntimeArtifact changedStage = baseline;
			changedStage.m_Manifest.m_Stage = ShaderStage::Pixel;
			RefreshArtifactIdentity(changedStage);
			context.Check(
				baseline.m_Manifest.m_ArtifactId != changedStage.m_Manifest.m_ArtifactId,
				"Runtime ArtifactId changes when compatibility semantics change");

			ShaderRuntimeArtifact changedEntryPoint = baseline;
			changedEntryPoint.m_Manifest.m_EntryPoint = "VSAlternate";
			RefreshArtifactIdentity(changedEntryPoint);
			context.Check(
				baseline.m_Manifest.m_ArtifactId !=
					changedEntryPoint.m_Manifest.m_ArtifactId,
				"Runtime ArtifactId covers executable entry-point metadata");
		}

		void RunProgramRegistryTests(SelfTestContext& context) noexcept
		{
			const ShaderProgramRef programRef{
				.m_ProgramId = "gglab.shader.test",
				.m_VariantId = "vertex.default",
				.m_Stage = ShaderStage::Vertex,
			};
			const ShaderProgramRef secondProgramRef{
				.m_ProgramId = "gglab.shader.test",
				.m_VariantId = "pixel.default",
				.m_Stage = ShaderStage::Pixel,
			};
			const ShaderProgramRef sameNamesDifferentStage{
				.m_ProgramId = "gglab.shader.test",
				.m_VariantId = "vertex.default",
				.m_Stage = ShaderStage::Pixel,
			};
			const ShaderArtifactRef firstArtifact = MakeRef(MakeDxilArtifact());
			ShaderRuntimeArtifact changedArtifact = MakeDxilArtifact();
			changedArtifact.m_Manifest.m_Stage = ShaderStage::Pixel;
			RefreshArtifactIdentity(changedArtifact);
			const ShaderArtifactRef secondArtifact = MakeRef(changedArtifact);

			ShaderProgramRegistry registry;
			context.Check(
				registry.Bind(programRef, firstArtifact) == ShaderProgramBindStatus::Bound &&
					registry.Resolve(programRef) == firstArtifact &&
					registry.GetMappingCount() == 1,
				"Program registry resolves a stable ProgramRef to its bound ArtifactRef");
			context.Check(
				registry.Bind(programRef, firstArtifact) ==
					ShaderProgramBindStatus::AlreadyBound &&
					registry.GetMappingCount() == 1,
				"Program registry treats an identical binding as idempotent");
			context.Check(
				registry.Bind(programRef, secondArtifact) == ShaderProgramBindStatus::Rebound &&
					registry.Resolve(programRef) == secondArtifact,
				"Program registry explicitly replaces a program's active immutable artifact");
			context.Check(
				registry.Bind(sameNamesDifferentStage, secondArtifact) ==
					ShaderProgramBindStatus::Bound &&
					registry.Resolve(sameNamesDifferentStage) == secondArtifact &&
					registry.GetMappingCount() == 2,
				"ProgramRef stage is an explicit part of stable Runtime identity");
			context.Check(
				registry.Bind({}, firstArtifact) == ShaderProgramBindStatus::InvalidProgram &&
					registry.Bind(secondProgramRef, {}) ==
						ShaderProgramBindStatus::InvalidArtifact &&
					!registry.Resolve(secondProgramRef).has_value(),
				"Program registry rejects invalid identities without publishing a mapping");

			ShaderProgramDemandSet demands;
			const std::array declaredPrograms{ programRef, secondProgramRef, programRef };
			context.Check(demands.AddRange(declaredPrograms) &&
				demands.GetPrograms().size() == 2 &&
				demands.GetPrograms()[0] == programRef &&
				demands.GetPrograms()[1] == secondProgramRef,
				"Program demand aggregation preserves declaration order and removes duplicates");
			context.Check(
				demands.Add({}) == ShaderProgramDemandAddStatus::InvalidProgram,
				"Program demand aggregation rejects invalid stable identities");
		}

		void RunProgramRegistryArtifactTests(SelfTestContext& context) noexcept
		{
			ShaderArtifactRef goldenArtifactRef{};
			for (size_t index = 0;
				index < goldenArtifactRef.m_ArtifactId.m_DurableDigest.m_Value.size();
				++index)
			{
				goldenArtifactRef.m_ArtifactId.m_DurableDigest.m_Value[index] =
					static_cast<std::byte>(index + 1);
			}
			const std::array goldenEntries{
				ShaderProgramRegistryEntry{
					.m_ProgramRef = {
						.m_ProgramId = "gglab.shader.golden",
						.m_VariantId = "vertex.default",
						.m_Stage = ShaderStage::Vertex,
					},
					.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
					.m_ArtifactRef = goldenArtifactRef,
				},
			};
			const ShaderProgramRegistryArtifactBuildResult goldenBuild =
				BuildShaderProgramRegistryArtifact(goldenEntries);
			context.Check(
				goldenBuild.IsSuccess() &&
					Sha256DigestToHex(goldenBuild.m_Artifact.m_RegistryId.m_DurableDigest) ==
						"c7ee18fba789733252c38bd74ff03556fbb9ac3e36e0c96cc2bd089455455905",
				"Program Registry identity matches its stable golden vector");

			const ShaderProgramRef vertexProgram{
				.m_ProgramId = "gglab.shader.registry-test",
				.m_VariantId = "vertex.default",
				.m_Stage = ShaderStage::Vertex,
			};
			const ShaderProgramRef pixelProgram{
				.m_ProgramId = "gglab.shader.registry-test",
				.m_VariantId = "pixel.default",
				.m_Stage = ShaderStage::Pixel,
			};
			const ShaderArtifactRef dxilRef = MakeRef(MakeDxilArtifact());
			const ShaderArtifactRef spirVRef = MakeRef(MakeVulkanArtifact());
			const std::array entries{
				ShaderProgramRegistryEntry{
					.m_ProgramRef = pixelProgram,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = dxilRef,
				},
				ShaderProgramRegistryEntry{
					.m_ProgramRef = vertexProgram,
					.m_TargetProfile = ShaderTargetProfile::GGLabVulkan13,
					.m_ArtifactRef = spirVRef,
				},
				ShaderProgramRegistryEntry{
					.m_ProgramRef = vertexProgram,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = dxilRef,
				},
			};

			const ShaderProgramRegistryArtifactBuildResult build =
				BuildShaderProgramRegistryArtifact(entries);
			context.Check(
				build.IsSuccess() && build.m_Artifact.m_Entries.size() == entries.size() &&
					ValidateShaderProgramRegistryArtifact(build.m_Artifact) ==
						ShaderProgramRegistryArtifactValidationStatus::Valid,
				"Program Registry Artifact canonicalizes a complete valid mapping snapshot");
			context.Check(
				ResolveShaderProgramRegistryArtifact(
					build.m_Artifact, vertexProgram, ShaderTargetProfile::GGLabDX12) ==
						dxilRef &&
					ResolveShaderProgramRegistryArtifact(
						build.m_Artifact,
						vertexProgram,
						ShaderTargetProfile::GGLabVulkan13) == spirVRef &&
					!ResolveShaderProgramRegistryArtifact(
						build.m_Artifact,
						pixelProgram,
						ShaderTargetProfile::GGLabVulkan13).has_value(),
				"Program Registry Artifact resolves one logical program per target profile");

			auto changedEntries = entries;
			changedEntries[2].m_ArtifactRef.m_ArtifactId.m_DurableDigest.m_Value[0] ^=
				std::byte{ 1 };
			const ShaderProgramRegistryArtifactBuildResult changedBuild =
				BuildShaderProgramRegistryArtifact(changedEntries);
			context.Check(
				changedBuild.IsSuccess() &&
					changedBuild.m_Artifact.m_RegistryId != build.m_Artifact.m_RegistryId,
				"RegistryId changes when one effective Program-to-Artifact binding changes");

			const std::array duplicateEntries{ entries[0], entries[0] };
			const std::array<ShaderProgramRegistryEntry, 0> emptyEntries{};
			context.Check(
				BuildShaderProgramRegistryArtifact(duplicateEntries).m_Status ==
					ShaderProgramRegistryArtifactBuildStatus::DuplicateBinding &&
					BuildShaderProgramRegistryArtifact(emptyEntries).m_Status ==
						ShaderProgramRegistryArtifactBuildStatus::Empty,
				"Program Registry Artifact rejects duplicate and empty snapshots explicitly");

			ShaderProgramRegistryArtifact nonCanonical = build.m_Artifact;
			std::swap(nonCanonical.m_Entries[0], nonCanonical.m_Entries[1]);
			context.Check(
				ValidateShaderProgramRegistryArtifact(nonCanonical) ==
					ShaderProgramRegistryArtifactValidationStatus::NonCanonicalOrder,
				"Program Registry Artifact validation rejects non-canonical entry order");
		}

		void RunCompatibilityTests(SelfTestContext& context) noexcept
		{
			const ShaderRuntimeArtifact dxil = MakeDxilArtifact();
			const ShaderRuntimeArtifact vulkan = MakeVulkanArtifact();
			const ShaderArtifactCompatibilityRequest dxilRequest = MakeDxilRequest();
			const ShaderArtifactCompatibilityRequest vulkanRequest = MakeVulkanRequest();

			context.Check(
				ValidateShaderArtifactCompatibility(dxil.m_Manifest, dxilRequest).IsCompatible(),
				"DXIL runtime artifact matches the DX12 compatibility contract");
			context.Check(
				ValidateShaderArtifactCompatibility(
					vulkan.m_Manifest, vulkanRequest).IsCompatible(),
				"SPIR-V runtime artifact matches the Vulkan 1.3 compatibility contract");

			ShaderRuntimeArtifactManifest manifest = vulkan.m_Manifest;
			manifest.m_SchemaVersion = ShaderRuntimeArtifactManifestSchemaVersion + 1;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::UnsupportedManifestSchema,
				"Compatibility rejects unsupported Runtime manifest schemas explicitly");

			manifest = vulkan.m_Manifest;
			manifest.m_BindingABIRevision = 2;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::BindingABIRevisionMismatch,
				"Compatibility treats Binding ABI revision as a hard boundary");

			manifest = vulkan.m_Manifest;
			manifest.m_CoordinateOptions = ShaderCoordinateOptions::UseDxPositionW;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::CoordinateOptionsMismatch,
				"Compatibility rejects coordinate contract mismatches");

			manifest = vulkan.m_Manifest;
			manifest.m_Stage = ShaderStage::Pixel;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::ShaderStageMismatch,
				"Compatibility rejects shader stage mismatches");

			manifest = vulkan.m_Manifest;
			manifest.m_EntryPoint.clear();
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::InvalidEntryPoint,
				"Compatibility rejects incomplete executable shader metadata");

			manifest = vulkan.m_Manifest;
			manifest.m_TargetProfile = ShaderTargetProfile::GGLabDX12;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::TargetProfileMismatch,
				"Compatibility reports target profile mismatches explicitly");

			manifest = vulkan.m_Manifest;
			manifest.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::BinaryFormatMismatch,
				"Compatibility reports binary format mismatches explicitly");

			manifest = vulkan.m_Manifest;
			manifest.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::TargetEnvironmentMismatch,
				"Compatibility reports target environment mismatches explicitly");

			manifest = vulkan.m_Manifest;
			manifest.m_Stage = static_cast<ShaderStage>(0xFFFFFFFFu);
			context.Check(
				ValidateShaderArtifactCompatibility(manifest, vulkanRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::InvalidManifestTarget,
				"Compatibility rejects unknown manifest target vocabulary");

			ShaderArtifactCompatibilityRequest invalidRequest = vulkanRequest;
			invalidRequest.m_BindingABIRevision = 0;
			context.Check(
				ValidateShaderArtifactCompatibility(
					vulkan.m_Manifest, invalidRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::InvalidRequest,
				"Compatibility rejects internally inconsistent Runtime requests");

			context.Check(
				ValidateShaderArtifactCompatibility(
					vulkan.m_Manifest, dxilRequest).m_Status ==
					ShaderArtifactCompatibilityStatus::TargetProfileMismatch,
				"Compatibility rejects cross-backend artifacts before Runtime use");
		}

		void RunStoreTests(SelfTestContext& context) noexcept
		{
			const ShaderRuntimeArtifact validArtifact = MakeDxilArtifact();
			const ShaderArtifactRef validRef = MakeRef(validArtifact);
			const ShaderArtifactCompatibilityRequest request = MakeDxilRequest();
			FixtureArtifactReader reader;
			ShaderArtifactStore store(reader);

			reader.m_Result = {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = validArtifact,
			};
			ShaderArtifactLoadResult result = store.LoadArtifact(validRef, request);
			context.Check(
				result.IsSuccess() && result.m_Compatibility.IsCompatible() &&
					result.m_Artifact.m_Manifest.m_ArtifactId == validRef.m_ArtifactId,
				"Store returns only a compatible content-addressed Runtime artifact");
			context.Check(
				reader.m_ReadCount == 1 && reader.m_LastRef == validRef,
				"Store resolves the requested stable ArtifactRef exactly once");

			const ShaderRuntimeArtifact vulkanArtifact = MakeVulkanArtifact();
			const ShaderArtifactRef vulkanRef = MakeRef(vulkanArtifact);
			reader.m_Result = {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = vulkanArtifact,
			};
			context.Check(
				store.LoadArtifact(vulkanRef, MakeVulkanRequest()).IsSuccess(),
				"Store validates and returns a compatible SPIR-V Runtime artifact");

			const uint32_t readsBeforeInvalidRef = reader.m_ReadCount;
			result = store.LoadArtifact({}, request);
			context.Check(
				result.m_Status == ShaderArtifactLoadStatus::InvalidReference &&
					reader.m_ReadCount == readsBeforeInvalidRef,
				"Store rejects invalid ArtifactRefs without consulting storage");

			ShaderArtifactCompatibilityRequest invalidRequest = request;
			invalidRequest.m_BindingABIRevision = 1;
			const uint32_t readsBeforeInvalidRequest = reader.m_ReadCount;
			result = store.LoadArtifact(validRef, invalidRequest);
			context.Check(
				result.m_Status ==
					ShaderArtifactLoadStatus::InvalidCompatibilityRequest &&
					result.m_Compatibility.m_Status ==
						ShaderArtifactCompatibilityStatus::InvalidRequest &&
					reader.m_ReadCount == readsBeforeInvalidRequest,
				"Store rejects invalid compatibility requests without consulting storage");

			reader.m_Result = { .m_Status = ShaderArtifactReadStatus::NotFound };
			context.Check(
				store.LoadArtifact(validRef, request).m_Status ==
					ShaderArtifactLoadStatus::NotFound,
				"Store reports a missing runtime artifact explicitly");

			reader.m_Result = { .m_Status = ShaderArtifactReadStatus::IOFailure };
			context.Check(
				store.LoadArtifact(validRef, request).m_Status ==
					ShaderArtifactLoadStatus::ReadFailure,
				"Store reports runtime artifact IO failure explicitly");

			reader.m_Result = { .m_Status = ShaderArtifactReadStatus::MalformedArtifact };
			context.Check(
				store.LoadArtifact(validRef, request).m_Status ==
					ShaderArtifactLoadStatus::MalformedArtifact,
				"Store reports malformed portable manifests explicitly");

			ShaderRuntimeArtifact unsupportedSchema = validArtifact;
			unsupportedSchema.m_Manifest.m_SchemaVersion++;
			reader.m_Result = {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = unsupportedSchema,
			};
			result = store.LoadArtifact(validRef, request);
			context.Check(
				result.m_Status == ShaderArtifactLoadStatus::IncompatibleArtifact &&
					result.m_Compatibility.m_Status ==
						ShaderArtifactCompatibilityStatus::UnsupportedManifestSchema,
				"Store exposes unsupported schemas as structured incompatibility");

			ShaderRuntimeArtifact mismatchedIdentity = validArtifact;
			mismatchedIdentity.m_Manifest.m_ArtifactId.m_DurableDigest.m_Value[0] ^=
				std::byte{ 1 };
			reader.m_Result = {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = mismatchedIdentity,
			};
			context.Check(
				store.LoadArtifact(validRef, request).m_Status ==
					ShaderArtifactLoadStatus::ArtifactIdentityMismatch,
				"Store rejects manifests that do not match the requested content address");

			ShaderRuntimeArtifact corruptBytes = validArtifact;
			static_cast<std::byte*>(corruptBytes.m_Binary.Data())[19] ^= std::byte{ 1 };
			reader.m_Result = {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = corruptBytes,
			};
			context.Check(
				store.LoadArtifact(validRef, request).m_Status ==
					ShaderArtifactLoadStatus::BinaryDigestMismatch,
				"Store hashes the exact returned bytes and rejects binary corruption");

			ShaderRuntimeArtifact invalidDxil = validArtifact;
			std::memcpy(invalidDxil.m_Binary.Data(), "NOPE", 4);
			RefreshArtifactIdentity(invalidDxil);
			const ShaderArtifactRef invalidDxilRef = MakeRef(invalidDxil);
			reader.m_Result = {
				.m_Status = ShaderArtifactReadStatus::Success,
				.m_Artifact = invalidDxil,
			};
			context.Check(
				store.LoadArtifact(invalidDxilRef, request).m_Status ==
					ShaderArtifactLoadStatus::InvalidBinary,
				"Store rejects digest-valid bytes that are not the declared binary format");
		}

		void RunArtifactOnlyShaderManagerTests(SelfTestContext& context) noexcept
		{
			const std::filesystem::path root = std::filesystem::temp_directory_path() /
				"gglab-artifact-only-shader-manager-test";
			std::error_code errorCode;
			std::filesystem::remove_all(root, errorCode);

			const ShaderProgramRef programRef{
				.m_ProgramId = "gglab.shader.artifact-only-test",
				.m_VariantId = "vertex.default",
				.m_Stage = ShaderStage::Vertex,
			};
			const ShaderRuntimeArtifact artifact = MakeDxilArtifact();
			const ShaderArtifactRef artifactRef = MakeRef(artifact);
			const std::array entries{
				ShaderProgramRegistryEntry{
					.m_ProgramRef = programRef,
					.m_TargetProfile = ShaderTargetProfile::GGLabDX12,
					.m_ArtifactRef = artifactRef,
				},
			};
			const ShaderProgramRegistryArtifactBuildResult registryBuild =
				BuildShaderProgramRegistryArtifact(entries);
			const ShaderProgramRegistryArtifactRef registryRef{
				.m_RegistryId = registryBuild.m_Artifact.m_RegistryId,
			};

			const ShaderLooseArtifactPaths artifactPaths =
				ShaderLooseArtifactLocator(root).GetPaths(artifactRef);
			const SerializedShaderRuntimeArtifactManifest serializedManifest =
				SerializeShaderRuntimeArtifactManifest(artifact.m_Manifest);
			const auto binaryBytes = std::span(
				static_cast<const std::byte*>(artifact.m_Binary.Data()),
				artifact.m_Binary.SizeInBytes());
			const ShaderLooseProgramRegistryArtifactPath registryPath =
				ShaderLooseProgramRegistryArtifactLocator(root).GetPath(registryRef);
			const SerializedShaderProgramRegistryArtifact serializedRegistry =
				SerializeShaderProgramRegistryArtifact(registryBuild.m_Artifact);
			const bool fixturesWritten = registryBuild.IsSuccess() &&
				WriteBytes(artifactPaths.m_BinaryPath, binaryBytes) &&
				WriteBytes(artifactPaths.m_ManifestPath, serializedManifest) &&
				WriteBytes(registryPath.m_Path, serializedRegistry);

			ShaderManager manager({
				.m_ActiveBackend = RHIBackendType::DX12,
				.m_ArtifactRoot = root,
				.m_ActiveRegistry = registryRef,
				});
			const ShaderID shaderId = manager.LoadProgram(programRef);
			context.Check(fixturesWritten && manager.IsReady() && shaderId.IsValid() &&
				manager.GetBytecode(shaderId).m_Format == ShaderBinaryFormat::Dxil &&
				manager.GetBytecode(shaderId).m_EntryPoint == "VSMain" &&
				manager.ResolveArtifact(programRef) == artifactRef,
				"ShaderManager loads a program using only an injected registry and artifact root");

			ShaderManager wrongBackendManager({
				.m_ActiveBackend = RHIBackendType::Vulkan,
				.m_ArtifactRoot = root,
				.m_ActiveRegistry = registryRef,
				});
			context.Check(wrongBackendManager.IsReady() &&
				!wrongBackendManager.LoadProgram(programRef).IsValid() &&
				!wrongBackendManager.ResolveArtifact(programRef).has_value(),
				"ShaderManager never falls back across backend-specific registry bindings");

			ShaderProgramRegistryArtifactRef missingRegistryRef = registryRef;
			missingRegistryRef.m_RegistryId.m_DurableDigest.m_Value[0] ^= std::byte{ 1 };
			ShaderManager missingRegistryManager({
				.m_ActiveBackend = RHIBackendType::DX12,
				.m_ArtifactRoot = root,
				.m_ActiveRegistry = missingRegistryRef,
				});
			context.Check(!missingRegistryManager.IsReady() &&
				missingRegistryManager.GetInitializeStatus() ==
					ShaderManagerInitializeStatus::RegistryNotFound,
				"ShaderManager reports a missing injected registry without consulting source or compiler state");

			std::filesystem::remove_all(root, errorCode);
		}
	}

	void RunShaderArtifactRuntimeSelfTests(SelfTestContext& context) noexcept
	{
		RunIdentityTests(context);
		RunProgramRegistryTests(context);
		RunProgramRegistryArtifactTests(context);
		RunCompatibilityTests(context);
		RunStoreTests(context);
		RunArtifactOnlyShaderManagerTests(context);
	}
}
