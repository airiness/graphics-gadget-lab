#include "ShaderArtifactRuntimeSelfTests.h"

#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include "GGLabFoundation/Hash/Sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

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
		}

		void RunProgramRegistryTests(SelfTestContext& context) noexcept
		{
			const ShaderProgramRef programRef{
				.m_ProgramId = "gglab.shader.test",
				.m_VariantId = "vertex.default",
			};
			const ShaderProgramRef secondProgramRef{
				.m_ProgramId = "gglab.shader.test",
				.m_VariantId = "pixel.default",
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
	}

	void RunShaderArtifactRuntimeSelfTests(SelfTestContext& context) noexcept
	{
		RunIdentityTests(context);
		RunProgramRegistryTests(context);
		RunCompatibilityTests(context);
		RunStoreTests(context);
	}
}
