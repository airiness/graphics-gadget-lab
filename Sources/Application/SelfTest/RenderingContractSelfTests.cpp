#include "Core/Precompiled.h"
#include "Application/SelfTest/RenderingContractSelfTests.h"
#include "Core/Math/MathFunctions.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassDepthPrepass.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
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
				m_TextureBarriers.insert(
					m_TextureBarriers.end(),
					barriers.begin(),
					barriers.end());
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
			std::vector<RHITextureBarrier> m_TextureBarriers;
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
				!screen_space::IsDepthBackground(
					1.0f - 5.0e-7f,
					DepthConvention::Standard) &&
				!screen_space::IsDepthBackground(
					5.0e-7f,
					DepthConvention::Reversed),
				"Non-clear depth values near the far plane remain geometry");

			constexpr float PrecisionNearZ = 0.05f;
			constexpr float PrecisionFarZ = 5000.0f;
			constexpr float FarGeometryZ = PrecisionFarZ * 0.95f;
			const Matrix precisionStandardProjection =
				math::CreatePerspectiveFieldOfViewLH(
					FovRadians,
					Aspect,
					PrecisionNearZ,
					PrecisionFarZ);
			const Matrix precisionReversedProjection =
				math::CreatePerspectiveFieldOfViewLHReversedZ(
					FovRadians,
					Aspect,
					PrecisionNearZ,
					PrecisionFarZ);
			const float precisionStandardDepth = ProjectPosition(
				Vector3(0.0f, 0.0f, FarGeometryZ),
				precisionStandardProjection).m_RawDepth;
			const float precisionReversedDepth = ProjectPosition(
				Vector3(0.0f, 0.0f, FarGeometryZ),
				precisionReversedProjection).m_RawDepth;
			context.Check(
				precisionStandardDepth < 1.0f &&
					1.0f - precisionStandardDepth < 1.0e-6f &&
					!screen_space::IsDepthBackground(
						precisionStandardDepth,
						DepthConvention::Standard) &&
					precisionReversedDepth > 0.0f &&
					precisionReversedDepth < 1.0e-6f &&
					!screen_space::IsDepthBackground(
						precisionReversedDepth,
						DepthConvention::Reversed),
				"Geometry at 95 percent of the far range is not classified as background");
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

			GraphicsPhysicalPipelineKey reversedWriteRecipe{};
			reversedWriteRecipe.m_DepthPreset = DepthPreset::ReversedZWrite;
			const auto reversedWrite =
				BuildRHIGraphicsPipelineDesc(reversedWriteRecipe).m_DepthStencil;
			GraphicsPhysicalPipelineKey reversedReadRecipe{};
			reversedReadRecipe.m_DepthPreset = DepthPreset::ReversedZReadOnly;
			const auto reversedRead =
				BuildRHIGraphicsPipelineDesc(reversedReadRecipe).m_DepthStencil;
			GraphicsPhysicalPipelineKey reversedEqualRecipe{};
			reversedEqualRecipe.m_DepthPreset =
				DepthPreset::ReversedZEqualReadOnly;
			const auto reversedEqual =
				BuildRHIGraphicsPipelineDesc(
					reversedEqualRecipe).m_DepthStencil;
			GraphicsPhysicalPipelineKey standardWriteRecipe{};
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
					reversedEqual.m_DepthTestEnable &&
					!reversedEqual.m_DepthWriteEnable &&
					reversedEqual.m_DepthCompareOp ==
						RHICompareOp::Equal &&
					standardWrite.m_DepthTestEnable &&
					standardWrite.m_DepthWriteEnable &&
					standardWrite.m_DepthCompareOp == RHICompareOp::Less,
				"Depth presets encode explicit Reversed-Z and Standard-Z compare contracts");

			struct SampleableDepthPassData
			{
				RGTextureViewId m_View{};
			};
			RecordingDevice recordingDevice;
			RecordingGraphicsCommandContext recordingGraphicsContext;
			constexpr RHITextureHandle graphDepthHandle{ 41, 7 };
			RenderGraph graph({
				.m_Device = &recordingDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			RGTextureId graphDepth;
			RHITextureDesc graphDepthDesc = depthDesc;
			graphDepthDesc.m_Usage = RHITextureUsage::None;
			graph.AddPass<SampleableDepthPassData>(
				"SampleableDepth.Write",
				[&graphDepth, graphDepthDesc, graphDepthHandle, dsvDesc](
					RenderGraph::RGBuilder& builder,
					SampleableDepthPassData& data)
				{
					graphDepth = builder.ImportTexture(
						"DisplayView.DepthBuffer",
						graphDepthHandle,
						graphDepthDesc,
						RGTextureAccess::None);
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
				const RHIResourceState commonState = CommonRHIResourceState();
				const RHIResourceState depthWriteState =
					ToRHIResourceState(RGTextureAccess::DepthStencilWrite);
				const RHIResourceState pixelSampleState =
					ToRHIResourceState(
						RGTextureAccess::Sample,
						RHIStage::PixelShader);
				const auto* executionPlan = graph.GetExecutionPlan();
				const bool hasExactPlannerContract =
					executionPlan &&
					executionPlan->GetResources().size() == 1 &&
					executionPlan->GetPasses().size() == 2 &&
					executionPlan->GetPasses()[0].m_PreBarriers.size() == 1 &&
					executionPlan->GetPasses()[0].m_PostBarriers.empty() &&
					executionPlan->GetPasses()[1].m_PreBarriers.size() == 1 &&
					executionPlan->GetPasses()[1].m_PostBarriers.size() == 1;
				bool plannerBarrierFieldsMatch = hasExactPlannerContract;
				if (hasExactPlannerContract)
				{
					const auto& writeBarrier =
						executionPlan->GetPasses()[0].m_PreBarriers[0];
					const auto& sampleBarrier =
						executionPlan->GetPasses()[1].m_PreBarriers[0];
					const auto& finalBarrier =
						executionPlan->GetPasses()[1].m_PostBarriers[0];
					const auto isFullTextureTransition =
						[](const RGBarrierIntent& barrier) noexcept
						{
							return barrier.m_Resource.Value() == 0 &&
								barrier.m_Kind == RGBarrierKind::Transition &&
								!barrier.m_Subresources.has_value();
						};
					plannerBarrierFieldsMatch =
						isFullTextureTransition(writeBarrier) &&
						writeBarrier.m_Reason ==
							RGBarrierReason::AccessTransition &&
						writeBarrier.m_Before == commonState &&
						writeBarrier.m_After == depthWriteState &&
						isFullTextureTransition(sampleBarrier) &&
						sampleBarrier.m_Reason ==
							RGBarrierReason::AccessTransition &&
						sampleBarrier.m_Before == depthWriteState &&
						sampleBarrier.m_After == pixelSampleState &&
						isFullTextureTransition(finalBarrier) &&
						finalBarrier.m_Reason ==
							RGBarrierReason::FinalStateTransition &&
						finalBarrier.m_Before == pixelSampleState &&
						finalBarrier.m_After == commonState;
				}
				context.Check(
					plannerBarrierFieldsMatch,
					"RenderGraph plans the exact Common-to-DSW-to-pixel-SRV-to-Common depth contract");

				const bool dependencyMatches = executionPlan &&
					std::ranges::any_of(
						executionPlan->GetDependencyEdges(),
						[](const RGPassDependencyEdge& edge) noexcept
						{
							return edge.m_From.Value() == 0 &&
								edge.m_To.Value() == 1 &&
								edge.m_Reason ==
									RGDependencyReason::WriterToReader;
						});
				context.Check(
					dependencyMatches,
					"Sampleable depth preserves its writer-to-reader dependency");

				bool compiledViewsMatch = executionPlan &&
					executionPlan->GetTextureViews().size() == 2;
				if (compiledViewsMatch)
				{
					const auto& compiledDsv =
						executionPlan->GetTextureViews()[0].m_Desc;
					const auto& compiledSrv =
						executionPlan->GetTextureViews()[1].m_Desc;
					compiledViewsMatch =
						compiledDsv.m_Type ==
							RHITextureViewType::DepthStencil &&
						compiledDsv.m_Dimension ==
							RHITextureViewDimension::Texture2D &&
						compiledDsv.m_Format == RHIFormat::D32Float &&
						compiledDsv.m_Subresources == dsvDesc.m_Subresources &&
						compiledSrv.m_Type ==
							RHITextureViewType::ShaderResource &&
						compiledSrv.m_Dimension ==
							RHITextureViewDimension::Texture2D &&
						compiledSrv.m_Format == RHIFormat::R32Float &&
						compiledSrv.m_Subresources == srvDesc.m_Subresources;
				}
				context.Check(
					compiledViewsMatch,
					"Sampleable depth compiles canonical D32 DSV and R32 SRV views of one resource");

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
						snapshot.m_Passes[0].m_PreBarriers.size() == 1 &&
						snapshot.m_Passes[1].m_PreBarriers.size() == 1 &&
						snapshot.m_Passes[1].m_PostBarriers.size() == 1,
					"RenderGraph snapshot publishes the inferred sampleable-depth contract");

				RGExecuteContext executeContext({
					.m_GraphicsCommandContext = &recordingGraphicsContext,
				});
				graph.Execute(executeContext);
				const auto isFullResourceBarrier =
					[](const RHITextureBarrier& barrier) noexcept
					{
						return !barrier.m_Subresources.has_value();
					};
				const bool loweredBarriersMatch =
					recordingGraphicsContext.m_TextureBarriers.size() == 3 &&
					recordingGraphicsContext.m_TextureBarriers[0].m_Texture ==
						graphDepthHandle &&
					isFullResourceBarrier(
						recordingGraphicsContext.m_TextureBarriers[0]) &&
					recordingGraphicsContext.m_TextureBarriers[0].m_Before ==
						commonState &&
					recordingGraphicsContext.m_TextureBarriers[0].m_After ==
						depthWriteState &&
					recordingGraphicsContext.m_TextureBarriers[1].m_Texture ==
						graphDepthHandle &&
					isFullResourceBarrier(
						recordingGraphicsContext.m_TextureBarriers[1]) &&
					recordingGraphicsContext.m_TextureBarriers[1].m_Before ==
						depthWriteState &&
					recordingGraphicsContext.m_TextureBarriers[1].m_After ==
						pixelSampleState &&
					recordingGraphicsContext.m_TextureBarriers[2].m_Texture ==
						graphDepthHandle &&
					isFullResourceBarrier(
						recordingGraphicsContext.m_TextureBarriers[2]) &&
					recordingGraphicsContext.m_TextureBarriers[2].m_Before ==
						pixelSampleState &&
					recordingGraphicsContext.m_TextureBarriers[2].m_After ==
						commonState;
				context.Check(
					loweredBarriersMatch,
					"RenderGraph executor lowers exactly three resource-specific depth barriers");

				D3D12_RESOURCE_DESC nativeResourceDesc{};
				nativeResourceDesc.Dimension =
					D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				nativeResourceDesc.Width = 1280;
				nativeResourceDesc.Height = 720;
				nativeResourceDesc.DepthOrArraySize = 1;
				nativeResourceDesc.MipLevels = 1;
				nativeResourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				nativeResourceDesc.SampleDesc = { 1, 0 };
				auto* nativeResource =
					reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 });
				bool nativeBarrierMatches = false;
				if (recordingGraphicsContext.m_TextureBarriers.size() == 3)
				{
					const D3D12_TEXTURE_BARRIER nativeBarrier =
						BuildD3D12TextureBarrier(
							recordingGraphicsContext.m_TextureBarriers[1],
							nativeResource,
							nativeResourceDesc);
					nativeBarrierMatches =
						nativeBarrier.SyncBefore ==
							D3D12_BARRIER_SYNC_DEPTH_STENCIL &&
						nativeBarrier.SyncAfter ==
							D3D12_BARRIER_SYNC_PIXEL_SHADING &&
						nativeBarrier.AccessBefore ==
							D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE &&
						nativeBarrier.AccessAfter ==
							D3D12_BARRIER_ACCESS_SHADER_RESOURCE &&
						nativeBarrier.LayoutBefore ==
							D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE &&
						nativeBarrier.LayoutAfter ==
							D3D12_BARRIER_LAYOUT_SHADER_RESOURCE &&
						nativeBarrier.pResource == nativeResource &&
						nativeBarrier.Subresources.IndexOrFirstMipLevel ==
							D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
						nativeBarrier.Flags ==
							D3D12_TEXTURE_BARRIER_FLAG_NONE;
				}
				context.Check(
					nativeBarrierMatches,
					"DX12 lowers the depth transition to the exact Enhanced Barrier contract");
			}

			struct DepthAccessChainPassData {};
			RenderGraph accessChainGraph({
				.m_Device = &recordingDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(
						uintptr_t{ 1 }),
			});
			RGTextureId accessChainDepth;
			accessChainGraph.AddPass<DepthAccessChainPassData>(
				"DepthAccessChain.Prepass",
				[&accessChainDepth,
					graphDepthDesc,
					graphDepthHandle](
					RenderGraph::RGBuilder& builder,
					DepthAccessChainPassData&)
				{
					accessChainDepth = builder.ImportTexture(
						"DepthAccessChain.Depth",
						graphDepthHandle,
						graphDepthDesc,
						RGTextureAccess::None);
					builder.WriteInPlace(
						accessChainDepth,
						RGTextureAccess::DepthStencilWrite);
				});
			accessChainGraph.AddPass<DepthAccessChainPassData>(
				"DepthAccessChain.Sample",
				[&accessChainDepth](
					RenderGraph::RGBuilder& builder,
					DepthAccessChainPassData&)
				{
					accessChainDepth = builder.Read(
						accessChainDepth,
						RGTextureAccess::Sample,
						RHIStage::ComputeShader);
					builder.SideEffect();
				});
			accessChainGraph.AddPass<DepthAccessChainPassData>(
				"DepthAccessChain.Forward",
				[&accessChainDepth](
					RenderGraph::RGBuilder& builder,
					DepthAccessChainPassData&)
				{
					accessChainDepth = builder.Read(
						accessChainDepth,
						RGTextureAccess::DepthStencilRead);
					builder.SideEffect();
				});

			const bool accessChainCompiled =
				accessChainGraph.Compile();
			bool accessChainMatches = accessChainCompiled;
			if (accessChainCompiled)
			{
				const auto* plan =
					accessChainGraph.GetExecutionPlan();
				const RHIResourceState writeState =
					ToRHIResourceState(
						RGTextureAccess::DepthStencilWrite);
				const RHIResourceState sampleState =
					ToRHIResourceState(
						RGTextureAccess::Sample,
						RHIStage::ComputeShader);
				const RHIResourceState readState =
					ToRHIResourceState(
						RGTextureAccess::DepthStencilRead);
				accessChainMatches =
					plan &&
					plan->GetPasses().size() == 3 &&
					plan->GetPasses()[0].m_PreBarriers.size() ==
						1 &&
					plan->GetPasses()[1].m_PreBarriers.size() ==
						1 &&
					plan->GetPasses()[2].m_PreBarriers.size() ==
						1 &&
					plan->GetPasses()[2].m_PostBarriers.size() ==
						1;
				if (accessChainMatches)
				{
					const auto& sampleBarrier =
						plan->GetPasses()[1].
							m_PreBarriers.front();
					const auto& readBarrier =
						plan->GetPasses()[2].
							m_PreBarriers.front();
					accessChainMatches =
						sampleBarrier.m_Before == writeState &&
						sampleBarrier.m_After == sampleState &&
						readBarrier.m_Before == sampleState &&
						readBarrier.m_After == readState;
				}
			}
			context.Check(
				accessChainMatches,
				"RenderGraph preserves the depth-write to sample to read-only-depth access chain");
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

			desc.m_SourcePath =
				L"Passes/PassDepthPrepass.hlsl";
			desc.m_Stage = ShaderStage::Vertex;
			desc.m_Entry = L"VSMain";
			const ShaderCompileArtifact depthVertexArtifact =
				compiler.CompileOrLoadArtifact(
					compiler.NormalizeShaderDesc(desc));
			desc.m_Stage = ShaderStage::Pixel;
			desc.m_Entry = L"PSAlphaTest";
			const ShaderCompileArtifact depthAlphaArtifact =
				compiler.CompileOrLoadArtifact(
					compiler.NormalizeShaderDesc(desc));
			context.Check(
				depthVertexArtifact.m_Binary.IsValid() &&
					depthAlphaArtifact.m_Binary.IsValid(),
				"Production DXC compiles depth-only and alpha-tested prepass variants");
		}

		void RunDepthCoverageContractTests(SelfTestContext& context) noexcept
		{
			const auto makeVariantBits =
				[](RenderBucket bucket, bool doubleSided) noexcept
				{
					uint64_t bits =
						static_cast<uint64_t>(bucket) <<
						RenderQueueBuilder::VariantBit::BucketShift;
					if (doubleSided)
					{
						bits |=
							RenderQueueBuilder::VariantBit::DoubleSided;
					}
					return bits;
				};

			GraphicsPhysicalPipelineKey basePhysicalKey{};
			basePhysicalKey.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			basePhysicalKey.m_TopologyType =
				RHIPrimitiveTopologyType::Triangle;
			basePhysicalKey.m_PrimitiveTopology =
				RHIPrimitiveTopology::TriangleList;
			basePhysicalKey.m_Formats.m_SampleCount = 1;
			basePhysicalKey.m_Formats.m_SampleQuality = 0;
			basePhysicalKey.m_RasterizerPreset = RasterizerPreset::Default;
			basePhysicalKey.m_DepthPreset = DepthPreset::ReversedZWrite;
			basePhysicalKey.m_BlendPreset = BlendPreset::Default;

			const auto opaque =
				RenderPassForwardPBR::
					BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey,
					makeVariantBits(RenderBucket::Opaque, false));
			const auto opaqueRepeat =
				RenderPassForwardPBR::
					BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey,
					makeVariantBits(RenderBucket::Opaque, false));
			const auto alphaTest =
				RenderPassForwardPBR::
					BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey,
					makeVariantBits(RenderBucket::AlphaTest, false));
			const auto doubleSided =
				RenderPassForwardPBR::
					BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey,
					makeVariantBits(RenderBucket::Opaque, true));
			const auto transparent =
				RenderPassForwardPBR::
					BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey,
					makeVariantBits(RenderBucket::Transparent, false));
			GraphicsPhysicalPipelineKey prepassPhysicalKey =
				basePhysicalKey;
			prepassPhysicalKey.m_Formats.m_RenderTargetCount = 0;
			prepassPhysicalKey.m_BlendPreset =
				BlendPreset::ColorWriteDisable;
			const auto prepassOpaque =
				RenderPassDepthPrepass::
					BuildDepthCoveragePipelineSignatureForVariant(
						prepassPhysicalKey,
						makeVariantBits(
							RenderBucket::Opaque,
							false));
			const auto prepassAlpha =
				RenderPassDepthPrepass::
					BuildDepthCoveragePipelineSignatureForVariant(
						prepassPhysicalKey,
						makeVariantBits(
							RenderBucket::AlphaTest,
							false));
			context.Check(
				opaque && opaqueRepeat && alphaTest && doubleSided &&
					prepassOpaque && prepassAlpha &&
					*opaque == *opaqueRepeat &&
					*opaque == *prepassOpaque &&
					*alphaTest == *prepassAlpha &&
					!transparent,
				"Prepass and Forward variants generate matching stable coverage signatures");
			if (!opaque || !alphaTest || !doubleSided)
			{
				return;
			}

			const uint64_t opaqueVariant =
				makeVariantBits(RenderBucket::Opaque, false);
			const GraphicsLogicalPipelineMetadata
				forwardMetadata =
					RenderPassForwardPBR::
						BuildLogicalPipelineMetadataForVariant(
							basePhysicalKey,
							opaqueVariant);
			const GraphicsLogicalPipelineMetadata
				forwardMetadataRepeat =
					RenderPassForwardPBR::
						BuildLogicalPipelineMetadataForVariant(
							basePhysicalKey,
							opaqueVariant);
			const GraphicsLogicalPipelineMetadata
				prepassMetadata =
					RenderPassDepthPrepass::
						BuildLogicalPipelineMetadataForVariant(
							prepassPhysicalKey,
							opaqueVariant);
			context.Check(
				forwardMetadata == forwardMetadataRepeat &&
					forwardMetadata == prepassMetadata,
				"Logical coverage metadata is deterministic before any physical pipeline is resolved");

			DepthCoveragePipelineSignature alphaNormalized = *alphaTest;
			alphaNormalized.m_AlphaVariant =
				DepthCoverageAlphaVariant::Opaque;
			DepthCoveragePipelineSignature doubleSidedNormalized =
				*doubleSided;
			doubleSidedNormalized.m_CullMode = opaque->m_CullMode;
			doubleSidedNormalized.m_DoubleSided = false;
			context.Check(
				alphaNormalized == *opaque &&
					doubleSidedNormalized == *opaque,
				"Coverage variants differ only in their declared pipeline fields");

			const auto differsAfter =
				[&opaque](auto mutate) noexcept
				{
					DepthCoveragePipelineSignature changed = *opaque;
					mutate(changed);
					return changed != *opaque;
				};
			const bool pipelineFieldsParticipate =
				differsAfter([](auto& value)
					{
						value.m_VertexProgram =
							DepthCoverageVertexProgram::SkinnedMesh;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_Deformation =
							DepthCoverageDeformationVariant::Skinned;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_InputLayout = InputLayoutID::P3C4;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_PositionPrecision =
							DepthCoveragePositionPrecision::Float16;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_PositionFormat =
							RHIFormat::R32G32B32A32Float;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_TopologyType =
							RHIPrimitiveTopologyType::Line;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_PrimitiveTopology =
							RHIPrimitiveTopology::LineList;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_FillMode = RHIFillMode::Wireframe;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_CullMode = RHICullMode::Front;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_FrontCounterClockwise = true;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_DepthClipEnable = false;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_DoubleSided = true;
					}) &&
				differsAfter([](auto& value)
					{
						++value.m_DepthBias;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_DepthBiasClamp = 1.0f;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_SlopeScaledDepthBias = 1.0f;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_SampleCount = 4;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_SampleQuality = 1;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_SampleMask = 0x7FFFFFFFu;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_AlphaToCoverageEnable = true;
					}) &&
				differsAfter([](auto& value)
					{
						value.m_AlphaVariant =
							DepthCoverageAlphaVariant::BaseColorMask;
					});
			context.Check(
				pipelineFieldsParticipate,
				"Every coverage pipeline field participates in signature identity");

			GraphicsPhysicalPipelineKey nonCoveragePhysicalKey =
				basePhysicalKey;
			nonCoveragePhysicalKey.m_DepthPreset =
				DepthPreset::ReversedZReadOnly;
			nonCoveragePhysicalKey.m_BlendPreset = BlendPreset::AlphaBlend;
			nonCoveragePhysicalKey.m_Formats.m_RenderTargetFormats[0] =
				RHIFormat::R8G8B8A8Unorm;
			const auto nonCoverageSignature =
				RenderPassForwardPBR::
					BuildDepthCoveragePipelineSignatureForVariant(
					nonCoveragePhysicalKey,
					makeVariantBits(RenderBucket::Opaque, false));
			context.Check(
				nonCoverageSignature &&
					*nonCoverageSignature == *opaque,
				"Depth, blend, and render-target state do not change coverage identity");

			const GraphicsLogicalPipelineMetadata opaqueMetadata{
				.m_DepthCoveragePipelineSignature = *opaque,
			};
			const GraphicsLogicalPipelineMetadata alphaMetadata{
				.m_DepthCoveragePipelineSignature = *alphaTest,
			};
			const GraphicsPhysicalPipelineKey opaquePhysicalKey =
				basePhysicalKey;
			const GraphicsPhysicalPipelineKey alphaPhysicalKey =
				basePhysicalKey;
			context.Check(
				opaquePhysicalKey == alphaPhysicalKey &&
					opaqueMetadata != alphaMetadata,
				"Logical coverage metadata changes without changing the physical pipeline key");

			const DepthCoverageRasterDomain rasterDomain{
				.m_FrameSerial = 23,
				.m_ViewBindingId = 1,
				.m_CurrentViewSource = {
					.m_Buffer = RHIBufferHandle{ 4, 5 },
					.m_ElementIndex = 29,
				},
				.m_CurrentJitteredProjectionSource = {
					.m_Buffer = RHIBufferHandle{ 4, 5 },
					.m_ElementIndex = 29,
				},
				.m_ProjectionSource =
					DepthCoverageProjectionSource::ViewDataProjection,
				.m_TargetWidth = 1280,
				.m_TargetHeight = 720,
				.m_Viewport = {
					.m_Width = 1280.0f,
					.m_Height = 720.0f,
				},
				.m_Scissor = {
					.m_Right = 1280,
					.m_Bottom = 720,
				},
				.m_DepthConvention = DepthConvention::Reversed,
			};
			const auto rasterDomainDiffersAfter =
				[&rasterDomain](auto mutate) noexcept
				{
					DepthCoverageRasterDomain changed = rasterDomain;
					mutate(changed);
					return changed != rasterDomain;
				};
			context.Check(
				rasterDomain.IsValid() &&
					CompareDepthCoverageRasterDomains(
						rasterDomain,
						rasterDomain).m_Matches &&
					rasterDomainDiffersAfter([](auto& value)
						{
							++value.m_FrameSerial;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							++value.m_ViewBindingId;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_CurrentViewSource.m_Buffer =
								RHIBufferHandle{ 4, 6 };
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							++value.m_CurrentJitteredProjectionSource.m_ElementIndex;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_ProjectionSource =
								DepthCoverageProjectionSource::
									DedicatedJitteredProjection;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							++value.m_TargetWidth;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							++value.m_TargetHeight;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Viewport.m_X = 1.0f;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Viewport.m_Y = 1.0f;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Viewport.m_Width = 1920.0f;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Viewport.m_Height = 1080.0f;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Viewport.m_MinDepth = 0.25f;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Viewport.m_MaxDepth = 0.75f;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Scissor.m_Left = 1;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Scissor.m_Top = 1;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Scissor.m_Right = 1920;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_Scissor.m_Bottom = 1080;
						}) &&
					rasterDomainDiffersAfter([](auto& value)
						{
							value.m_DepthConvention =
								DepthConvention::Standard;
						}),
				"Raster-domain identity covers the complete view and raster state");

			DepthCoverageRasterDomain undersizedDomain =
				rasterDomain;
			undersizedDomain.m_TargetWidth = 1279;
			DepthCoverageRasterDomain oversizedViewportDomain =
				rasterDomain;
			oversizedViewportDomain.m_Viewport.m_Width =
				1281.0f;
			const RHITextureDesc coverageColorDesc{
				.m_Format = RHIFormat::R16G16B16A16Float,
				.m_Extent = { 1280, 720, 1 },
			};
			const RHITextureDesc coverageDepthDesc{
				.m_Format = RHIFormat::R32Typeless,
				.m_Extent = { 1280, 720, 1 },
			};
			RHITextureDesc mismatchedCoverageDepthDesc =
				coverageDepthDesc;
			mismatchedCoverageDepthDesc.m_Extent.m_Height =
				719;
			context.Check(
				rasterDomain.MatchesTargetExtent(1280, 720) &&
					!rasterDomain.MatchesTargetExtent(1281, 720) &&
					!undersizedDomain.IsValid() &&
					!oversizedViewportDomain.IsValid() &&
					AreDepthCoverageTargetExtentsCompatible(
						rasterDomain,
						coverageColorDesc,
						coverageDepthDesc) &&
					!AreDepthCoverageTargetExtentsCompatible(
						rasterDomain,
						coverageColorDesc,
						mismatchedCoverageDepthDesc),
				"Raster domains and target descriptors reject color-depth extent mismatches");

			DepthCoverageRasterDomain changedRasterDomain = rasterDomain;
			++changedRasterDomain.m_FrameSerial;
			changedRasterDomain.m_TargetWidth = 1920;
			changedRasterDomain.m_Viewport.m_Width = 1920.0f;
			const auto pipelineComparison =
				CompareDepthCoveragePipelineSignatures(*opaque, *alphaTest);
			const auto rasterComparison =
				CompareDepthCoverageRasterDomains(
					rasterDomain,
					changedRasterDomain);
			context.Check(
				!pipelineComparison.m_Matches &&
					pipelineComparison.m_Mismatch.find("AlphaVariant") !=
						std::string::npos &&
					!rasterComparison.m_Matches &&
					rasterComparison.m_Mismatch.find("FrameSerial") !=
						std::string::npos &&
					rasterComparison.m_Mismatch.find("Viewport") !=
						std::string::npos &&
					DescribeDepthCoveragePipelineSignature(*opaque).find(
						"InputLayout") != std::string::npos &&
					DescribeDepthCoverageRasterDomain(rasterDomain).find(
						"Scissor") != std::string::npos,
				"Coverage diagnostics identify pipeline and raster-domain mismatches");

			const auto invalidPipelineComparison =
				CompareDepthCoveragePipelineSignatures(
					DepthCoveragePipelineSignature{},
					DepthCoveragePipelineSignature{});
			DepthCoverageRasterDomain invalidRhsDomain = rasterDomain;
			invalidRhsDomain.m_FrameSerial = 0;
			const auto invalidRasterComparison =
				CompareDepthCoverageRasterDomains(
					rasterDomain,
					invalidRhsDomain);
			context.Check(
				!invalidPipelineComparison.m_Matches &&
					invalidPipelineComparison.m_Mismatch.find(
						"Pipeline.LhsInvalid") != std::string::npos &&
					!invalidRasterComparison.m_Matches &&
					invalidRasterComparison.m_Mismatch.find(
						"RasterDomain.RhsInvalid") != std::string::npos,
				"Coverage comparison fails closed for invalid contract domains");

			const DepthCoverageDrawPacket packet{
				.m_Geometry = {
					.m_MeshId = ProceduralCubeMeshID,
					.m_VertexBuffer = {
						.m_Buffer = RHIBufferHandle{ 8, 9 },
						.m_Offset = 64,
						.m_Stride = 48,
						.m_SizeInBytes = 480,
					},
					.m_IndexBuffer = {
						.m_Buffer = RHIBufferHandle{ 10, 11 },
						.m_Offset = 32,
						.m_SizeInBytes = 72,
						.m_Format = RHIFormat::R32Uint,
					},
				},
				.m_IndexedDraw = {
					.m_IndexCount = 18,
					.m_InstanceCount = 2,
					.m_StartIndexLocation = 3,
					.m_BaseVertexLocation = -2,
					.m_StartInstanceLocation = 4,
				},
				.m_DrawParameters = {
					.ObjectOffset = 17,
				},
				.m_CurrentModelSource = {
					.m_Buffer = RHIBufferHandle{ 2, 3 },
					.m_ElementIndex = 17,
				},
				.m_MaterialAlphaSource = {
					.m_Buffer = RHIBufferHandle{ 6, 7 },
					.m_ElementIndex = 31,
				},
			};
			const auto packetDiffersAfter =
				[&packet](auto mutate) noexcept
				{
					DepthCoverageDrawPacket changed = packet;
					mutate(changed);
					return changed != packet;
				};
			RenderQueue queue{
				.m_DrawItems = {
					DrawItem{ .m_CoverageDrawPacket = packet },
				},
			};
			const auto& prepassPacket =
				queue.m_DrawItems.front().m_CoverageDrawPacket;
			const auto& forwardPacket =
				queue.m_DrawItems.front().m_CoverageDrawPacket;
			const DepthCoverageDrawPacket copiedPacket = packet;
			context.Check(
				packet.IsValid() &&
					IsSameDepthCoverageDrawPacket(
						prepassPacket,
						forwardPacket) &&
					!IsSameDepthCoverageDrawPacket(packet, copiedPacket) &&
					packetDiffersAfter([](auto& value)
						{
							value.m_Geometry.m_MeshId =
								ProceduralSphereMeshID;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							value.m_Geometry.m_VertexBuffer.m_Buffer =
								RHIBufferHandle{ 8, 10 };
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_Geometry.m_VertexBuffer.m_Offset;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_Geometry.m_VertexBuffer.m_Stride;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_Geometry.m_VertexBuffer.m_SizeInBytes;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							value.m_Geometry.m_IndexBuffer.m_Buffer =
								RHIBufferHandle{ 10, 12 };
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_Geometry.m_IndexBuffer.m_Offset;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_Geometry.m_IndexBuffer.m_SizeInBytes;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							value.m_Geometry.m_IndexBuffer.m_Format =
								RHIFormat::R32Float;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_IndexedDraw.m_IndexCount;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_IndexedDraw.m_InstanceCount;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_IndexedDraw.m_StartIndexLocation;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_IndexedDraw.m_BaseVertexLocation;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_IndexedDraw.m_StartInstanceLocation;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_DrawParameters.ObjectOffset;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							value.m_CurrentModelSource.m_Buffer =
								RHIBufferHandle{ 2, 4 };
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_CurrentModelSource.m_ElementIndex;
						}) &&
					packetDiffersAfter([](auto& value)
						{
							value.m_MaterialAlphaSource.m_Buffer =
								RHIBufferHandle{ 6, 8 };
						}) &&
					packetDiffersAfter([](auto& value)
						{
							++value.m_MaterialAlphaSource.m_ElementIndex;
						}) &&
					DescribeDepthCoverageDrawPacket(packet).find(
						"DrawIndexed") != std::string::npos &&
					DescribeDepthCoverageDrawPacket(packet).find(
						"MaterialAlpha") != std::string::npos,
				"RenderQueue owns one complete coverage draw packet shared by both passes");
		}

		void RunScreenSpaceAndDepthContractTests(SelfTestContext& context) noexcept
		{
			RunProjectionConventionTests(context);
			RunPositionReconstructionTests(context);
			RunScreenCoordinateTests(context);
			RunRenderViewConventionTests(context);
			RunSampleableDepthFormatTests(context);
			RunDepthCoverageContractTests(context);
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
