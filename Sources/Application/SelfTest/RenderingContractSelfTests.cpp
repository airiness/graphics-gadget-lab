#include "Core/Precompiled.h"
#include "Application/SelfTest/RenderingContractSelfTests.h"
#include "Core/Math/MathFunctions.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHITextureValidation.h"
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

		struct DualStorageAccessPassData
		{
			RGBufferId m_FirstBuffer;
			RGBufferId m_SecondBuffer;
		};

		struct TextureStorageAccessPassData
		{
			RGTextureId m_Texture;
		};

		struct BarrierBatchingPassData
		{
			RGTextureId m_Texture;
			RGBufferId m_Buffer;
		};

		class RecordingDevice final : public RHIDevice
		{
		public:
			RHIBackendType GetBackendType() const noexcept override { return {}; }
			std::string_view GetAdapterCompatibilityIdentity() const noexcept override
			{
				return "RenderingContract.RecordingDevice";
			}
			RHITextureSupportResult QueryTextureSupport(
				const RHITextureDesc&) const noexcept override
			{
				return {};
			}
			RHITextureSupportResult QueryTextureViewSupport(
				const RHITextureDesc&,
				const RHITextureViewDesc&) const noexcept override
			{
				return {};
			}
			RHITextureHandle CreateTexture(
				const RHITextureDesc&,
				const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return {};
			}
			RHIBufferHandle CreateBuffer(
				const RHIBufferDesc&,
				const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return {};
			}
			RHITextureViewHandle CreateTextureView(
				RHITextureHandle,
				const RHITextureViewDesc&) noexcept override
			{
				return {};
			}
			RHIBufferViewHandle CreateBufferView(
				RHIBufferHandle,
				const RHIBufferViewDesc&) noexcept override
			{
				return {};
			}
			RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override
			{
				return {};
			}
			void DestroyTexture(RHITextureHandle) noexcept override {}
			void DestroyBuffer(RHIBufferHandle) noexcept override {}
			void DestroyTextureView(RHITextureViewHandle) noexcept override {}
			void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
			void DestroySampler(RHISamplerHandle) noexcept override {}
			void SetTextureDebugBinding(
				RHITextureHandle,
				const RHIResourceDebugBindingDesc&) noexcept override {}
			void SetBufferDebugBinding(
				RHIBufferHandle,
				const RHIResourceDebugBindingDesc&) noexcept override {}
			std::string_view GetTextureDebugName(
				RHITextureHandle) const noexcept override
			{
				return {};
			}
			std::string_view GetBufferDebugName(
				RHIBufferHandle) const noexcept override
			{
				return {};
			}
			void* MapBuffer(
				RHIBufferHandle,
				RHIMappedBufferRange) noexcept override
			{
				return nullptr;
			}
			void UnmapBuffer(
				RHIBufferHandle,
				RHIMappedBufferRange) noexcept override {}
			uint32_t GetBufferViewAlignment(
				RHIBufferViewType) const noexcept override
			{
				return 1;
			}
			bool IsAlive(RHITextureHandle texture) const noexcept override
			{
				return texture.IsValid();
			}
			bool IsAlive(RHIBufferHandle buffer) const noexcept override
			{
				return buffer.IsValid();
			}
			bool IsAlive(RHISamplerHandle sampler) const noexcept override
			{
				return sampler.IsValid();
			}
			bool IsFencePointCompleted(
				const RHIFencePoint&) const noexcept override
			{
				return true;
			}
			void RecordTextureUse(
				RHITextureHandle,
				const RHIFencePoint&) noexcept override {}
			void RecordBufferUse(
				RHIBufferHandle,
				const RHIFencePoint&) noexcept override {}
			RHIDescriptorHandle GetTextureViewDescriptor(
				RHITextureViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetBufferViewDescriptor(
				RHIBufferViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetSamplerDescriptor(
				RHISamplerHandle) const noexcept override
			{
				return {};
			}
			void RetireCompletedWork() noexcept override {}
		};

		class RecordingGraphicsCommandContext final : public RHIGraphicsCommandContext
		{
		public:
			RHICommandContextHandle GetHandle() const noexcept override { return {}; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override
			{
				m_TextureBarrierCount += static_cast<uint32_t>(barriers.size());
			}
			void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
			{
				m_BufferBarrierCount += static_cast<uint32_t>(barriers.size());
			}
			void FlushBarriers() noexcept override { ++m_FlushBarrierCount; }
			void BeginGpuProfileScope(std::string_view) noexcept override { ++m_BeginProfileCount; }
			void EndGpuProfileScope() noexcept override { ++m_EndProfileCount; }
			void SetPipeline(RHIPipelineHandle) noexcept override {}
			void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
			void SetRenderTargets(
				std::span<const RHITextureViewHandle>,
				RHITextureViewHandle) noexcept override {}
			void ClearColor(
				RHITextureViewHandle,
				const std::array<float, 4>&) noexcept override {}
			void ClearDepthStencil(
				RHITextureViewHandle,
				float,
				std::optional<uint8_t>) noexcept override {}
			void SetViewport(const RHIViewport&) noexcept override {}
			void SetScissorRect(const RHIScissorRect&) noexcept override {}
			void SetPrimitiveTopology(RHIPrimitiveTopology) noexcept override {}
			void SetConstantBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetPushConstants(
				uint32_t,
				std::span<const uint32_t>,
				uint32_t) noexcept override {}
			void SetVertexBuffers(
				uint32_t,
				std::span<const RHIVertexBufferBinding>) noexcept override {}
			void SetIndexBuffer(const RHIIndexBufferBinding&) noexcept override {}
			void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) noexcept override {}
			void Draw(uint32_t, uint32_t, uint32_t, uint32_t) noexcept override {}

			uint32_t m_BeginProfileCount = 0;
			uint32_t m_EndProfileCount = 0;
			uint32_t m_FlushBarrierCount = 0;
			uint32_t m_TextureBarrierCount = 0;
			uint32_t m_BufferBarrierCount = 0;
		};

		class RecordingComputeCommandContext final : public RHIComputeCommandContext
		{
		public:
			RHICommandContextHandle GetHandle() const noexcept override { return {}; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override
			{
				m_TextureBarrierCount += static_cast<uint32_t>(barriers.size());
			}
			void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
			{
				m_BufferBarrierCount += static_cast<uint32_t>(barriers.size());
			}
			void FlushBarriers() noexcept override { ++m_FlushBarrierCount; }
			void BeginGpuProfileScope(std::string_view) noexcept override { ++m_BeginProfileCount; }
			void EndGpuProfileScope() noexcept override { ++m_EndProfileCount; }
			void SetPipeline(RHIPipelineHandle) noexcept override {}
			void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
			void SetConstantBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadWriteBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetPushConstants(
				uint32_t,
				std::span<const uint32_t>,
				uint32_t) noexcept override {}
			void Dispatch(uint32_t, uint32_t, uint32_t) noexcept override {}

			uint32_t m_BeginProfileCount = 0;
			uint32_t m_EndProfileCount = 0;
			uint32_t m_FlushBarrierCount = 0;
			uint32_t m_TextureBarrierCount = 0;
			uint32_t m_BufferBarrierCount = 0;
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

			const float mainNear = ProjectPosition(
				Vector3(0.0f, 0.0f, mainView.m_Near),
				mainView.m_Proj).m_RawDepth;
			const float mainFar = ProjectPosition(
				Vector3(0.0f, 0.0f, mainView.m_Far),
				mainView.m_Proj).m_RawDepth;
			context.Check(
				mainView.m_DepthConvention == DepthConvention::Reversed &&
					NearlyEqual(mainNear, 1.0f) &&
					NearlyEqual(mainFar, 0.0f),
				"Main view records and projects with its Reversed-Z contract");
			context.Check(
				shadowView.m_DepthConvention == DepthConvention::Standard,
				"Directional shadow view records its Standard-Z contract");
		}

		void RunSampleableDepthFormatTests(SelfTestContext& context) noexcept
		{
			const RHITextureDesc depthDesc{
				.m_Format = RHIFormat::R32Typeless,
				.m_Usage =
					RHITextureUsage::Sampled |
					RHITextureUsage::DepthStencil,
				.m_Extent = { 1280, 720, 1 },
				.m_ClearValue = RHIClearValue{
					.m_Format = RHIFormat::D32Float,
					.m_Depth = 0.0f,
					.m_IsDepthStencil = true,
				},
			};
			const RHITextureViewDesc dsvDesc{
				.m_Type = RHITextureViewType::DepthStencil,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::D32Float,
				.m_Subresources = {
					.m_MipCount = 1,
					.m_ArraySliceCount = 1,
					.m_Aspects = RHITextureAspect::Depth,
				},
			};
			const RHITextureViewDesc srvDesc{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R32Float,
				.m_Subresources = {
					.m_MipCount = 1,
					.m_ArraySliceCount = 1,
					.m_Aspects = RHITextureAspect::Depth,
				},
			};
			context.Check(
				ValidateRHITextureDesc(depthDesc).IsValid() &&
					ValidateRHITextureViewDesc(depthDesc, dsvDesc).IsValid() &&
					ValidateRHITextureViewDesc(depthDesc, srvDesc).IsValid(),
				"R32 typeless main depth accepts typed D32 DSV and R32 SRV views");

			RHITextureDesc typelessClearDesc = depthDesc;
			typelessClearDesc.m_ClearValue->m_Format = RHIFormat::R32Typeless;
			context.Check(
				!ValidateRHITextureDesc(typelessClearDesc).IsValid(),
				"Optimized clear values reject typeless formats");

			GraphicsPipelineRecipe reversedWriteRecipe{};
			reversedWriteRecipe.m_DepthPreset = DepthPreset::ReversedZWrite;
			const auto reversedWrite =
				BuildRHIGraphicsPipelineDesc(reversedWriteRecipe).m_DepthStencil;
			GraphicsPipelineRecipe reversedReadRecipe{};
			reversedReadRecipe.m_DepthPreset = DepthPreset::ReversedZReadOnly;
			const auto reversedRead =
				BuildRHIGraphicsPipelineDesc(reversedReadRecipe).m_DepthStencil;
			GraphicsPipelineRecipe standardWriteRecipe{};
			standardWriteRecipe.m_DepthPreset = DepthPreset::StandardZWrite;
			const auto standardWrite =
				BuildRHIGraphicsPipelineDesc(standardWriteRecipe).m_DepthStencil;
			context.Check(
				reversedWrite.m_DepthTestEnable &&
					reversedWrite.m_DepthWriteEnable &&
					reversedWrite.m_DepthCompareOp == RHICompareOp::GreaterEqual &&
					reversedRead.m_DepthTestEnable &&
					!reversedRead.m_DepthWriteEnable &&
					reversedRead.m_DepthCompareOp == RHICompareOp::GreaterEqual &&
					standardWrite.m_DepthTestEnable &&
					standardWrite.m_DepthWriteEnable &&
					standardWrite.m_DepthCompareOp == RHICompareOp::Less,
				"Depth presets encode explicit Reversed-Z and Standard-Z compare contracts");

			struct SampleableDepthPassData
			{
				RGTextureViewId m_View{};
			};
			RenderGraph graph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			RGTextureId graphDepth;
			RHITextureDesc graphDepthDesc = depthDesc;
			graphDepthDesc.m_Usage = RHITextureUsage::None;
			graph.AddPass<SampleableDepthPassData>(
				"SampleableDepth.Write",
				[&graphDepth, graphDepthDesc, dsvDesc](
					RenderGraph::RGBuilder& builder,
					SampleableDepthPassData& data)
				{
					graphDepth = builder.CreateTexture(
						"DisplayView.DepthBuffer",
						graphDepthDesc);
					builder.WriteInPlace(
						graphDepth,
						RGTextureAccess::DepthStencilWrite);
					data.m_View =
						builder.CreateView<RHITextureViewType::DepthStencil>(
							graphDepth,
							dsvDesc);
				});
			graph.AddPass<SampleableDepthPassData>(
				"SampleableDepth.Sample",
				[&graphDepth, srvDesc](
					RenderGraph::RGBuilder& builder,
					SampleableDepthPassData& data)
				{
					graphDepth = builder.Read(
						graphDepth,
						RGTextureAccess::Sample,
						RHIStage::PixelShader);
					data.m_View =
						builder.CreateView<RHITextureViewType::ShaderResource>(
							graphDepth,
							srvDesc);
					builder.SideEffect();
				});

			const bool graphCompiled = graph.Compile();
			context.Check(
				graphCompiled,
				"RenderGraph sampleable-depth DSV-to-SRV fixture compiles");
			if (graphCompiled)
			{
				RGSnapshot snapshot;
				BuildRenderGraphSnapshot(graph, snapshot);
				const auto expectedUsage =
					RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
				context.Check(
					snapshot.m_Resources.size() == 1 &&
						snapshot.m_Resources[0].m_UsageBits ==
							static_cast<uint64_t>(expectedUsage) &&
						snapshot.m_Resources[0].m_TextureFormat ==
							RHIFormat::R32Typeless &&
						snapshot.m_Passes[0].m_Accesses[0].m_AccessValue ==
							static_cast<uint64_t>(
								RGTextureAccess::DepthStencilWrite) &&
						snapshot.m_Passes[1].m_Accesses[0].m_AccessValue ==
							static_cast<uint64_t>(RGTextureAccess::Sample) &&
						snapshot.m_Passes[1].m_PreBarriers.size() == 1 &&
						snapshot.m_Passes[1].m_PreBarriers[0].m_Kind ==
							RGBarrierKind::Transition,
					"RenderGraph infers depth and sampled usage with a DSV-to-SRV transition");
			}
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
			RunSampleableDepthFormatTests(context);
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
			struct TextureCompatibilityRow
			{
				RGTextureAccess m_Access;
				std::array<bool, 3> m_DependencyAccesses;
			};
			constexpr std::array TextureCompatibility =
			{
				TextureCompatibilityRow{ RGTextureAccess::None, { false, false, false } },
				TextureCompatibilityRow{ RGTextureAccess::Sample, { true, false, false } },
				TextureCompatibilityRow{ RGTextureAccess::RenderTarget, { false, true, true } },
				TextureCompatibilityRow{ RGTextureAccess::DepthStencilWrite, { false, true, true } },
				TextureCompatibilityRow{ RGTextureAccess::DepthStencilRead, { true, false, false } },
				TextureCompatibilityRow{ RGTextureAccess::StorageRead, { true, false, false } },
				TextureCompatibilityRow{ RGTextureAccess::StorageWrite, { false, true, false } },
				TextureCompatibilityRow{ RGTextureAccess::StorageReadWrite, { false, false, true } },
				TextureCompatibilityRow{ RGTextureAccess::CopySource, { true, false, false } },
				TextureCompatibilityRow{ RGTextureAccess::CopyDest, { false, true, false } },
				TextureCompatibilityRow{ RGTextureAccess::Present, { false, false, false } },
			};
			struct BufferCompatibilityRow
			{
				RGBufferAccess m_Access;
				std::array<bool, 3> m_DependencyAccesses;
			};
			constexpr std::array BufferCompatibility =
			{
				BufferCompatibilityRow{ RGBufferAccess::None, { false, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::Vertex, { true, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::Index, { true, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::Constant, { true, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::StructuredRead, { true, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::StorageRead, { true, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::StorageWrite, { false, true, false } },
				BufferCompatibilityRow{ RGBufferAccess::StorageReadWrite, { false, false, true } },
				BufferCompatibilityRow{ RGBufferAccess::CopySource, { true, false, false } },
				BufferCompatibilityRow{ RGBufferAccess::CopyDest, { false, true, false } },
				BufferCompatibilityRow{ RGBufferAccess::IndirectArgument, { true, false, false } },
			};
			constexpr std::array DependencyAccesses =
			{
				RGDependencyAccess::Read,
				RGDependencyAccess::Write,
				RGDependencyAccess::ReadWrite,
			};
			static_assert(TextureCompatibility.size() ==
				static_cast<size_t>(RGTextureAccess::Present) + 1);
			static_assert(BufferCompatibility.size() ==
				static_cast<size_t>(RGBufferAccess::IndirectArgument) + 1);

			bool completeCompatibilityMatrixMatches = true;
			for (const auto& row : TextureCompatibility)
			{
				for (size_t accessIndex = 0; accessIndex < DependencyAccesses.size(); ++accessIndex)
				{
					const auto dependencyAccess = DependencyAccesses[accessIndex];
					completeCompatibilityMatrixMatches &=
						IsRGAccessCompatible(
							row.m_Access,
							dependencyAccess,
							RGOrderingRequirement::Ordered) ==
							row.m_DependencyAccesses[accessIndex];
					completeCompatibilityMatrixMatches &=
						IsRGAccessCompatible(
							row.m_Access,
							dependencyAccess,
							RGOrderingRequirement::Unordered) ==
							(row.m_DependencyAccesses[accessIndex] && IsStorageAccess(row.m_Access));
				}
			}
			for (const auto& row : BufferCompatibility)
			{
				for (size_t accessIndex = 0; accessIndex < DependencyAccesses.size(); ++accessIndex)
				{
					const auto dependencyAccess = DependencyAccesses[accessIndex];
					completeCompatibilityMatrixMatches &=
						IsRGAccessCompatible(
							row.m_Access,
							dependencyAccess,
							RGOrderingRequirement::Ordered) ==
							row.m_DependencyAccesses[accessIndex];
					completeCompatibilityMatrixMatches &=
						IsRGAccessCompatible(
							row.m_Access,
							dependencyAccess,
							RGOrderingRequirement::Unordered) ==
							(row.m_DependencyAccesses[accessIndex] && IsStorageAccess(row.m_Access));
				}
			}
			context.Check(
				completeCompatibilityMatrixMatches,
				"RenderGraph validates the complete texture and buffer access compatibility matrix");

			bool orderedUavMatrixMatches = true;
			for (const auto beforeAccess : DependencyAccesses)
			{
				for (const auto afterAccess : DependencyAccesses)
				{
					const bool expected =
						beforeAccess != RGDependencyAccess::Read ||
						afterAccess != RGDependencyAccess::Read;
					orderedUavMatrixMatches &=
						NeedsOrderedUavBarrier(
							beforeAccess,
							RGOrderingRequirement::Ordered,
							afterAccess,
							RGOrderingRequirement::Ordered) == expected;
					orderedUavMatrixMatches &=
						!NeedsOrderedUavBarrier(
							beforeAccess,
							RGOrderingRequirement::Unordered,
							afterAccess,
							RGOrderingRequirement::Ordered);
					orderedUavMatrixMatches &=
						!NeedsOrderedUavBarrier(
							beforeAccess,
							RGOrderingRequirement::Ordered,
							afterAccess,
							RGOrderingRequirement::Unordered);
				}
			}
			context.Check(
				orderedUavMatrixMatches,
				"Ordered UAV hazards cover the complete read, write, and read-write matrix");
			const RHIResourceState pixelReadState =
				ToRHIResourceState(RGBufferAccess::StructuredRead, RHIStage::PixelShader);
			const RHIResourceState computeReadState =
				ToRHIResourceState(RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
			context.Check(
				!NeedsRHIResourceTransition(pixelReadState, computeReadState),
				"Stage-only changes do not alter persistent resource state");

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
					initialWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Transition &&
					storageRead.m_PreBarriers.empty() &&
					secondWrite.m_PreBarriers.size() == 1 &&
					secondWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav &&
					storageReadWrite.m_PreBarriers.size() == 1 &&
					storageReadWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav &&
					finalRead.m_PreBarriers.size() == 1 &&
					finalRead.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav,
				"Storage barriers distinguish transitions, ordered hazards, and explicit unordered access");
			context.Check(
				unusedWrite.m_Culled &&
					unusedWrite.m_ExecutionOrder < 0 &&
					unusedWrite.m_PreBarriers.empty() &&
					unusedWrite.m_PostBarriers.empty(),
				"Culled storage writers leave no execution or barrier work");

			RenderGraph transitionGraph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			RGBufferId transitionBuffer;
			transitionGraph.AddPass<StorageAccessPassData>(
				"TransitionStorageWrite",
				[&transitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					transitionBuffer = builder.CreateBuffer("TransitionBuffer");
					builder.WriteInPlace(transitionBuffer, RGBufferAccess::StorageWrite);
					data.m_Buffer = transitionBuffer;
				});
			transitionGraph.AddPass<StorageAccessPassData>(
				"TransitionStructuredRead",
				[&transitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(transitionBuffer, RGBufferAccess::StructuredRead);
					builder.SideEffect();
				});
			const bool transitionCompiled = transitionGraph.Compile();
			context.Check(transitionCompiled, "UAV-to-SRV transition fixture compiles");
			if (transitionCompiled)
			{
				RGSnapshot transitionSnapshot;
				BuildRenderGraphSnapshot(transitionGraph, transitionSnapshot);
				const auto& barriers = transitionSnapshot.m_Passes[1].m_PreBarriers;
				context.Check(
					barriers.size() == 1 &&
						barriers[0].m_Kind == RGBarrierKind::Transition &&
						barriers[0].m_Reason == RGBarrierReason::AccessTransition,
					"UAV-to-SRV edges emit one transition without a duplicate UAV barrier");
			}

			RenderGraph resourceSpecificGraph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			RGBufferId firstBuffer;
			RGBufferId secondBuffer;
			resourceSpecificGraph.AddPass<DualStorageAccessPassData>(
				"WriteTwoStorageBuffers",
				[&](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
				{
					firstBuffer = builder.CreateBuffer("FirstStorageBuffer");
					secondBuffer = builder.CreateBuffer("SecondStorageBuffer");
					builder.WriteInPlace(firstBuffer, RGBufferAccess::StorageWrite);
					builder.WriteInPlace(secondBuffer, RGBufferAccess::StorageWrite);
					data.m_FirstBuffer = firstBuffer;
					data.m_SecondBuffer = secondBuffer;
				});
			resourceSpecificGraph.AddPass<DualStorageAccessPassData>(
				"ReadWriteTwoStorageBuffers",
				[&](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(firstBuffer, RGBufferAccess::StorageReadWrite);
					builder.ReadWriteInPlace(secondBuffer, RGBufferAccess::StorageReadWrite);
					data.m_FirstBuffer = firstBuffer;
					data.m_SecondBuffer = secondBuffer;
					builder.SideEffect();
				});
			const bool resourceSpecificCompiled = resourceSpecificGraph.Compile();
			context.Check(resourceSpecificCompiled, "Resource-specific UAV barrier fixture compiles");
			if (resourceSpecificCompiled)
			{
				RGSnapshot resourceSpecificSnapshot;
				BuildRenderGraphSnapshot(resourceSpecificGraph, resourceSpecificSnapshot);
				const auto& barriers = resourceSpecificSnapshot.m_Passes[1].m_PreBarriers;
				context.Check(
					barriers.size() == 2 &&
						barriers[0].m_Kind == RGBarrierKind::Uav &&
						barriers[1].m_Kind == RGBarrierKind::Uav &&
						barriers[0].m_VirtualResourceIndex != barriers[1].m_VirtualResourceIndex,
					"Each ordered UAV hazard retains its own resource identity for physical resolution");
			}

			RenderGraph multiSubresourceGraph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			RGTextureId mipTexture;
			const RHITextureDesc mipTextureDesc =
			{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Extent = { .m_Width = 4, .m_Height = 4, .m_Depth = 1 },
				.m_MipLevels = 3,
			};
			const RHISubresourceRange firstMip =
			{
				.m_BaseMip = 0,
				.m_MipCount = 1,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange firstTwoMips =
			{
				.m_BaseMip = 0,
				.m_MipCount = 2,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange thirdMip =
			{
				.m_BaseMip = 2,
				.m_MipCount = 1,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>(
				"WriteFirstMip",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					mipTexture = builder.CreateTexture("MipTexture", mipTextureDesc);
					builder.WriteInPlace(
						mipTexture,
						RGTextureAccess::StorageWrite,
						firstMip);
					data.m_Texture = mipTexture;
				});
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>(
				"SampleThirdMip",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					data.m_Texture = builder.Read(
						mipTexture,
						RGTextureAccess::Sample,
						RHIStage::PixelShader,
						thirdMip);
					builder.SideEffect();
				});
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>(
				"ReadWriteFirstTwoMips",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(
						mipTexture,
						RGTextureAccess::StorageReadWrite,
						firstTwoMips);
					data.m_Texture = mipTexture;
					builder.SideEffect();
				});
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>(
				"CopyToThirdMip",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					builder.WriteInPlace(
						mipTexture,
						RGTextureAccess::CopyDest,
						thirdMip);
					data.m_Texture = mipTexture;
					builder.SideEffect();
				});
			const bool multiSubresourceCompiled = multiSubresourceGraph.Compile();
			context.Check(
				multiSubresourceCompiled,
				"Multi-subresource texture UAV barrier fixture compiles");
			if (multiSubresourceCompiled)
			{
				RGSnapshot multiSubresourceSnapshot;
				BuildRenderGraphSnapshot(multiSubresourceGraph, multiSubresourceSnapshot);
				const auto& barriers = multiSubresourceSnapshot.m_Passes[2].m_PreBarriers;
				const auto transition = std::ranges::find(
					barriers,
					RGBarrierKind::Transition,
					&RGSnapshotBarrierInfo::m_Kind);
				const auto uav = std::ranges::find(
					barriers,
					RGBarrierKind::Uav,
					&RGSnapshotBarrierInfo::m_Kind);
				context.Check(
					barriers.size() == 2 &&
						transition != barriers.end() &&
						transition->m_Subresources &&
						transition->m_Subresources->m_BaseMip == 1 &&
						transition->m_Subresources->m_MipCount == 1 &&
						uav != barriers.end() &&
						uav->m_Subresources == firstMip &&
						uav->m_Before.m_Layout == RHILayout::UnorderedAccess &&
						uav->m_After.m_Layout == RHILayout::UnorderedAccess,
					"Texture transitions and Enhanced UAV ordering retain exact subresource state");
				const auto& copyBarriers =
					multiSubresourceSnapshot.m_Passes[3].m_PreBarriers;
				context.Check(
					copyBarriers.size() == 1 &&
						copyBarriers[0].m_Kind == RGBarrierKind::Transition &&
						copyBarriers[0].m_Subresources == thirdMip &&
						Test(copyBarriers[0].m_Before.m_Stages, RHIStage::PixelShader),
					"A texture UAV barrier preserves non-UAV synchronization scopes on untouched subresources");
			}

			RenderGraph stageScopeGraph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			RGBufferId stageScopeBuffer;
			stageScopeGraph.AddPass<StorageAccessPassData>(
				"InitializeStageScopeBuffer",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					stageScopeBuffer = builder.CreateBuffer("StageScopeBuffer");
					builder.WriteInPlace(stageScopeBuffer, RGBufferAccess::CopyDest);
					data.m_Buffer = stageScopeBuffer;
				});
			stageScopeGraph.AddPass<StorageAccessPassData>(
				"PixelRead",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(
						stageScopeBuffer,
						RGBufferAccess::StructuredRead,
						RHIStage::PixelShader);
					builder.SideEffect();
				});
			stageScopeGraph.AddPass<StorageAccessPassData>(
				"ComputeRead",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(
						stageScopeBuffer,
						RGBufferAccess::StructuredRead,
						RHIStage::ComputeShader);
					builder.SideEffect();
				});
			stageScopeGraph.AddPass<StorageAccessPassData>(
				"CopyAfterReads",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					builder.WriteInPlace(stageScopeBuffer, RGBufferAccess::CopyDest);
					data.m_Buffer = stageScopeBuffer;
					builder.SideEffect();
				});
			const bool stageScopeCompiled = stageScopeGraph.Compile();
			context.Check(stageScopeCompiled, "Barrier synchronization-scope fixture compiles");
			if (stageScopeCompiled)
			{
				RGSnapshot stageScopeSnapshot;
				BuildRenderGraphSnapshot(stageScopeGraph, stageScopeSnapshot);
				const auto& computeRead = stageScopeSnapshot.m_Passes[2];
				const auto& copyAfterReads = stageScopeSnapshot.m_Passes[3];
				context.Check(
					computeRead.m_PreBarriers.empty() &&
						copyAfterReads.m_PreBarriers.size() == 1 &&
						Test(copyAfterReads.m_PreBarriers[0].m_Before.m_Stages, RHIStage::PixelShader) &&
						Test(copyAfterReads.m_PreBarriers[0].m_Before.m_Stages, RHIStage::ComputeShader),
					"Read-only stage scopes accumulate until a persistent state transition");
			}

			RecordingGraphicsCommandContext graphicsContext;
			RecordingComputeCommandContext directComputeContext;
			uint32_t graphicsExecutions = 0;
			uint32_t computeExecutions = 0;
			uint32_t copyExecutions = 0;
			uint32_t culledComputeExecutions = 0;
			bool graphicsContextSelected = false;
			bool directComputeContextSelected = false;
			bool asyncComputeRemainsUnavailable = false;
			RenderGraph encoderGraph(
				{
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{ 1 }),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			encoderGraph.AddTrivialSideEffectPass(
				"GraphicsEncoder",
				[&](RGExecuteContext& executeContext)
				{
					++graphicsExecutions;
					graphicsContextSelected =
						executeContext.GetGraphicsCommandContext() == &graphicsContext;
				});
			encoderGraph.AddTrivialSideEffectPass(
				"ComputeEncoder",
				RGPassEncoderType::Compute,
				[&](RGExecuteContext& executeContext)
				{
					++computeExecutions;
					directComputeContextSelected =
						executeContext.GetDirectComputeCommandContext() == &directComputeContext;
					asyncComputeRemainsUnavailable =
						executeContext.GetAsyncComputeCommandContext() == nullptr;
				});
			encoderGraph.AddTrivialSideEffectPass(
				"CopyEncoder",
				RGPassEncoderType::Copy,
				[&](RGExecuteContext&)
				{
					++copyExecutions;
				});
			encoderGraph.AddPass<StorageAccessPassData>(
				"CulledComputeEncoder",
				RGPassEncoderType::Compute,
				[](RenderGraph::RGBuilder&, StorageAccessPassData&) {},
				[&](RGExecuteContext&, StorageAccessPassData&)
				{
					++culledComputeExecutions;
				});
			const bool encoderGraphCompiled = encoderGraph.Compile();
			context.Check(encoderGraphCompiled, "RenderGraph pass encoder fixture compiles");
			if (encoderGraphCompiled)
			{
				RGSnapshot encoderSnapshot;
				BuildRenderGraphSnapshot(encoderGraph, encoderSnapshot);
				context.Check(
					encoderSnapshot.m_Passes.size() == 4 &&
						encoderSnapshot.m_Passes[0].m_EncoderType == RGPassEncoderType::Graphics &&
						encoderSnapshot.m_Passes[1].m_EncoderType == RGPassEncoderType::Compute &&
						encoderSnapshot.m_Passes[2].m_EncoderType == RGPassEncoderType::Copy &&
						encoderSnapshot.m_Passes[3].m_EncoderType == RGPassEncoderType::Compute &&
						encoderSnapshot.m_Passes[3].m_Culled,
					"RenderGraph snapshots preserve encoder metadata for live and culled passes");

				RGExecuteContext executeContext(
					{
						.m_GraphicsCommandContext = &graphicsContext,
						.m_DirectComputeCommandContext = &directComputeContext,
						.m_AsyncComputeCommandContext = nullptr,
					});
				encoderGraph.Execute(executeContext);
				context.Check(
					graphicsExecutions == 1 &&
						computeExecutions == 1 &&
						copyExecutions == 1 &&
						culledComputeExecutions == 0 &&
						graphicsContextSelected &&
						directComputeContextSelected &&
						asyncComputeRemainsUnavailable,
					"RenderGraph executes each live pass with its declared direct-list encoder");
				context.Check(
					graphicsContext.m_BeginProfileCount == 2 &&
						graphicsContext.m_EndProfileCount == 2 &&
						directComputeContext.m_BeginProfileCount == 1 &&
						directComputeContext.m_EndProfileCount == 1,
					"RenderGraph profiles Graphics, Compute, and Copy on their selected command contexts");
			}

			RecordingGraphicsCommandContext batchingGraphicsContext;
			RecordingComputeCommandContext batchingComputeContext;
			RecordingDevice batchingDevice;
			RenderGraph barrierBatchingGraph(
				{
					.m_Device = &batchingDevice,
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
				});
			barrierBatchingGraph.AddPass<BarrierBatchingPassData>(
				"BatchTextureAndBufferBarriers",
				RGPassEncoderType::Compute,
				[](RenderGraph::RGBuilder& builder, BarrierBatchingPassData& data)
				{
					const RHITextureDesc textureDesc =
					{
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Extent = { .m_Width = 4, .m_Height = 4, .m_Depth = 1 },
					};
					const RHIBufferDesc bufferDesc =
					{
						.m_SizeInBytes = 64,
						.m_StrideInBytes = 16,
					};
					data.m_Texture = builder.ImportTexture(
						"BatchingTexture",
						RHITextureHandle{ 1, 1 },
						textureDesc,
						RGTextureAccess::CopyDest);
					data.m_Buffer = builder.ImportBuffer(
						"BatchingBuffer",
						RHIBufferHandle{ 1, 1 },
						bufferDesc,
						RGBufferAccess::CopyDest);
					data.m_Texture = builder.Read(
						data.m_Texture,
						RGTextureAccess::Sample,
						RHIStage::ComputeShader);
					data.m_Buffer = builder.Read(
						data.m_Buffer,
						RGBufferAccess::StructuredRead,
						RHIStage::ComputeShader);
					builder.Export(
						data.m_Texture,
						RGTextureAccess::Sample,
						RHIStage::ComputeShader);
					builder.Export(
						data.m_Buffer,
						RGBufferAccess::StructuredRead,
						RHIStage::ComputeShader);
					builder.SideEffect();
				},
				[](RGExecuteContext&, BarrierBatchingPassData&) {});
			const bool barrierBatchingCompiled = barrierBatchingGraph.Compile();
			context.Check(barrierBatchingCompiled, "RenderGraph barrier batching fixture compiles");
			if (barrierBatchingCompiled)
			{
				RGExecuteContext batchingExecuteContext(
					{
						.m_GraphicsCommandContext = &batchingGraphicsContext,
						.m_DirectComputeCommandContext = &batchingComputeContext,
						.m_AsyncComputeCommandContext = nullptr,
					});
				barrierBatchingGraph.Execute(batchingExecuteContext);
				context.Check(
					batchingComputeContext.m_TextureBarrierCount == 1 &&
						batchingComputeContext.m_BufferBarrierCount == 1 &&
						batchingComputeContext.m_FlushBarrierCount == 1,
					"RenderGraph batches texture and buffer barriers into one explicit flush");
			}
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
