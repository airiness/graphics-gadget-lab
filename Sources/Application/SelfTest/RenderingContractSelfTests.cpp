#include "Core/Precompiled.h"
#include "Application/SelfTest/RenderingContractSelfTests.h"
#include "Core/Math/MathFunctions.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderView.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "Graphics/Shader/ShaderCompiler.h"

namespace gglab
{
	namespace
	{
		constexpr float ProjectionTolerance = 1.0e-4f;
		constexpr float PositionTolerance = 2.0e-3f;

		struct ProjectedPosition
		{
			Vector2 m_UV = Vector2::Zero;
			float m_RawDepth = 0.0f;
		};

		struct StorageAccessPassData
		{
			RGBufferId m_Buffer;
		};

		[[nodiscard]] bool NearlyEqual(
			float lhs,
			float rhs,
			float tolerance = ProjectionTolerance) noexcept
		{
			return std::abs(lhs - rhs) <= tolerance;
		}

		[[nodiscard]] bool NearlyEqual(
			const Vector2& lhs,
			const Vector2& rhs,
			float tolerance = ProjectionTolerance) noexcept
		{
			return NearlyEqual(lhs.m_X, rhs.m_X, tolerance) &&
				NearlyEqual(lhs.m_Y, rhs.m_Y, tolerance);
		}

		[[nodiscard]] bool NearlyEqual(
			const Vector3& lhs,
			const Vector3& rhs,
			float tolerance = PositionTolerance) noexcept
		{
			return NearlyEqual(lhs.m_X, rhs.m_X, tolerance) &&
				NearlyEqual(lhs.m_Y, rhs.m_Y, tolerance) &&
				NearlyEqual(lhs.m_Z, rhs.m_Z, tolerance);
		}

		[[nodiscard]] ProjectedPosition ProjectPosition(
			const Vector3& position,
			const Matrix& transform) noexcept
		{
			const Vector4 clipPosition = math::Transform(Vector4(position, 1.0f), transform);
			const float inverseW = 1.0f / clipPosition.m_W;
			const Vector2 ndc(
				clipPosition.m_X * inverseW,
				clipPosition.m_Y * inverseW);
			return ProjectedPosition{
				.m_UV = screen_space::NDCToUV(ndc),
				.m_RawDepth = clipPosition.m_Z * inverseW,
			};
		}

		void RunSuiteSmokeTests(SelfTestContext& context) noexcept
		{
			context.Check(
				true,
				"Rendering contract suite executes deterministic checks");
		}

		void RunProjectionConventionTests(SelfTestContext& context) noexcept
		{
			constexpr float NearZ = 0.25f;
			constexpr float FarZ = 250.0f;
			constexpr float FovRadians = math::ToRadians(67.0f);
			constexpr float Aspect = 16.0f / 9.0f;

			const Matrix standardProjection = math::CreatePerspectiveFieldOfViewLH(
				FovRadians, Aspect, NearZ, FarZ);
			const Matrix reversedProjection = math::CreatePerspectiveFieldOfViewLHReversedZ(
				FovRadians, Aspect, NearZ, FarZ);

			const float standardNear =
				ProjectPosition(Vector3(0.0f, 0.0f, NearZ), standardProjection).m_RawDepth;
			const float standardFar =
				ProjectPosition(Vector3(0.0f, 0.0f, FarZ), standardProjection).m_RawDepth;
			const float reversedNear =
				ProjectPosition(Vector3(0.0f, 0.0f, NearZ), reversedProjection).m_RawDepth;
			const float reversedFar =
				ProjectPosition(Vector3(0.0f, 0.0f, FarZ), reversedProjection).m_RawDepth;

			context.Check(
				NearlyEqual(standardNear, 0.0f) &&
					NearlyEqual(standardFar, 1.0f),
				"Standard-Z projection maps near to zero and far to one");
			context.Check(
				NearlyEqual(reversedNear, 1.0f) &&
					NearlyEqual(reversedFar, 0.0f),
				"Reversed-Z projection maps near to one and far to zero");
			context.Check(
				screen_space::GetDepthBackgroundValue(DepthConvention::Standard) == 1.0f &&
					screen_space::GetDepthBackgroundValue(DepthConvention::Reversed) == 0.0f &&
					screen_space::IsDepthBackground(1.0f, DepthConvention::Standard) &&
					screen_space::IsDepthBackground(0.0f, DepthConvention::Reversed),
				"Depth background values are convention-aware");
			context.Check(
				screen_space::IsDepthNearer(0.25f, 0.75f, DepthConvention::Standard) &&
					screen_space::IsDepthFarther(0.75f, 0.25f, DepthConvention::Standard) &&
					screen_space::IsDepthNearer(0.75f, 0.25f, DepthConvention::Reversed) &&
					screen_space::IsDepthFarther(0.25f, 0.75f, DepthConvention::Reversed),
				"Depth ordering is convention-aware");
		}

		void RunPositionReconstructionTests(SelfTestContext& context) noexcept
		{
			constexpr float NearZ = 0.1f;
			constexpr float FarZ = 500.0f;
			const Matrix standardProjection = math::CreatePerspectiveFieldOfViewLH(
				math::ToRadians(60.0f), 16.0f / 9.0f, NearZ, FarZ);
			const Matrix reversedProjection = math::CreatePerspectiveFieldOfViewLHReversedZ(
				math::ToRadians(60.0f), 16.0f / 9.0f, NearZ, FarZ);
			const Vector3 positionVS(0.75f, -0.35f, 17.0f);

			const ProjectedPosition standardSample =
				ProjectPosition(positionVS, standardProjection);
			const ProjectedPosition reversedSample =
				ProjectPosition(positionVS, reversedProjection);
			const Vector3 reconstructedStandard = screen_space::ReconstructViewPosition(
				standardSample.m_UV,
				standardSample.m_RawDepth,
				math::Inverse(standardProjection));
			const Vector3 reconstructedReversed = screen_space::ReconstructViewPosition(
				reversedSample.m_UV,
				reversedSample.m_RawDepth,
				math::Inverse(reversedProjection));

			context.Check(
				NearlyEqual(reconstructedStandard, positionVS),
				"Standard-Z reconstructs a mid-depth view position");
			context.Check(
				NearlyEqual(reconstructedReversed, positionVS),
				"Reversed-Z reconstructs a mid-depth view position");
			context.Check(
				NearlyEqual(
					screen_space::RawDepthToPositiveViewZ(
						reversedSample.m_UV,
						reversedSample.m_RawDepth,
						math::Inverse(reversedProjection)),
					positionVS.m_Z,
					PositionTolerance),
				"Raw depth reconstructs positive left-handed view Z");
			context.Check(
				NearlyEqual(
					screen_space::RawDepthToPositiveViewZ(
						standardSample.m_UV,
						standardSample.m_RawDepth,
						math::Inverse(standardProjection)),
					positionVS.m_Z,
					PositionTolerance),
				"Standard-Z raw depth reconstructs positive left-handed view Z");

			const Matrix view = math::CreateLookAtLH(
				Vector3(3.0f, 2.0f, -4.0f),
				Vector3(0.0f, 1.0f, 5.0f),
				Vector3::UnitY);
			const Matrix viewProjection = view * reversedProjection;
			const Vector3 positionWS(1.25f, 0.5f, 8.0f);
			const ProjectedPosition worldSample = ProjectPosition(positionWS, viewProjection);
			const Vector3 reconstructedWorld = screen_space::ReconstructWorldPosition(
				worldSample.m_UV,
				worldSample.m_RawDepth,
				math::Inverse(viewProjection));
			context.Check(
				NearlyEqual(reconstructedWorld, positionWS),
				"Reversed-Z raw depth reconstructs world position");

			const Matrix standardViewProjection = view * standardProjection;
			const ProjectedPosition standardWorldSample =
				ProjectPosition(positionWS, standardViewProjection);
			const Vector3 reconstructedStandardWorld =
				screen_space::ReconstructWorldPosition(
					standardWorldSample.m_UV,
					standardWorldSample.m_RawDepth,
					math::Inverse(standardViewProjection));
			context.Check(
				NearlyEqual(reconstructedStandardWorld, positionWS),
				"Standard-Z raw depth reconstructs world position");

			const Matrix degenerateInverseTransform{};
			context.Check(
				NearlyEqual(
					screen_space::ReconstructPositionFromRawDepth(
						Vector2(0.5f, 0.5f),
						0.5f,
						degenerateInverseTransform),
					Vector3::Zero),
				"Position reconstruction returns zero when homogeneous W is degenerate");
		}

		void RunScreenCoordinateTests(SelfTestContext& context) noexcept
		{
			const Vector2 pixelCenter = screen_space::PixelCenterToUV(0, 0, 4, 2);
			const Vector2 ndc = screen_space::UVToNDC(pixelCenter);
			context.Check(
				NearlyEqual(pixelCenter, Vector2(0.125f, 0.25f)),
				"Pixel centers map to top-left texture UV coordinates");
			context.Check(
				NearlyEqual(ndc, Vector2(-0.75f, 0.5f)) &&
					NearlyEqual(screen_space::NDCToUV(ndc), pixelCenter),
				"Top-left UV and D3D NDC coordinates round-trip");
		}

		void RunRenderViewConventionTests(SelfTestContext& context) noexcept
		{
			Camera camera(Camera::CreateInfo{});
			const ResolvedViewRenderSettings settings{};
			const RenderView mainView = RenderViewBuilder{}.Build<RenderViewID::Main>(
				RenderViewBuildInfo<RenderViewID::Main>{
					.m_Camera = camera,
					.m_RenderSettings = settings,
					.m_Width = 1280,
					.m_Height = 720,
				});
			const RenderView shadowView =
				RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>(
					RenderViewBuildInfo<RenderViewID::DirectionalShadow>{
						.m_MainView = mainView,
					});

			context.Check(
				mainView.m_DepthConvention == DepthConvention::Standard,
				"Main view records its Standard-Z contract");
			context.Check(
				shadowView.m_DepthConvention == DepthConvention::Standard,
				"Directional shadow view records its Standard-Z contract");
		}

		void RunShaderCompileContractTests(SelfTestContext& context) noexcept
		{
			ShaderCompiler compiler;
			ShaderDesc desc{
				.m_SourcePath = L"Tests/RenderingContractCompile.hlsl",
				.m_Stage = ShaderStage::Compute,
				.m_Model = ShaderModel::SM_6_7,
				.m_Entry = L"CSMain",
				.m_IncludeDirs = { L"." },
				.m_HlslVersion = L"2021",
			};
			const ShaderDesc normalizedDesc = compiler.NormalizeShaderDesc(desc);
			const ShaderCompileArtifact artifact =
				compiler.CompileOrLoadArtifact(normalizedDesc);
			context.Check(
				artifact.m_Binary.IsValid(),
				"Production DXC compiles screen-space and depth reconstruction helpers");
		}

		void RunScreenSpaceAndDepthContractTests(SelfTestContext& context) noexcept
		{
			RunProjectionConventionTests(context);
			RunPositionReconstructionTests(context);
			RunScreenCoordinateTests(context);
			RunRenderViewConventionTests(context);
			RunShaderCompileContractTests(context);
		}

		[[nodiscard]] bool HasDependencyEdge(
			const RGSnapshot& snapshot,
			uint32_t fromPass,
			uint32_t toPass,
			RGDependencyReason reason) noexcept
		{
			return std::ranges::any_of(
				snapshot.m_DependencyEdges,
				[=](const RGSnapshotDependencyEdge& edge)
				{
					return edge.m_FromPassIndex == static_cast<int32_t>(fromPass) &&
						edge.m_ToPassIndex == static_cast<int32_t>(toPass) &&
						edge.m_Reason == reason;
				});
		}

		void RunRenderGraphAccessAndBarrierContractTests(SelfTestContext& context) noexcept
		{
			const RHIResourceState textureStorageState =
				ToRHIResourceState(RGTextureAccess::StorageRead);
			const RHIResourceState bufferStorageState =
				ToRHIResourceState(RGBufferAccess::StorageRead);
			context.Check(
				textureStorageState == ToRHIResourceState(RGTextureAccess::StorageWrite) &&
					textureStorageState == ToRHIResourceState(RGTextureAccess::StorageReadWrite) &&
					bufferStorageState == ToRHIResourceState(RGBufferAccess::StorageWrite) &&
					bufferStorageState == ToRHIResourceState(RGBufferAccess::StorageReadWrite) &&
					ToRHIUsage(RGTextureAccess::StorageRead) ==
						RHITextureUsage::UnorderedAccess &&
					ToRHIUsage(RGTextureAccess::StorageWrite) ==
						RHITextureUsage::UnorderedAccess &&
					ToRHIUsage(RGTextureAccess::StorageReadWrite) ==
						RHITextureUsage::UnorderedAccess &&
					ToRHIUsage(RGBufferAccess::StorageRead) ==
						RHIBufferUsage::UnorderedAccess &&
					ToRHIUsage(RGBufferAccess::StorageWrite) ==
						RHIBufferUsage::UnorderedAccess &&
					ToRHIUsage(RGBufferAccess::StorageReadWrite) ==
						RHIBufferUsage::UnorderedAccess &&
					HasUavAccess(textureStorageState) &&
					HasUavAccess(bufferStorageState),
				"Storage read, write, and read-write accesses map to UAV resource states");
			context.Check(
				IsRGAccessCompatible(
					RGTextureAccess::StorageRead,
					RGDependencyAccess::Read,
					RGOrderingRequirement::Ordered) &&
					IsRGAccessCompatible(
						RGTextureAccess::StorageWrite,
						RGDependencyAccess::Write,
						RGOrderingRequirement::Ordered) &&
					IsRGAccessCompatible(
						RGBufferAccess::StorageReadWrite,
						RGDependencyAccess::ReadWrite,
						RGOrderingRequirement::Unordered) &&
					!IsRGAccessCompatible(
						RGTextureAccess::StorageRead,
						RGDependencyAccess::Write,
						RGOrderingRequirement::Ordered) &&
					!IsRGAccessCompatible(
						RGBufferAccess::StructuredRead,
						RGDependencyAccess::Read,
						RGOrderingRequirement::Unordered),
				"Storage access and ordering declarations reject incompatible semantics");

			RenderGraph graph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			RGBufferId storageBuffer;
			graph.AddPass<StorageAccessPassData>(
				"InitialStorageWrite",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					storageBuffer = builder.CreateBuffer("StorageBuffer");
					builder.WriteInPlace(storageBuffer, RGBufferAccess::StorageWrite);
					data.m_Buffer = storageBuffer;
				});
			graph.AddPass<StorageAccessPassData>(
				"StorageRead",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(
						storageBuffer,
						RGBufferAccess::StorageRead,
						std::nullopt,
						RGOrderingRequirement::Unordered);
					builder.SideEffect();
				});
			graph.AddPass<StorageAccessPassData>(
				"SecondStorageWrite",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					builder.WriteInPlace(storageBuffer, RGBufferAccess::StorageWrite);
					data.m_Buffer = storageBuffer;
				});
			graph.AddPass<StorageAccessPassData>(
				"StorageReadWrite",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(
						storageBuffer,
						RGBufferAccess::StorageReadWrite);
					data.m_Buffer = storageBuffer;
				});
			graph.AddPass<StorageAccessPassData>(
				"FinalStorageRead",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(
						storageBuffer,
						RGBufferAccess::StorageRead);
					builder.SideEffect();
				});
			graph.AddPass<StorageAccessPassData>(
				"UnusedStorageWrite",
				[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.CreateBuffer("UnusedStorageBuffer");
					builder.WriteInPlace(data.m_Buffer, RGBufferAccess::StorageWrite);
				});

			const bool compiled = graph.Compile();
			context.Check(compiled, "Storage access RenderGraph fixture compiles");
			if (!compiled)
			{
				return;
			}

			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const auto& initialWrite = snapshot.m_Passes[0];
			const auto& storageRead = snapshot.m_Passes[1];
			const auto& secondWrite = snapshot.m_Passes[2];
			const auto& storageReadWrite = snapshot.m_Passes[3];
			const auto& finalRead = snapshot.m_Passes[4];
			const auto& unusedWrite = snapshot.m_Passes[5];

			context.Check(
				initialWrite.m_Accesses[0].m_DependencyAccess == RGDependencyAccess::Write &&
					initialWrite.m_Accesses[0].m_AccessValue ==
						static_cast<uint64_t>(RGBufferAccess::StorageWrite) &&
					storageRead.m_Accesses[0].m_DependencyAccess == RGDependencyAccess::Read &&
					storageRead.m_Accesses[0].m_AccessValue ==
						static_cast<uint64_t>(RGBufferAccess::StorageRead) &&
					storageReadWrite.m_Accesses[0].m_DependencyAccess ==
						RGDependencyAccess::ReadWrite &&
					storageReadWrite.m_Accesses[0].m_AccessValue ==
						static_cast<uint64_t>(RGBufferAccess::StorageReadWrite),
				"Storage access semantics remain distinct in RenderGraph snapshots");
			context.Check(
				storageRead.m_Accesses[0].m_Ordering == RGOrderingRequirement::Unordered &&
					initialWrite.m_Accesses[0].m_Ordering == RGOrderingRequirement::Ordered,
				"RenderGraph snapshots preserve explicit and default ordering requirements");
			context.Check(
				HasDependencyEdge(
					snapshot, 0, 1, RGDependencyReason::WriterToReader) &&
					HasDependencyEdge(
						snapshot, 1, 2, RGDependencyReason::PreviousReaderToWriter),
				"RenderGraph creates writer-to-reader and reader-to-writer edges");
			context.Check(
				HasDependencyEdge(
					snapshot, 0, 2, RGDependencyReason::PreviousWriterToWriter),
				"RenderGraph creates writer-to-writer edges");
			context.Check(
				HasDependencyEdge(
					snapshot, 2, 3, RGDependencyReason::WriterToReader) &&
					HasDependencyEdge(
						snapshot, 3, 4, RGDependencyReason::WriterToReader),
				"Read-write storage access participates in incoming and outgoing dependencies");
			context.Check(
				!initialWrite.m_PreBarriers.empty() &&
					!storageRead.m_PreBarriers.empty() &&
					!secondWrite.m_PreBarriers.empty() &&
					!storageReadWrite.m_PreBarriers.empty() &&
					!finalRead.m_PreBarriers.empty(),
				"Storage access split preserves conservative UAV barrier coverage");
			context.Check(
				unusedWrite.m_Culled &&
					unusedWrite.m_ExecutionOrder < 0 &&
					unusedWrite.m_PreBarriers.empty() &&
					unusedWrite.m_PostBarriers.empty(),
				"Culled storage writers leave no execution or barrier work");
		}

		void RunTemporalCompatibilityAndHistoryContractTests(SelfTestContext&) noexcept
		{
		}
	}

	void RunRenderingContractSelfTests(SelfTestContext& context) noexcept
	{
		RunSuiteSmokeTests(context);
		RunScreenSpaceAndDepthContractTests(context);
		RunRenderGraphAccessAndBarrierContractTests(context);
		RunTemporalCompatibilityAndHistoryContractTests(context);
	}
}
