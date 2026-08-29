#include "RenderingContractSelfTests.h"
#include "Core/Math/MathFunctions.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/CameraRig.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/Pipeline/ForwardPlus.h"
#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/Pipeline/TemporalAA.h"
#include "Graphics/Pipeline/TemporalAACapability.h"
#include "Graphics/Pipeline/TemporalFrameTransaction.h"
#include "Graphics/Pipeline/TemporalMotion.h"
#include "Graphics/PostProcess/PostProcessColor.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/RenderPassDepthPrepass.h"
#include "Graphics/RenderPass/RenderPassForwardOpaque.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"
#include "Graphics/RenderPass/TemporalGeometryGraphResources.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "Graphics/Resource/TransientResourcePool.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12PipelineDescUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12TextureSupportUtils.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/Utility/DXGIFormatUtils.h"
#include "Graphics/RenderView.h"
#include "Graphics/RenderPipeline/DepthCoverageFramePlan.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"
#include "Graphics/TransferBatch.h"

#include <array>
#include <filesystem>
#include <limits>
#include <type_traits>

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

		struct GTAODataflowPassData
		{
			std::array<RGTextureViewId, 3> m_Views{};
		};

		struct TemporalHistoryContractPassData
		{
			TemporalHistoryRenderGraphResources m_History;
		};

		struct BarrierBatchingPassData
		{
			RGTextureId m_Texture;
			RGBufferId m_Buffer;
		};

		struct OpaqueSceneExtensionFixturePassData
		{
			RGBufferId m_ScenePhaseBuffer;
		};

		struct OverlayExtensionFixturePassData
		{
		};

		template <typename T>
		concept HasPublicPresent = requires(T & value) { value.Present(); };

		template <typename T>
		concept HasPublicFrameCompletionWait = requires(T & value) { value.WaitFrameCompletion(); };

		template <typename T>
		concept HasPublicCurrentBackBufferIndex =
			requires(const T & value) { value.GetCurrentBackBufferIndex(); };

		class RecordingOpaqueSceneExtension final : public RenderPipelineSceneExtensionBase
		{
		public:
			RecordingOpaqueSceneExtension(RGBufferId& scenePhaseBuffer,
				uint32_t& addPassCount, uint32_t& destructionCount) noexcept :
				m_ScenePhaseBuffer(scenePhaseBuffer), m_AddPassCount(addPassCount),
				m_DestructionCount(destructionCount)
			{
			}
			~RecordingOpaqueSceneExtension() override { ++m_DestructionCount; }

			void AddOpaqueScenePasses(RenderGraph& renderGraph,
				const RenderFrameContext&, const RenderServices&) noexcept override
			{
				++m_AddPassCount;
				renderGraph.AddPass<OpaqueSceneExtensionFixturePassData>(
					"RenderingContract.OpaqueSceneExtension",
					[this](RenderGraph::RGBuilder& builder,
						OpaqueSceneExtensionFixturePassData& data)
					{
						builder.ReadWriteInPlace(
							m_ScenePhaseBuffer, RGBufferAccess::StorageReadWrite);
						data.m_ScenePhaseBuffer = m_ScenePhaseBuffer;
					});
			}

		private:
			RGBufferId& m_ScenePhaseBuffer;
			uint32_t& m_AddPassCount;
			uint32_t& m_DestructionCount;
		};

		class RecordingOverlayExtension final : public RenderPipelineOverlayExtensionBase
		{
		public:
			explicit RecordingOverlayExtension(uint32_t& addPassCount) noexcept :
				m_AddPassCount(addPassCount)
			{
			}

			void AddOverlayPasses(RenderGraph& renderGraph,
				const RenderFrameContext&, const RenderServices&) noexcept override
			{
				++m_AddPassCount;
				renderGraph.AddPass<OverlayExtensionFixturePassData>(
					"RenderingContract.OverlayExtension",
					[](RenderGraph::RGBuilder& builder, OverlayExtensionFixturePassData&)
					{
						builder.SideEffect();
					});
			}

		private:
			uint32_t& m_AddPassCount;
		};

		class IntegratedTemporalSceneExtension final : public RenderPipelineSceneExtensionBase
		{
		public:
			SceneExtensionTemporalParticipation GetTemporalParticipation() const noexcept override
			{
				return SceneExtensionTemporalParticipation::TemporalIntegrated;
			}

			void AddOpaqueScenePasses(
				RenderGraph&, const RenderFrameContext&, const RenderServices&) noexcept override
			{
			}
		};

		class RecordingDevice final : public RHIDevice
		{
		public:
			RHIBackendType GetBackendType() const noexcept override { return {}; }
			std::string_view GetAdapterCompatibilityIdentity() const noexcept override
			{
				return "RenderingContract.RecordingDevice";
			}
			RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override
			{
				return {
					.m_Supported = true,
					.m_MinLaneCount = 32,
					.m_MaxLaneCount = 64,
				};
			}
			RHITextureSupportResult QueryTextureSupport(
				const RHITextureDesc&) const noexcept override
			{
				return {};
			}
			RHITextureSupportResult QueryTextureViewSupport(
				const RHITextureDesc& textureDesc,
				const RHITextureViewDesc& viewDesc) const noexcept override
			{
				m_LastTextureViewQueryTextureDesc = textureDesc;
				m_LastTextureViewQueryDesc = viewDesc;
				++m_TextureViewQueryCount;
				return { .m_Supported = m_TextureViewsSupported };
			}
			RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&,
				const RHIResourceDebugIdentityDesc&) noexcept override
			{
				++m_CreateTextureCount;
				return RHITextureHandle{ m_NextTextureIndex++, 1 };
			}
			RHIBufferHandle CreateBuffer(
				const RHIBufferDesc&, const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return {};
			}
			RHITextureViewHandle CreateTextureView(
				RHITextureHandle, const RHITextureViewDesc&) noexcept override
			{
				return {};
			}
			RHIBufferViewHandle CreateBufferView(
				RHIBufferHandle, const RHIBufferViewDesc&) noexcept override
			{
				return {};
			}
			RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override { return {}; }
			void DestroyTexture(RHITextureHandle) noexcept override
			{
				++m_DestroyTextureCount;
			}
			void DestroyBuffer(RHIBufferHandle) noexcept override {}
			void DestroyTextureView(RHITextureViewHandle) noexcept override {}
			void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
			void DestroySampler(RHISamplerHandle) noexcept override {}
			void SetTextureDebugBinding(
				RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			void SetBufferDebugBinding(
				RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			std::string_view GetTextureDebugName(RHITextureHandle) const noexcept override
			{
				return {};
			}
			std::string_view GetBufferDebugName(RHIBufferHandle) const noexcept override
			{
				return {};
			}
			void* MapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override
			{
				return nullptr;
			}
			void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override {}
			uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override { return 1; }
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
			bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept override
			{
				return !m_UseControlledFenceCompletion ||
					(fencePoint.IsValid() && fencePoint.m_Value <= m_CompletedFenceValue);
			}
			void RecordTextureUse(
				RHITextureHandle, const RHIFencePoint& fencePoint) noexcept override
			{
				++m_RecordTextureUseCount;
				m_LastRecordedFencePoint = fencePoint;
			}
			void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
			RHIDescriptorHandle GetTextureViewDescriptor(
				RHITextureViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle) const noexcept override
			{
				return {};
			}
			void RetireCompletedWork() noexcept override {}

			bool m_TextureViewsSupported = false;
			mutable RHITextureDesc m_LastTextureViewQueryTextureDesc{};
			mutable RHITextureViewDesc m_LastTextureViewQueryDesc{};
			mutable uint32_t m_TextureViewQueryCount = 0;
			bool m_UseControlledFenceCompletion = false;
			uint64_t m_CompletedFenceValue = 0;
			uint32_t m_CreateTextureCount = 0;
			uint32_t m_DestroyTextureCount = 0;
			uint32_t m_RecordTextureUseCount = 0;
			RHIFencePoint m_LastRecordedFencePoint{};

		private:
			uint32_t m_NextTextureIndex = 1;
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
				GGLAB_ASSERT(!m_IsRendering);
				m_TextureBarrierCount += static_cast<uint32_t>(barriers.size());
				m_TextureBarriers.insert(m_TextureBarriers.end(), barriers.begin(), barriers.end());
			}
			void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
			{
				GGLAB_ASSERT(!m_IsRendering);
				m_BufferBarrierCount += static_cast<uint32_t>(barriers.size());
			}
			void FlushBarriers() noexcept override
			{
				GGLAB_ASSERT(!m_IsRendering);
				++m_FlushBarrierCount;
			}
			void CopyBuffer(
				RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override
			{
				GGLAB_ASSERT(!m_IsRendering);
				++m_CopyBufferCount;
			}
			void BeginGpuProfileScope(std::string_view) noexcept override { ++m_BeginProfileCount; }
			void EndGpuProfileScope() noexcept override { ++m_EndProfileCount; }
			void SetPipeline(RHIPipelineHandle) noexcept override {}
			void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
			void BeginRendering(const RHIRenderingInfo&) noexcept override
			{
				GGLAB_ASSERT(!m_IsRendering);
				m_IsRendering = true;
				++m_BeginRenderingCount;
			}
			void EndRendering() noexcept override
			{
				GGLAB_ASSERT(m_IsRendering);
				m_IsRendering = false;
				++m_EndRenderingCount;
			}
			bool IsRendering() const noexcept override { return m_IsRendering; }
			void ClearColorAttachment(uint32_t, const std::array<float, 4>&) noexcept override {}
			void ClearDepthAttachment(float, std::optional<uint8_t>) noexcept override
			{
			}
			void SetViewport(const RHIViewport&) noexcept override {}
			void SetScissorRect(const RHIScissorRect&) noexcept override {}
			void SetPrimitiveTopology(RHIPrimitiveTopology) noexcept override {}
			void SetConstantBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetPushConstants(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override
			{
			}
			void SetVertexBuffers(
				uint32_t, std::span<const RHIVertexBufferBinding>) noexcept override
			{
			}
			void SetIndexBuffer(const RHIIndexBufferBinding&) noexcept override {}
			void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) noexcept override {}
			void Draw(uint32_t, uint32_t, uint32_t, uint32_t) noexcept override {}

			uint32_t m_BeginProfileCount = 0;
			uint32_t m_EndProfileCount = 0;
			uint32_t m_FlushBarrierCount = 0;
			uint32_t m_TextureBarrierCount = 0;
			uint32_t m_BufferBarrierCount = 0;
			uint32_t m_CopyBufferCount = 0;
			uint32_t m_BeginRenderingCount = 0;
			uint32_t m_EndRenderingCount = 0;
			bool m_IsRendering = false;
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
			void CopyBuffer(
				RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override
			{
			}
			void BeginGpuProfileScope(std::string_view) noexcept override { ++m_BeginProfileCount; }
			void EndGpuProfileScope() noexcept override { ++m_EndProfileCount; }
			void SetPipeline(RHIPipelineHandle) noexcept override {}
			void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
			void SetConstantBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetReadWriteBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
			void SetPushConstants(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override
			{
			}
			void Dispatch(uint32_t, uint32_t, uint32_t) noexcept override {}

			uint32_t m_BeginProfileCount = 0;
			uint32_t m_EndProfileCount = 0;
			uint32_t m_FlushBarrierCount = 0;
			uint32_t m_TextureBarrierCount = 0;
			uint32_t m_BufferBarrierCount = 0;
		};

		class RecordingTransferContext final : public RHITransferContext
		{
		public:
			RHICommandContextHandle GetHandle() const noexcept override { return {}; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Copy; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override
			{
				m_TextureBarriers.insert(m_TextureBarriers.end(), barriers.begin(), barriers.end());
			}
			void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
			{
				m_BufferBarriers.insert(m_BufferBarriers.end(), barriers.begin(), barriers.end());
			}
			void FlushBarriers() noexcept override {}
			void CopyBuffer(
				RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override
			{
			}
			void Begin() noexcept override {}
			RHIFencePoint Submit(bool) noexcept override
			{
				++m_SubmitCallCount;
				return m_FailSubmit ? RHIFencePoint{} : RHIFencePoint{ RHIFenceHandle{ 1, 1 }, 7 };
			}
			void Abort() noexcept override { ++m_AbortCallCount; }
			void ReclaimCompleted() noexcept override {}
			bool UploadBuffer(
				const void*, uint64_t, RHIBufferHandle, uint64_t) noexcept override
			{
				return !m_FailUploads;
			}
			bool UploadTexture(
				const RHITextureUploadData&, RHITextureHandle) noexcept override
			{
				return !m_FailUploads;
			}
			RHITextureReadbackRequest ReadbackTexture(
				RHITextureHandle, const RHITextureDesc& desc) noexcept override
			{
				if (m_FailReadbacks)
				{
					return {};
				}
				RHITextureReadbackRequest request{};
				request.m_Buffer = RHIBufferOwner(nullptr, RHIBufferHandle{ 9, 1 });
				request.m_BufferSizeInBytes = 4;
				request.m_TextureDesc = desc;
				request.m_Subresources.push_back({});
				return request;
			}

			std::vector<RHITextureBarrier> m_TextureBarriers;
			std::vector<RHIBufferBarrier> m_BufferBarriers;
			bool m_FailUploads = false;
			bool m_FailReadbacks = true;
			bool m_FailSubmit = false;
			uint32_t m_SubmitCallCount = 0;
			uint32_t m_AbortCallCount = 0;
		};

		[[nodiscard]] bool NearlyEqual(
			float lhs, float rhs, float tolerance = ProjectionTolerance) noexcept
		{
			return std::abs(lhs - rhs) <= tolerance;
		}

		[[nodiscard]] bool NearlyEqual(
			const Vector2& lhs, const Vector2& rhs, float tolerance = ProjectionTolerance) noexcept
		{
			return NearlyEqual(lhs.m_X, rhs.m_X, tolerance) &&
				NearlyEqual(lhs.m_Y, rhs.m_Y, tolerance);
		}

		[[nodiscard]] bool NearlyEqual(
			const Vector3& lhs, const Vector3& rhs, float tolerance = PositionTolerance) noexcept
		{
			return NearlyEqual(lhs.m_X, rhs.m_X, tolerance) &&
				NearlyEqual(lhs.m_Y, rhs.m_Y, tolerance) &&
				NearlyEqual(lhs.m_Z, rhs.m_Z, tolerance);
		}

		[[nodiscard]] ProjectedPosition ProjectPosition(
			const Vector3& position, const Matrix& transform) noexcept
		{
			const Vector4 clipPosition = math::Transform(Vector4(position, 1.0f), transform);
			const float inverseW = 1.0f / clipPosition.m_W;
			const Vector2 ndc(clipPosition.m_X * inverseW, clipPosition.m_Y * inverseW);
			return ProjectedPosition{
				.m_UV = screen_space::NDCToUV(ndc),
				.m_RawDepth = clipPosition.m_Z * inverseW,
			};
		}

		void RunSuiteSmokeTests(SelfTestContext& context) noexcept
		{
			context.Check(true, "Rendering contract suite executes deterministic checks");
		}

		void RunOpaqueSceneExtensionContractTests(SelfTestContext& context) noexcept
		{
			static_assert(std::is_abstract_v<RenderPipelineSceneExtensionBase>);
			static_assert(std::has_virtual_destructor_v<RenderPipelineSceneExtensionBase>);
			static_assert(!std::is_copy_constructible_v<RenderPipelineForwardPBR::CreateInfo>);
			static_assert(std::is_nothrow_move_constructible_v<
				RenderPipelineForwardPBR::CreateInfo>);

			uint32_t addPassCount = 0;
			uint32_t destructionCount = 0;
			RGBufferId scenePhaseBuffer;
			auto extension = std::make_unique<RecordingOpaqueSceneExtension>(
				scenePhaseBuffer, addPassCount, destructionCount);
			RecordingOpaqueSceneExtension* extensionPtr = extension.get();
			context.Check(extensionPtr->GetTemporalParticipation() ==
				SceneExtensionTemporalParticipation::PostTAA,
				"Scene extensions default to post-TAA participation until they declare a temporal contract");

			RenderGraph graph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			graph.AddPass<OpaqueSceneExtensionFixturePassData>(
				"RenderingContract.BuiltInOpaque",
				[&scenePhaseBuffer](RenderGraph::RGBuilder& builder,
					OpaqueSceneExtensionFixturePassData& data)
				{
					scenePhaseBuffer = builder.CreateBuffer("OpaqueScenePhaseBuffer");
					builder.WriteInPlace(scenePhaseBuffer, RGBufferAccess::StorageWrite);
					data.m_ScenePhaseBuffer = scenePhaseBuffer;
				});
			graph.AddPass<OpaqueSceneExtensionFixturePassData>(
				"RenderingContract.TemporalAA",
				[&scenePhaseBuffer](RenderGraph::RGBuilder& builder,
					OpaqueSceneExtensionFixturePassData& data)
				{
					builder.ReadWriteInPlace(
						scenePhaseBuffer, RGBufferAccess::StorageReadWrite);
					data.m_ScenePhaseBuffer = scenePhaseBuffer;
				});
			const RenderScene renderScene{};
			const RenderFrameContext frameContext{ .m_RenderScene = renderScene };
			const RenderServices services{};
			extensionPtr->AddOpaqueScenePasses(graph, frameContext, services);
			for (const char* passName : {
				"RenderingContract.ForwardTransparent",
				"RenderingContract.DebugDrawScene",
				})
			{
				graph.AddPass<OpaqueSceneExtensionFixturePassData>(passName,
					[&scenePhaseBuffer](RenderGraph::RGBuilder& builder,
						OpaqueSceneExtensionFixturePassData& data)
					{
						builder.ReadWriteInPlace(
							scenePhaseBuffer, RGBufferAccess::StorageReadWrite);
						data.m_ScenePhaseBuffer = scenePhaseBuffer;
					});
			}
			graph.AddPass<OpaqueSceneExtensionFixturePassData>(
				"RenderingContract.PostProcess",
				[&scenePhaseBuffer](RenderGraph::RGBuilder& builder,
					OpaqueSceneExtensionFixturePassData& data)
				{
					data.m_ScenePhaseBuffer =
						builder.Read(scenePhaseBuffer, RGBufferAccess::StorageRead);
					builder.SideEffect();
				});
			const bool compiled = graph.Compile();
			context.Check(compiled && addPassCount == 1,
				"Opaque scene extensions add their RenderGraph passes through the narrow hook");

			if (compiled)
			{
				RGSnapshot snapshot;
				BuildRenderGraphSnapshot(graph, snapshot);
				context.Check(snapshot.m_Passes.size() == 6 &&
					snapshot.m_Passes[2].m_Name == "RenderingContract.OpaqueSceneExtension" &&
					!snapshot.m_Passes[2].m_Culled,
					"Opaque scene extension passes remain live in the compiled graph");
				context.Check(snapshot.m_Passes.size() == 6 &&
					snapshot.m_Passes[0].m_ExecutionOrder <
						snapshot.m_Passes[1].m_ExecutionOrder &&
					snapshot.m_Passes[1].m_ExecutionOrder <
						snapshot.m_Passes[2].m_ExecutionOrder &&
					snapshot.m_Passes[2].m_ExecutionOrder <
						snapshot.m_Passes[3].m_ExecutionOrder &&
					snapshot.m_Passes[3].m_ExecutionOrder <
						snapshot.m_Passes[4].m_ExecutionOrder &&
					snapshot.m_Passes[4].m_ExecutionOrder <
						snapshot.m_Passes[5].m_ExecutionOrder,
					"Forward scene composition orders opaque, TAA, post-TAA extension, transparent, "
					"scene debug, and post-process consumers");
			}

			auto debugReadback = std::make_shared<ForwardPlusDebugReadback>();
			const long debugReadbackOwnerCount = debugReadback.use_count();
			{
				RenderPipelineForwardPBR pipeline(RenderPipelineForwardPBR::CreateInfo{
					.m_ForwardPlusDebugReadback = debugReadback,
					.m_SceneExtension = std::move(extension),
					});
				context.Check(extension == nullptr && destructionCount == 0 &&
					debugReadback.use_count() > debugReadbackOwnerCount,
					"ForwardPBR owns the optional extension alongside Forward+ diagnostics");
			}
			context.Check(destructionCount == 1 &&
				debugReadback.use_count() == debugReadbackOwnerCount,
				"ForwardPBR retires optional extension and debug ownership with the pipeline");

			RenderPipelineForwardPBR defaultPipeline;
			context.Check(defaultPipeline.GetName() == "ForwardPBR",
				"ForwardPBR remains source-compatible without an optional extension");
		}

		void RunOverlayExtensionContractTests(SelfTestContext& context) noexcept
		{
			static_assert(std::is_abstract_v<RenderPipelineOverlayExtensionBase>);
			static_assert(std::has_virtual_destructor_v<RenderPipelineOverlayExtensionBase>);
			static_assert(std::is_same_v<decltype(RenderServices::m_OverlayExtension),
				RenderPipelineOverlayExtensionBase*>);

			uint32_t addPassCount = 0;
			RecordingOverlayExtension extension(addPassCount);
			const RenderServices services{ .m_OverlayExtension = &extension };
			const RenderScene renderScene{};
			const RenderFrameContext frameContext{ .m_RenderScene = renderScene };
			RenderGraph graph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			services.m_OverlayExtension->AddOverlayPasses(graph, frameContext, services);

			const bool compiled = graph.Compile();
			context.Check(compiled && addPassCount == 1,
				"Overlay extensions add their RenderGraph passes through the runtime-owned hook");
			if (compiled)
			{
				RGSnapshot snapshot;
				BuildRenderGraphSnapshot(graph, snapshot);
				context.Check(snapshot.m_Passes.size() == 1 &&
					snapshot.m_Passes.front().m_Name == "RenderingContract.OverlayExtension" &&
					!snapshot.m_Passes.front().m_Culled,
					"Application-owned overlay passes remain live without a concrete DevTools service");
			}
		}

		void RunRuntimePathConfigurationContractTests(SelfTestContext& context) noexcept
		{
			class RecordingContextFactory final : public RHIContextFactoryBase
			{
			public:
				std::unique_ptr<RHIContext> CreateContext(
					const RHIContextDesc& desc) const noexcept override
				{
					m_CreateCalled = true;
					m_Desc = desc;
					return {};
				}

				mutable bool m_CreateCalled = false;
				mutable RHIContextDesc m_Desc{};
			};

			static_assert(std::is_abstract_v<RHIContextFactoryBase>);
			static_assert(std::has_virtual_destructor_v<RHIContextFactoryBase>);
			static_assert(std::is_same_v<decltype(Renderer::CreateInfo::m_RHIContextFactory),
				const RHIContextFactoryBase*>);

			Renderer::CreateInfo createInfo{};
			context.Check(!createInfo.HasRequiredRuntimePaths(),
				"Renderer configuration rejects missing host-supplied runtime paths");
			createInfo.m_IblDerivedDataCacheDirectory = "DerivedDataCache/IBL";
			context.Check(createInfo.HasRequiredRuntimePaths(),
				"Renderer configuration accepts the required host-supplied IBL cache root");

			Renderer missingFactoryRenderer;
			context.Check(!missingFactoryRenderer.Initialize(createInfo),
				"Renderer rejects a missing host-supplied RHI context factory");

			RecordingContextFactory factory;
			createInfo.m_RHIContextFactory = &factory;
			createInfo.m_Width = 1280;
			createInfo.m_Height = 720;
			Renderer renderer;
			context.Check(!renderer.Initialize(createInfo) && factory.m_CreateCalled &&
				factory.m_Desc.m_Width == 1280 && factory.m_Desc.m_Height == 720,
				"Renderer borrows the host factory and forwards only neutral context settings");
		}

		void RunNapaVoxelRenderGraphContractTests(SelfTestContext& context) noexcept
		{
			struct FixturePassData
			{
				RGTextureId m_Color{};
				RGTextureId m_Depth{};
				RGBufferId m_Vertex{};
				RGBufferId m_Index{};
			};

			RecordingDevice device;
			RenderGraph graph({
				.m_Device = &device,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGTextureId sceneColor{};
			RGTextureId sceneDepth{};
			graph.AddPass<FixturePassData>("RenderingContract.BuiltInOpaque",
				[&sceneColor, &sceneDepth](RenderGraph::RGBuilder& builder, FixturePassData& data)
				{
					const RHITextureDesc colorDesc{
						.m_Format = RHIFormat::R16G16B16A16Float,
						.m_Extent = { 1280, 720, 1 },
					};
					const RHITextureDesc depthDesc{
						.m_Format = RHIFormat::D32Float,
						.m_Extent = { 1280, 720, 1 },
					};
					sceneColor = builder.CreateTexture("NapaVoxel.SceneColor", colorDesc);
					sceneDepth = builder.CreateTexture("NapaVoxel.SceneDepth", depthDesc);
					builder.WriteInPlace(sceneColor, RGTextureAccess::RenderTarget);
					builder.WriteInPlace(sceneDepth, RGTextureAccess::DepthStencilWrite);
					data.m_Color = sceneColor;
					data.m_Depth = sceneDepth;
				});
			graph.AddPass<FixturePassData>("RenderingContract.NapaVoxel",
				[&sceneColor, &sceneDepth](RenderGraph::RGBuilder& builder, FixturePassData& data)
				{
					builder.ReadWriteInPlace(sceneColor, RGTextureAccess::RenderTarget);
					builder.ReadWriteInPlace(sceneDepth, RGTextureAccess::DepthStencilWrite);
					data.m_Color = sceneColor;
					data.m_Depth = sceneDepth;
					const RHIBufferDesc vertexDesc{
						.m_SizeInBytes = 3 * 6 * sizeof(float),
						.m_StrideInBytes = 6 * sizeof(float),
						.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest,
					};
					const RHIBufferDesc indexDesc{
						.m_SizeInBytes = 3 * sizeof(uint32_t),
						.m_StrideInBytes = sizeof(uint32_t),
						.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest,
					};
					data.m_Vertex = builder.ImportBuffer("NapaVoxel.Vertex",
						RHIBufferHandle{ 41, 1 }, vertexDesc, RGBufferAccess::None,
						RGContentValidity::Defined);
					data.m_Index = builder.ImportBuffer("NapaVoxel.Index",
						RHIBufferHandle{ 42, 1 }, indexDesc, RGBufferAccess::None,
						RGContentValidity::Defined);
					data.m_Vertex = builder.Read(
						data.m_Vertex, RGBufferAccess::Vertex, RHIStage::VertexShader);
					data.m_Index = builder.Read(
						data.m_Index, RGBufferAccess::Index, RHIStage::IndexInput);
					builder.Export(data.m_Vertex, RGBufferAccess::None);
					builder.Export(data.m_Index, RGBufferAccess::None);
				});
			graph.AddPass<FixturePassData>("RenderingContract.ForwardTransparent",
				[&sceneColor, &sceneDepth](RenderGraph::RGBuilder& builder, FixturePassData& data)
				{
					builder.ReadWriteInPlace(sceneColor, RGTextureAccess::RenderTarget);
					data.m_Color = sceneColor;
					data.m_Depth =
						builder.Read(sceneDepth, RGTextureAccess::DepthStencilRead);
				});
			graph.AddPass<FixturePassData>("RenderingContract.PostProcess",
				[&sceneColor](RenderGraph::RGBuilder& builder, FixturePassData& data)
				{
					data.m_Color = builder.Read(sceneColor, RGTextureAccess::Sample);
					builder.SideEffect();
				});

			const bool compiled = graph.Compile();
			context.Check(compiled, "Napa voxel production RenderGraph dataflow fixture compiles");
			if (!compiled)
			{
				return;
			}

			const RGExecutionPlan* plan = graph.GetExecutionPlan();
			const bool passChainMatches = plan && plan->GetPasses().size() == 4 &&
				plan->GetPasses()[0].m_ExecutionOrder < plan->GetPasses()[1].m_ExecutionOrder &&
				plan->GetPasses()[1].m_ExecutionOrder < plan->GetPasses()[2].m_ExecutionOrder &&
				plan->GetPasses()[2].m_ExecutionOrder < plan->GetPasses()[3].m_ExecutionOrder;
			context.Check(passChainMatches,
				"Napa voxel attachment writes remain between built-in opaque and later consumers");

			bool resourceContractMatches = passChainMatches;
			if (resourceContractMatches)
			{
				const RGCompiledPass& voxelPass = plan->GetPasses()[1];
				const auto hasAccess = [&voxelPass](RGResourceType type, uint64_t access) noexcept
					{
						return std::ranges::any_of(voxelPass.m_Accesses,
							[type, access](const RGCompiledAccess& compiledAccess) noexcept
							{
								return compiledAccess.m_ResourceType == type &&
									compiledAccess.m_AccessValue == access;
							});
					};
				resourceContractMatches =
					hasAccess(RGResourceType::RGTexture,
						static_cast<uint64_t>(RGTextureAccess::RenderTarget)) &&
					hasAccess(RGResourceType::RGTexture,
						static_cast<uint64_t>(RGTextureAccess::DepthStencilWrite)) &&
					hasAccess(RGResourceType::RGBuffer,
						static_cast<uint64_t>(RGBufferAccess::Vertex)) &&
					hasAccess(RGResourceType::RGBuffer,
						static_cast<uint64_t>(RGBufferAccess::Index));
			}
			context.Check(resourceContractMatches,
				"Napa voxel declares HDR color load/write, depth load/write, and mesh buffer reads");

			bool barrierContractMatches = passChainMatches;
			if (barrierContractMatches)
			{
				const RGCompiledPass& voxelPass = plan->GetPasses()[1];
				const RHIResourceState common = CommonRHIResourceState();
				const RHIResourceState vertex = ToRHIResourceState(RGBufferAccess::Vertex);
				const RHIResourceState index = ToRHIResourceState(RGBufferAccess::Index);
				const auto matchesTransition = [](const RGBarrierIntent& barrier,
					const RHIResourceState& before, const RHIResourceState& after) noexcept
					{
						return barrier.m_Kind == RGBarrierKind::Transition &&
							barrier.m_Before == before && barrier.m_After == after;
					};
				barrierContractMatches = voxelPass.m_PreBarriers.size() == 2 &&
					voxelPass.m_PostBarriers.size() == 2 &&
					std::ranges::any_of(voxelPass.m_PreBarriers,
						[&](const RGBarrierIntent& barrier)
						{ return matchesTransition(barrier, common, vertex); }) &&
					std::ranges::any_of(voxelPass.m_PreBarriers,
						[&](const RGBarrierIntent& barrier)
						{ return matchesTransition(barrier, common, index); }) &&
					std::ranges::any_of(voxelPass.m_PostBarriers,
						[&](const RGBarrierIntent& barrier)
						{ return matchesTransition(barrier, vertex, common); }) &&
					std::ranges::any_of(voxelPass.m_PostBarriers,
						[&](const RGBarrierIntent& barrier)
						{ return matchesTransition(barrier, index, common); });
			}
			context.Check(barrierContractMatches,
				"Napa voxel mesh buffers transition from Common for draw and return to Common");
		}

		void RunRHIFrameAndGraphicsScopeContractTests(SelfTestContext& context) noexcept
		{
			static_assert(!HasPublicPresent<RHISwapChain>);
			static_assert(!HasPublicFrameCompletionWait<RHISwapChain>);
			static_assert(!HasPublicCurrentBackBufferIndex<RHISwapChain>);

			const RHIFrameBeginResult unavailable = RHIFrameBeginResult::Unavailable();
			const RHIFrameBeginResult fatalBegin = RHIFrameBeginResult::Fatal();
			context.Check(unavailable.IsUnavailable() && !unavailable.IsReady() &&
				unavailable.GetFrame() == nullptr && fatalBegin.IsFatal() &&
				fatalBegin.GetFrame() == nullptr,
				"RHI BeginFrame distinguishes temporary unavailability from fatal failure without a frame");

			const RHIFencePoint submittedFence{ RHIFenceHandle{ 7, 1 }, 19 };
			const RHIFrameEndResult completed = RHIFrameEndResult::Completed(submittedFence);
			const RHIFrameEndResult fatalAfterSubmit = RHIFrameEndResult::Fatal(submittedFence);
			const RHIFrameEndResult fatalBeforeSubmit = RHIFrameEndResult::Fatal();
			context.Check(completed.IsCompleted() && completed.HasSubmission() &&
				completed.GetSubmittedFence() == submittedFence &&
				fatalAfterSubmit.IsFatal() && fatalAfterSubmit.HasSubmission() &&
				fatalAfterSubmit.GetSubmittedFence() == submittedFence &&
				fatalBeforeSubmit.IsFatal() && !fatalBeforeSubmit.HasSubmission(),
				"RHI EndFrame separates transaction completion from submitted-fence lifetime semantics");

			constexpr uint32_t FrameSlotCount = 2;
			constexpr uint32_t SwapChainImageCount = 3;
			RenderFrameBuilder::BuildResult syntheticFrame{};
			syntheticFrame.m_FrameSlotIndex = 1;
			syntheticFrame.m_BackBufferIndex = 2;
			syntheticFrame.m_FrameSerial = 17;
			const RenderFrameContext renderContext = syntheticFrame.MakeRenderFrameContext();
			std::array<uint32_t, FrameSlotCount> frameSlotStorage{};
			std::array<uint32_t, SwapChainImageCount> swapChainImageStorage{};
			frameSlotStorage[renderContext.m_FrameSlotIndex] = 11;
			swapChainImageStorage[renderContext.m_BackBufferIndex] = 29;
			context.Check(renderContext.m_FrameSlotIndex < FrameSlotCount &&
				renderContext.m_BackBufferIndex < SwapChainImageCount &&
				renderContext.m_FrameSlotIndex != renderContext.m_BackBufferIndex &&
				frameSlotStorage[1] == 11 && swapChainImageStorage[2] == 29,
				"Frame context preserves independent frame-slot and swapchain-image indices");

			RenderFrameBuilder::BuildResult lateValidationFrame{};
			lateValidationFrame.m_UploadFencePoint =
				RHIFencePoint{ RHIFenceHandle{ 9, 1 }, 23 };
			lateValidationFrame.m_SceneGpuAllocations.m_SceneConstants.m_OffsetInBytes = 64;
			lateValidationFrame.m_SceneGpuAllocations.m_SceneConstants.m_SizeInBytes = 128;
			const RenderFrameContext lateValidationContext =
				lateValidationFrame.MakeRenderFrameContext();
			RenderFrameGpuResources frameGpuResources{};
			frameGpuResources.AdoptFrom(lateValidationContext);
			const uint64_t adoptedSceneConstantOffset =
				frameGpuResources.m_SceneGpuAllocations.m_SceneConstants.m_OffsetInBytes;
			frameGpuResources.AdoptFrom(lateValidationContext);
			context.Check(lateValidationFrame.m_SceneGpuAllocations.IsEmpty() &&
				frameGpuResources.m_UploadFencePoint == lateValidationFrame.m_UploadFencePoint &&
				frameGpuResources.m_SceneGpuAllocations.m_SceneConstants.IsValid() &&
				adoptedSceneConstantOffset == 64,
				"Frame-build GPU resources transfer before late validation and remain owned on early return");

			const auto& bgraUnorm = GetRHIFormatInfo(RHIFormat::B8G8R8A8Unorm);
			const auto& bgraSrgb = GetRHIFormatInfo(RHIFormat::B8G8R8A8UnormSrgb);
			context.Check(bgraUnorm.m_Family == RHIFormatFamily::B8G8R8A8 &&
				bgraSrgb.m_Family == RHIFormatFamily::B8G8R8A8 &&
				AreRHIFormatsInSameFamily(bgraUnorm.m_Format, bgraSrgb.m_Format) &&
				!AreRHIFormatsInSameFamily(
					RHIFormat::B8G8R8A8Unorm, RHIFormat::R8G8B8A8Unorm) &&
				bgraUnorm.m_BytesPerBlock == 4,
				"Pure RHI format metadata keeps BGRA8 in its own color-view family");

			bool locationsAreExplicit = true;
			for (uint32_t layoutIndex = 0;
				layoutIndex < static_cast<uint32_t>(InputLayoutID::Count); ++layoutIndex)
			{
				const auto vertexLayout =
					BuildRHIVertexInputLayoutDesc(static_cast<InputLayoutID>(layoutIndex));
				locationsAreExplicit &= vertexLayout.m_AttributeCount > 0;
				for (uint32_t attributeIndex = 0;
					attributeIndex < vertexLayout.m_AttributeCount; ++attributeIndex)
				{
					locationsAreExplicit &=
						vertexLayout.m_Attributes[attributeIndex].m_Location == attributeIndex;
				}
			}
			context.Check(locationsAreExplicit,
				"Built-in RHI vertex layouts assign deterministic explicit attribute locations");

			RHIRenderingSignature renderingSignature{};
			renderingSignature.m_ColorFormats[0] = RHIFormat::R8G8B8A8Unorm;
			renderingSignature.m_ColorAttachmentCount = 1;
			renderingSignature.m_DepthFormat = RHIFormat::D32Float;
			renderingSignature.m_SampleCount = 4;
			RHIGraphicsPipelineDesc pipelineDesc{};
			pipelineDesc.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
			pipelineDesc.m_RenderTargetCount = 1;
			pipelineDesc.m_DepthStencilFormat = RHIFormat::D32Float;
			pipelineDesc.m_SampleCount = 4;
			const bool matchingSignature =
				IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
			pipelineDesc.m_RenderTargetFormats[0] = RHIFormat::B8G8R8A8Unorm;
			const bool rejectsColorMismatch =
				!IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
			pipelineDesc.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
			pipelineDesc.m_DepthStencilFormat = RHIFormat::Unknown;
			const bool rejectsDepthMismatch =
				!IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
			pipelineDesc.m_DepthStencilFormat = RHIFormat::D32Float;
			pipelineDesc.m_SampleCount = 1;
			const bool rejectsSampleMismatch =
				!IsGraphicsPipelineCompatible(pipelineDesc, renderingSignature);
			context.Check(matchingSignature && rejectsColorMismatch && rejectsDepthMismatch &&
				rejectsSampleMismatch,
				"Graphics pipeline compatibility covers active color, depth, and sample signature");
		}

		void RunDX12GraphicsContractLoweringTests(SelfTestContext& context) noexcept
		{
			const bool bgraRoundTrips =
				ToDXGIFormat(RHIFormat::B8G8R8A8Unorm) == DXGI_FORMAT_B8G8R8A8_UNORM &&
				ToDXGIFormat(RHIFormat::B8G8R8A8UnormSrgb) ==
				DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
				ToRHIFormat(DXGI_FORMAT_B8G8R8A8_UNORM) == RHIFormat::B8G8R8A8Unorm &&
				ToRHIFormat(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ==
				RHIFormat::B8G8R8A8UnormSrgb;
			context.Check(bgraRoundTrips,
				"DX12 preserves actual BGRA8 swapchain formats through RHI conversion");

			GraphicsPhysicalPipelineKey recipe{};
			recipe.m_InputLayoutId = InputLayoutID::P3N3T2;
			const RHIGraphicsPipelineDesc baseDesc = BuildRHIGraphicsPipelineDesc(recipe);
			RHIGraphicsPipelineDesc changedDesc = baseDesc;
			++changedDesc.m_VertexInput.m_Attributes[1].m_Location;
			const DX12GraphicsPipelineShaderInputs shaders{};
			const auto baseKey =
				MakeDX12RHIGraphicsPSOKey(baseDesc, RootSignatureID{ 3 }, shaders);
			const auto changedKey =
				MakeDX12RHIGraphicsPSOKey(changedDesc, RootSignatureID{ 3 }, shaders);
			context.Check(baseKey != changedKey,
				"DX12 normalized graphics keys include explicit RHI vertex locations");

			const RHIBufferBarrier copyBarrier{
				.m_Before = {
					.m_Stages = RHIStage::Copy,
					.m_Access = RHIAccess::CopySource,
					.m_Layout = RHILayout::CopySource,
				},
				.m_After = {
					.m_Stages = RHIStage::Copy,
					.m_Access = RHIAccess::CopyDest,
					.m_Layout = RHILayout::CopyDest,
				},
			};
			auto* nativeResource = reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 });
			const D3D12_BUFFER_BARRIER nativeBarrier =
				BuildD3D12BufferBarrier(copyBarrier, nativeResource);
			context.Check(nativeBarrier.SyncBefore == D3D12_BARRIER_SYNC_COPY &&
				nativeBarrier.SyncAfter == D3D12_BARRIER_SYNC_COPY &&
				nativeBarrier.AccessBefore == D3D12_BARRIER_ACCESS_COPY_SOURCE &&
				nativeBarrier.AccessAfter == D3D12_BARRIER_ACCESS_COPY_DEST &&
				nativeBarrier.pResource == nativeResource,
				"DX12 lowers buffer transitions to the exact Enhanced Barrier contract");
		}

		void RunProjectionConventionTests(SelfTestContext& context) noexcept
		{
			constexpr float NearZ = 0.25f;
			constexpr float FarZ = 250.0f;
			constexpr float FovRadians = math::ToRadians(67.0f);
			constexpr float Aspect = 16.0f / 9.0f;

			const Matrix standardProjection =
				math::CreatePerspectiveFieldOfViewLH(FovRadians, Aspect, NearZ, FarZ);
			const Matrix reversedProjection =
				math::CreatePerspectiveFieldOfViewLHReversedZ(FovRadians, Aspect, NearZ, FarZ);

			const float standardNear =
				ProjectPosition(Vector3(0.0f, 0.0f, NearZ), standardProjection).m_RawDepth;
			const float standardFar =
				ProjectPosition(Vector3(0.0f, 0.0f, FarZ), standardProjection).m_RawDepth;
			const float reversedNear =
				ProjectPosition(Vector3(0.0f, 0.0f, NearZ), reversedProjection).m_RawDepth;
			const float reversedFar =
				ProjectPosition(Vector3(0.0f, 0.0f, FarZ), reversedProjection).m_RawDepth;

			context.Check(NearlyEqual(standardNear, 0.0f) && NearlyEqual(standardFar, 1.0f),
				"Standard-Z projection maps near to zero and far to one");
			context.Check(NearlyEqual(reversedNear, 1.0f) && NearlyEqual(reversedFar, 0.0f),
				"Reversed-Z projection maps near to one and far to zero");
			context.Check(
				screen_space::GetDepthBackgroundValue(DepthConvention::Standard) == 1.0f &&
				screen_space::GetDepthBackgroundValue(DepthConvention::Reversed) == 0.0f &&
				screen_space::IsDepthBackground(1.0f, DepthConvention::Standard) &&
				screen_space::IsDepthBackground(0.0f, DepthConvention::Reversed),
				"Depth background values are convention-aware");
			context.Check(
				!screen_space::IsDepthBackground(1.0f - 5.0e-7f, DepthConvention::Standard) &&
				!screen_space::IsDepthBackground(5.0e-7f, DepthConvention::Reversed),
				"Non-clear depth values near the far plane remain geometry");

			constexpr float PrecisionNearZ = 0.05f;
			constexpr float PrecisionFarZ = 5000.0f;
			constexpr float FarGeometryZ = PrecisionFarZ * 0.95f;
			const Matrix precisionStandardProjection = math::CreatePerspectiveFieldOfViewLH(
				FovRadians, Aspect, PrecisionNearZ, PrecisionFarZ);
			const Matrix precisionReversedProjection =
				math::CreatePerspectiveFieldOfViewLHReversedZ(
					FovRadians, Aspect, PrecisionNearZ, PrecisionFarZ);
			const float precisionStandardDepth =
				ProjectPosition(Vector3(0.0f, 0.0f, FarGeometryZ), precisionStandardProjection)
				.m_RawDepth;
			const float precisionReversedDepth =
				ProjectPosition(Vector3(0.0f, 0.0f, FarGeometryZ), precisionReversedProjection)
				.m_RawDepth;
			context.Check(precisionStandardDepth < 1.0f &&
				1.0f - precisionStandardDepth < 1.0e-6f &&
				!screen_space::IsDepthBackground(
					precisionStandardDepth, DepthConvention::Standard) &&
				precisionReversedDepth > 0.0f && precisionReversedDepth < 1.0e-6f &&
				!screen_space::IsDepthBackground(
					precisionReversedDepth, DepthConvention::Reversed),
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
				standardSample.m_UV, standardSample.m_RawDepth, math::Inverse(standardProjection));
			const Vector3 reconstructedReversed = screen_space::ReconstructViewPosition(
				reversedSample.m_UV, reversedSample.m_RawDepth, math::Inverse(reversedProjection));
			const Vector4 standardDepthParams =
				screen_space::MakeDepthReconstructionParams(standardProjection);
			const Vector4 reversedDepthParams =
				screen_space::MakeDepthReconstructionParams(reversedProjection);

			context.Check(NearlyEqual(reconstructedStandard, positionVS),
				"Standard-Z reconstructs a mid-depth view position");
			context.Check(NearlyEqual(reconstructedReversed, positionVS),
				"Reversed-Z reconstructs a mid-depth view position");
			context.Check(
				NearlyEqual(screen_space::RawDepthToPositiveViewZ(reversedSample.m_UV,
					reversedSample.m_RawDepth, math::Inverse(reversedProjection)),
					positionVS.m_Z, PositionTolerance),
				"Raw depth reconstructs positive left-handed view Z");
			context.Check(
				NearlyEqual(screen_space::RawDepthToPositiveViewZ(standardSample.m_UV,
					standardSample.m_RawDepth, math::Inverse(standardProjection)),
					positionVS.m_Z, PositionTolerance),
				"Standard-Z raw depth reconstructs positive left-handed view Z");
			context.Check(NearlyEqual(screen_space::RawDepthToPositiveViewZ(
				standardSample.m_RawDepth, standardDepthParams, DepthConvention::Standard),
				positionVS.m_Z, PositionTolerance) &&
				NearlyEqual(screen_space::RawDepthToPositiveViewZ(
					reversedSample.m_RawDepth, reversedDepthParams, DepthConvention::Reversed),
					positionVS.m_Z, PositionTolerance),
				"Compact depth reconstruction matches Standard-Z and Reversed-Z projection contracts");

			const Matrix view = math::CreateLookAtLH(
				Vector3(3.0f, 2.0f, -4.0f), Vector3(0.0f, 1.0f, 5.0f), Vector3::UnitY);
			const Matrix viewProjection = view * reversedProjection;
			const Vector3 positionWS(1.25f, 0.5f, 8.0f);
			const ProjectedPosition worldSample = ProjectPosition(positionWS, viewProjection);
			const Vector3 reconstructedWorld = screen_space::ReconstructWorldPosition(
				worldSample.m_UV, worldSample.m_RawDepth, math::Inverse(viewProjection));
			context.Check(NearlyEqual(reconstructedWorld, positionWS),
				"Reversed-Z raw depth reconstructs world position");

			const Matrix standardViewProjection = view * standardProjection;
			const ProjectedPosition standardWorldSample =
				ProjectPosition(positionWS, standardViewProjection);
			const Vector3 reconstructedStandardWorld =
				screen_space::ReconstructWorldPosition(standardWorldSample.m_UV,
					standardWorldSample.m_RawDepth, math::Inverse(standardViewProjection));
			context.Check(NearlyEqual(reconstructedStandardWorld, positionWS),
				"Standard-Z raw depth reconstructs world position");

			const Matrix degenerateInverseTransform{};
			context.Check(NearlyEqual(screen_space::ReconstructPositionFromRawDepth(
				Vector2(0.5f, 0.5f), 0.5f, degenerateInverseTransform),
				Vector3::Zero),
				"Position reconstruction returns zero when homogeneous W is degenerate");
		}

		void RunScreenCoordinateTests(SelfTestContext& context) noexcept
		{
			const Vector2 pixelCenter = screen_space::PixelCenterToUV(0, 0, 4, 2);
			const Vector2 ndc = screen_space::UVToNDC(pixelCenter);
			context.Check(NearlyEqual(pixelCenter, Vector2(0.125f, 0.25f)),
				"Pixel centers map to top-left texture UV coordinates");
			context.Check(NearlyEqual(ndc, Vector2(-0.75f, 0.5f)) &&
				NearlyEqual(screen_space::NDCToUV(ndc), pixelCenter),
				"Top-left UV and D3D NDC coordinates round-trip");
		}

		void RunRenderViewConventionTests(SelfTestContext& context) noexcept
		{
			Camera camera(Camera::CreateInfo{});
			const ResolvedViewRenderSettings settings{};
			const ResolvedTemporalFramePlan temporalFramePlan{
				.m_DisplayViewId = RenderViewID::Main,
			};
			const RenderView mainView = RenderViewBuilder{}.Build<RenderViewID::Main>(
				RenderViewBuildInfo<RenderViewID::Main>{
					.m_Camera = camera,
					.m_RenderSettings = settings,
					.m_TemporalFramePlan = temporalFramePlan,
					.m_Width = 1280,
					.m_Height = 720,
			});
			const RenderView shadowView =
				RenderViewBuilder{}.Build<RenderViewID::DirectionalShadow>(
					RenderViewBuildInfo<RenderViewID::DirectionalShadow>{
				.m_MainView = mainView,
			});

			const float mainNear =
				ProjectPosition(Vector3(0.0f, 0.0f, mainView.m_Near),
					mainView.m_RasterProj).m_RawDepth;
			const float mainFar =
				ProjectPosition(Vector3(0.0f, 0.0f, mainView.m_Far),
					mainView.m_RasterProj).m_RawDepth;
			context.Check(mainView.m_DepthConvention == DepthConvention::Reversed &&
				NearlyEqual(mainNear, 1.0f) && NearlyEqual(mainFar, 0.0f),
				"Main view records and projects with its Reversed-Z contract");
			context.Check(shadowView.m_DepthConvention == DepthConvention::Standard,
				"Directional shadow view records its Standard-Z contract");
		}

		void RunSampleableDepthFormatTests(SelfTestContext& context) noexcept
		{
			const RHITextureDesc depthDesc{
				.m_Format = RHIFormat::R32Typeless,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::DepthStencil,
				.m_Extent = {1280, 720, 1},
				.m_ClearValue =
					RHIClearValue{
						.m_Format = RHIFormat::D32Float,
						.m_Depth = 0.0f,
						.m_IsDepthStencil = true,
					},
			};
			const RHITextureViewDesc dsvDesc{
				.m_Type = RHITextureViewType::DepthStencil,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::D32Float,
				.m_Subresources =
					{
						.m_MipCount = 1,
						.m_ArraySliceCount = 1,
						.m_Aspects = RHITextureAspect::Depth,
					},
			};
			const RHITextureViewDesc srvDesc{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R32Float,
				.m_Subresources =
					{
						.m_MipCount = 1,
						.m_ArraySliceCount = 1,
						.m_Aspects = RHITextureAspect::Depth,
					},
			};
			context.Check(ValidateRHITextureDesc(depthDesc).IsValid() &&
				ValidateRHITextureViewDesc(depthDesc, dsvDesc).IsValid() &&
				ValidateRHITextureViewDesc(depthDesc, srvDesc).IsValid(),
				"R32 typeless main depth accepts typed D32 DSV and R32 SRV views");

			RHITextureDesc typelessClearDesc = depthDesc;
			typelessClearDesc.m_ClearValue->m_Format = RHIFormat::R32Typeless;
			context.Check(!ValidateRHITextureDesc(typelessClearDesc).IsValid(),
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
			reversedEqualRecipe.m_DepthPreset = DepthPreset::ReversedZEqualReadOnly;
			const auto reversedEqual =
				BuildRHIGraphicsPipelineDesc(reversedEqualRecipe).m_DepthStencil;
			GraphicsPhysicalPipelineKey standardWriteRecipe{};
			standardWriteRecipe.m_DepthPreset = DepthPreset::StandardZWrite;
			const auto standardWrite =
				BuildRHIGraphicsPipelineDesc(standardWriteRecipe).m_DepthStencil;
			context.Check(reversedWrite.m_DepthTestEnable && reversedWrite.m_DepthWriteEnable &&
				reversedWrite.m_DepthCompareOp == RHICompareOp::GreaterEqual &&
				reversedRead.m_DepthTestEnable && !reversedRead.m_DepthWriteEnable &&
				reversedRead.m_DepthCompareOp == RHICompareOp::GreaterEqual &&
				reversedEqual.m_DepthTestEnable &&
				!reversedEqual.m_DepthWriteEnable &&
				reversedEqual.m_DepthCompareOp == RHICompareOp::Equal &&
				standardWrite.m_DepthTestEnable && standardWrite.m_DepthWriteEnable &&
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
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGTextureId graphDepth;
			RHITextureDesc graphDepthDesc = depthDesc;
			graphDepthDesc.m_Usage = RHITextureUsage::None;
			graph.AddPass<SampleableDepthPassData>("SampleableDepth.Write",
				[&graphDepth, graphDepthDesc, graphDepthHandle, dsvDesc](
					RenderGraph::RGBuilder& builder, SampleableDepthPassData& data)
				{
					graphDepth = builder.ImportTexture("DisplayView.DepthBuffer", graphDepthHandle,
						graphDepthDesc, CommonRHIResourceState(), RGContentValidity::Undefined);
					builder.WriteInPlace(graphDepth, RGTextureAccess::DepthStencilWrite);
					data.m_View =
						builder.CreateView<RHITextureViewType::DepthStencil>(graphDepth, dsvDesc);
				});
			graph.AddPass<SampleableDepthPassData>("SampleableDepth.Sample",
				[&graphDepth, srvDesc](
					RenderGraph::RGBuilder& builder, SampleableDepthPassData& data)
				{
					graphDepth =
						builder.Read(graphDepth, RGTextureAccess::Sample, RHIStage::PixelShader);
					data.m_View =
						builder.CreateView<RHITextureViewType::ShaderResource>(graphDepth, srvDesc);
					builder.SideEffect();
				});

			const bool graphCompiled = graph.Compile();
			context.Check(
				graphCompiled, "RenderGraph sampleable-depth DSV-to-SRV fixture compiles");
			if (graphCompiled)
			{
				const RHIResourceState commonState = CommonRHIResourceState();
				const RHIResourceState depthWriteState =
					ToRHIResourceState(RGTextureAccess::DepthStencilWrite);
				const RHIResourceState pixelSampleState =
					ToRHIResourceState(RGTextureAccess::Sample, RHIStage::PixelShader);
				const auto* executionPlan = graph.GetExecutionPlan();
				const bool hasExactPlannerContract =
					executionPlan && executionPlan->GetResources().size() == 1 &&
					executionPlan->GetPasses().size() == 2 &&
					executionPlan->GetPasses()[0].m_PreBarriers.size() == 1 &&
					executionPlan->GetPasses()[0].m_PostBarriers.empty() &&
					executionPlan->GetPasses()[1].m_PreBarriers.size() == 1 &&
					executionPlan->GetPasses()[1].m_PostBarriers.size() == 1;
				bool plannerBarrierFieldsMatch = hasExactPlannerContract;
				if (hasExactPlannerContract)
				{
					const auto& writeBarrier = executionPlan->GetPasses()[0].m_PreBarriers[0];
					const auto& sampleBarrier = executionPlan->GetPasses()[1].m_PreBarriers[0];
					const auto& finalBarrier = executionPlan->GetPasses()[1].m_PostBarriers[0];
					const auto isFullTextureTransition = [](const RGBarrierIntent& barrier) noexcept
						{
							return barrier.m_Resource.Value() == 0 &&
								barrier.m_Kind == RGBarrierKind::Transition &&
								!barrier.m_Subresources.has_value();
						};
					plannerBarrierFieldsMatch =
						isFullTextureTransition(writeBarrier) &&
						writeBarrier.m_Reason == RGBarrierReason::AccessTransition &&
						writeBarrier.m_Before == commonState &&
						writeBarrier.m_After == depthWriteState &&
						isFullTextureTransition(sampleBarrier) &&
						sampleBarrier.m_Reason == RGBarrierReason::AccessTransition &&
						sampleBarrier.m_Before == depthWriteState &&
						sampleBarrier.m_After == pixelSampleState &&
						isFullTextureTransition(finalBarrier) &&
						finalBarrier.m_Reason == RGBarrierReason::FinalStateTransition &&
						finalBarrier.m_Before == pixelSampleState &&
						finalBarrier.m_After == commonState;
				}
				context.Check(plannerBarrierFieldsMatch,
					"RenderGraph plans the exact Common-to-DSW-to-pixel-SRV-to-Common depth contract");

				const bool dependencyMatches =
					executionPlan &&
					std::ranges::any_of(executionPlan->GetDependencyEdges(),
						[](const RGPassDependencyEdge& edge) noexcept
						{
							return edge.m_From.Value() == 0 && edge.m_To.Value() == 1 &&
								edge.m_Reason == RGDependencyReason::WriterToReader;
						});
				context.Check(dependencyMatches,
					"Sampleable depth preserves its writer-to-reader dependency");

				bool compiledViewsMatch =
					executionPlan && executionPlan->GetTextureViews().size() == 2;
				if (compiledViewsMatch)
				{
					const auto& compiledDsv = executionPlan->GetTextureViews()[0].m_Desc;
					const auto& compiledSrv = executionPlan->GetTextureViews()[1].m_Desc;
					compiledViewsMatch =
						compiledDsv.m_Type == RHITextureViewType::DepthStencil &&
						compiledDsv.m_Dimension == RHITextureViewDimension::Texture2D &&
						compiledDsv.m_Format == RHIFormat::D32Float &&
						compiledDsv.m_Subresources == dsvDesc.m_Subresources &&
						compiledSrv.m_Type == RHITextureViewType::ShaderResource &&
						compiledSrv.m_Dimension == RHITextureViewDimension::Texture2D &&
						compiledSrv.m_Format == RHIFormat::R32Float &&
						compiledSrv.m_Subresources == srvDesc.m_Subresources;
				}
				context.Check(compiledViewsMatch,
					"Sampleable depth compiles canonical D32 DSV and R32 SRV views of one resource");

				RGSnapshot snapshot;
				BuildRenderGraphSnapshot(graph, snapshot);
				const auto expectedUsage = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled;
				context.Check(
					snapshot.m_Resources.size() == 1 &&
					snapshot.m_Resources[0].m_UsageBits ==
					static_cast<uint64_t>(expectedUsage) &&
					snapshot.m_Resources[0].m_TextureFormat == RHIFormat::R32Typeless &&
					snapshot.m_Passes[0].m_Accesses[0].m_AccessValue ==
					static_cast<uint64_t>(RGTextureAccess::DepthStencilWrite) &&
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
				const auto isFullResourceBarrier = [](const RHITextureBarrier& barrier) noexcept
					{ return !barrier.m_Subresources.has_value(); };
				const bool loweredBarriersMatch =
					recordingGraphicsContext.m_TextureBarriers.size() == 3 &&
					recordingGraphicsContext.m_TextureBarriers[0].m_Texture == graphDepthHandle &&
					isFullResourceBarrier(recordingGraphicsContext.m_TextureBarriers[0]) &&
					recordingGraphicsContext.m_TextureBarriers[0].m_Before == commonState &&
					recordingGraphicsContext.m_TextureBarriers[0].m_After == depthWriteState &&
					recordingGraphicsContext.m_TextureBarriers[1].m_Texture == graphDepthHandle &&
					isFullResourceBarrier(recordingGraphicsContext.m_TextureBarriers[1]) &&
					recordingGraphicsContext.m_TextureBarriers[1].m_Before == depthWriteState &&
					recordingGraphicsContext.m_TextureBarriers[1].m_After == pixelSampleState &&
					recordingGraphicsContext.m_TextureBarriers[2].m_Texture == graphDepthHandle &&
					isFullResourceBarrier(recordingGraphicsContext.m_TextureBarriers[2]) &&
					recordingGraphicsContext.m_TextureBarriers[2].m_Before == pixelSampleState &&
					recordingGraphicsContext.m_TextureBarriers[2].m_After == commonState;
				context.Check(loweredBarriersMatch,
					"RenderGraph executor lowers exactly three resource-specific depth barriers");

				D3D12_RESOURCE_DESC nativeResourceDesc{};
				nativeResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				nativeResourceDesc.Width = 1280;
				nativeResourceDesc.Height = 720;
				nativeResourceDesc.DepthOrArraySize = 1;
				nativeResourceDesc.MipLevels = 1;
				nativeResourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				nativeResourceDesc.SampleDesc = { 1, 0 };
				auto* nativeResource = reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 });
				bool nativeBarrierMatches = false;
				if (recordingGraphicsContext.m_TextureBarriers.size() == 3)
				{
					const D3D12_TEXTURE_BARRIER nativeBarrier =
						BuildD3D12TextureBarrier(recordingGraphicsContext.m_TextureBarriers[1],
							nativeResource, nativeResourceDesc);
					nativeBarrierMatches =
						nativeBarrier.SyncBefore == D3D12_BARRIER_SYNC_DEPTH_STENCIL &&
						nativeBarrier.SyncAfter == D3D12_BARRIER_SYNC_PIXEL_SHADING &&
						nativeBarrier.AccessBefore == D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE &&
						nativeBarrier.AccessAfter == D3D12_BARRIER_ACCESS_SHADER_RESOURCE &&
						nativeBarrier.LayoutBefore == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE &&
						nativeBarrier.LayoutAfter == D3D12_BARRIER_LAYOUT_SHADER_RESOURCE &&
						nativeBarrier.pResource == nativeResource &&
						nativeBarrier.Subresources.IndexOrFirstMipLevel ==
						D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
						nativeBarrier.Flags == D3D12_TEXTURE_BARRIER_FLAG_NONE;
				}
				context.Check(nativeBarrierMatches,
					"DX12 lowers the depth transition to the exact Enhanced Barrier contract");
			}

			struct DepthAccessChainPassData
			{
			};
			RenderGraph accessChainGraph({
				.m_Device = &recordingDevice,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGTextureId accessChainDepth;
			accessChainGraph.AddPass<DepthAccessChainPassData>("DepthAccessChain.Prepass",
				[&accessChainDepth, graphDepthDesc, graphDepthHandle](
					RenderGraph::RGBuilder& builder, DepthAccessChainPassData&)
				{
					accessChainDepth = builder.ImportTexture("DepthAccessChain.Depth",
						graphDepthHandle, graphDepthDesc, RGTextureAccess::None,
						RGContentValidity::Undefined);
					builder.WriteInPlace(accessChainDepth, RGTextureAccess::DepthStencilWrite);
				});
			accessChainGraph.AddPass<DepthAccessChainPassData>("DepthAccessChain.Sample",
				[&accessChainDepth](RenderGraph::RGBuilder& builder, DepthAccessChainPassData&)
				{
					accessChainDepth = builder.Read(
						accessChainDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
					builder.SideEffect();
				});
			accessChainGraph.AddPass<DepthAccessChainPassData>("DepthAccessChain.Forward",
				[&accessChainDepth](RenderGraph::RGBuilder& builder, DepthAccessChainPassData&)
				{
					accessChainDepth =
						builder.Read(accessChainDepth, RGTextureAccess::DepthStencilRead);
					builder.SideEffect();
				});

			const bool accessChainCompiled = accessChainGraph.Compile();
			bool accessChainMatches = accessChainCompiled;
			if (accessChainCompiled)
			{
				const auto* plan = accessChainGraph.GetExecutionPlan();
				const RHIResourceState writeState =
					ToRHIResourceState(RGTextureAccess::DepthStencilWrite);
				const RHIResourceState sampleState =
					ToRHIResourceState(RGTextureAccess::Sample, RHIStage::ComputeShader);
				const RHIResourceState readState =
					ToRHIResourceState(RGTextureAccess::DepthStencilRead);
				accessChainMatches = plan && plan->GetPasses().size() == 3 &&
					plan->GetPasses()[0].m_PreBarriers.size() == 1 &&
					plan->GetPasses()[1].m_PreBarriers.size() == 1 &&
					plan->GetPasses()[2].m_PreBarriers.size() == 1 &&
					plan->GetPasses()[2].m_PostBarriers.size() == 1;
				if (accessChainMatches)
				{
					const auto& sampleBarrier = plan->GetPasses()[1].m_PreBarriers.front();
					const auto& readBarrier = plan->GetPasses()[2].m_PreBarriers.front();
					accessChainMatches = sampleBarrier.m_Before == writeState &&
						sampleBarrier.m_After == sampleState &&
						readBarrier.m_Before == sampleState &&
						readBarrier.m_After == readState;
				}
			}
			context.Check(accessChainMatches,
				"RenderGraph preserves the depth-write to sample to read-only-depth access chain");
		}

		void RunForwardPlusContractTests(SelfTestContext& context) noexcept
		{
			ForwardPlusSettings legacySettings{};
			legacySettings.m_Mode = ForwardLightingMode::Legacy;
			ForwardPlusSettings forwardPlusSettings{};
			ForwardPlusSettings validationSettings{};
			validationSettings.m_EnableHdrDiffValidation = true;
			context.Check(
				ResolveForwardPBRLightingVariant(
					ForwardPBRPassKind::Opaque, legacySettings, false) ==
				ForwardPBRLightingVariant::Legacy &&
				ResolveForwardPBRLightingVariant(
					ForwardPBRPassKind::Opaque, forwardPlusSettings, false) ==
				ForwardPBRLightingVariant::ForwardPlus &&
				ResolveForwardPBRLightingVariant(
					ForwardPBRPassKind::Opaque, validationSettings, true) ==
				ForwardPBRLightingVariant::ForwardPlusValidation &&
				ResolveForwardPBRLightingVariant(
					ForwardPBRPassKind::Opaque, validationSettings, false) ==
				ForwardPBRLightingVariant::ForwardPlus &&
				ResolveForwardPBRLightingVariant(
					ForwardPBRPassKind::Transparent, validationSettings, true) ==
				ForwardPBRLightingVariant::Legacy,
				"Opaque shading selects Legacy, Forward+, or HDR-diff variants while transparent shading remains Legacy");

			const ForwardPlusHdrDiffReadback withinTolerance{
				.m_MaxAbsoluteError = ForwardPlusHdrDiffAbsoluteTolerance,
				.m_MaxRelativeLuminanceError =
					ForwardPlusHdrDiffRelativeLuminanceTolerance,
				.m_ComparedPixelCount = 1,
				.m_IsValid = true,
			};
			ForwardPlusHdrDiffReadback outsideTolerance = withinTolerance;
			outsideTolerance.m_MaxAbsoluteError =
				std::nextafter(ForwardPlusHdrDiffAbsoluteTolerance,
					std::numeric_limits<float>::infinity());
			context.Check(IsForwardPlusHdrDiffWithinTolerance(withinTolerance) &&
				!IsForwardPlusHdrDiffWithinTolerance(outsideTolerance),
				"Forward+ HDR diff acceptance uses explicit inclusive absolute and relative luminance tolerances");
			context.Check(IsForwardPlusReadbackGenerationCurrent(9, 9) &&
				!IsForwardPlusReadbackGenerationCurrent(8, 9) &&
				!IsForwardPlusReadbackGenerationCurrent(0, 9),
				"Forward+ diagnostics reject stale and uninitialized readback generations");
			context.Check(ShouldPublishForwardPlusReadback(9, 103, 9, 9, 102) &&
				!ShouldPublishForwardPlusReadback(9, 101, 9, 9, 102) &&
				!ShouldPublishForwardPlusReadback(8, 104, 9, 9, 102),
				"Forward+ diagnostics publish only current-generation non-regressing frames");
			context.Check(IsForwardPlusGlobalLightCountSupported(0) &&
				IsForwardPlusGlobalLightCountSupported(ForwardPlusGlobalLightCapacity) &&
				!IsForwardPlusGlobalLightCountSupported(ForwardPlusGlobalLightCapacity + 1),
				"Forward+ uses a bounded global-light loop and fails closed when it overflows");
			std::array<uint32_t, 4> unsortedGlobalLightIndices{ 31, 4, 19, 7 };
			SortForwardPlusGlobalLightIndices(unsortedGlobalLightIndices);
			context.Check(unsortedGlobalLightIndices == std::array<uint32_t, 4>{ 4, 7, 19, 31 },
				"Forward+ establishes a stable ascending global-light order");
			const ForwardPlusTileGrid metricsGrid = MakeForwardPlusTileGrid(32, 16);
			const std::array<ForwardPlusTileHeader, 2> metricHeaders{
				ForwardPlusTileHeader{.m_Offset = 0, .m_CountAndFlags = 0 },
				ForwardPlusTileHeader{.m_Offset = 64, .m_CountAndFlags = 3 },
			};
			const std::array<ForwardPlusTileDepthRange, 2> metricDepthRanges{
				ForwardPlusTileDepthRange{},
				ForwardPlusTileDepthRange{.m_MinViewZ = 2.0f, .m_MaxViewZ = 7.0f },
			};
			const ForwardPlusGridMetrics gridMetrics = BuildForwardPlusGridMetrics(
				metricsGrid, metricHeaders, metricDepthRanges);
			context.Check(gridMetrics.m_IsValid &&
				gridMetrics.m_NonEmptyLightListTileCount == 1 &&
				gridMetrics.m_EmptyLightListTileCount == 1 &&
				gridMetrics.m_TotalLightReferences == 3 &&
				gridMetrics.m_AverageLightsPerTile == 1.5 &&
				gridMetrics.m_MaxLightsPerTile == 3 &&
				gridMetrics.m_OverflowTileCount == 0 && gridMetrics.m_MinViewZ == 2.0f &&
				gridMetrics.m_MaxViewZ == 7.0f,
				"Forward+ full-grid diagnostics aggregate an exact deterministic fixture");

			const ForwardPlusTileGrid grid1080 = MakeForwardPlusTileGrid(1920, 1080);
			const ForwardPlusTileGrid grid4K = MakeForwardPlusTileGrid(3840, 2160);
			context.Check(grid1080.IsValid() && grid1080.m_TileCountX == 120 &&
				grid1080.m_TileCountY == 68 && grid1080.m_TileCount == 8160 &&
				grid4K.IsValid() && grid4K.m_TileCountX == 240 &&
				grid4K.m_TileCountY == 135 && grid4K.m_TileCount == 32400 &&
				!MakeForwardPlusTileGrid(0, 1080).IsValid(),
				"Forward+ tile grids use exact 16x16 ceil-division");

			const ForwardPlusTileHeader header{
				.m_Offset = GetForwardPlusTileOffset(7),
				.m_CountAndFlags = 0xabcd0005u,
			};
			context.Check(GetForwardPlusTileOffset(0) == 0 && GetForwardPlusTileOffset(7) == 448 &&
				header.m_Offset == 448 && header.GetCount() == 5,
				"Forward+ headers preserve fixed-stride address and masked count semantics");

			std::array<uint8_t, ForwardPlusTileLightCapacity> emptyHits{};
			const auto emptyWave32 = BuildStableForwardPlusLightList(emptyHits, 32);
			const auto emptyWave64 = BuildStableForwardPlusLightList(emptyHits, 64);

			auto oneHit = emptyHits;
			oneHit[37] = 1;
			const auto oneWave32 = BuildStableForwardPlusLightList(oneHit, 32);
			const auto oneWave64 = BuildStableForwardPlusLightList(oneHit, 64);

			std::array<uint8_t, ForwardPlusTileLightCapacity> allHits{};
			allHits.fill(1);
			const auto allWave32 = BuildStableForwardPlusLightList(allHits, 32);
			const auto allWave64 = BuildStableForwardPlusLightList(allHits, 64);

			auto patternedHits = emptyHits;
			for (uint32_t lightIndex = 0; lightIndex < ForwardPlusTileLightCapacity; ++lightIndex)
			{
				patternedHits[lightIndex] = (lightIndex % 3u) == 1u || (lightIndex % 11u) == 0u;
			}
			const auto patternedWave32 = BuildStableForwardPlusLightList(patternedHits, 32);
			const auto patternedWave64 = BuildStableForwardPlusLightList(patternedHits, 64);
			context.Check(emptyWave32.empty() && emptyWave32 == emptyWave64 &&
				oneWave32 == std::vector<uint32_t>{37}&& oneWave32 == oneWave64 &&
				allWave32.size() == ForwardPlusTileLightCapacity &&
				allWave32 == allWave64 && allWave32.front() == 0 &&
				allWave32.back() == 63 && patternedWave32 == patternedWave64 &&
				std::ranges::is_sorted(patternedWave32),
				"Forward+ stable compaction preserves global light order for 0, 1, and 64 lights across Wave32 and Wave64 partitions");

			PersistentStructuredBufferTable<uint64_t, uint32_t> fullCapacityTable(2, 1);
			fullCapacityTable.BeginUpdate();
			GGLAB_UNUSED(fullCapacityTable.Upsert(1, 10));
			fullCapacityTable.EndUpdate();
			fullCapacityTable.BeginUpdate();
			const uint32_t firstReplacementSlot = fullCapacityTable.Upsert(2, 20);
			const uint32_t secondReplacementSlot = fullCapacityTable.Upsert(3, 30);
			fullCapacityTable.EndUpdate();
			bool oldKeyRemains = false;
			bool firstKeyPresent = false;
			bool secondKeyPresent = false;
			for (uint32_t slot = 0; slot < 2; ++slot)
			{
				const auto& key = fullCapacityTable.GetKey(slot);
				oldKeyRemains |= key && *key == 1;
				firstKeyPresent |= key && *key == 2;
				secondKeyPresent |= key && *key == 3;
			}
			context.Check(firstReplacementSlot != decltype(fullCapacityTable)::InvalidSlot &&
				secondReplacementSlot != decltype(fullCapacityTable)::InvalidSlot &&
				firstReplacementSlot != secondReplacementSlot &&
				fullCapacityTable.GetLiveCount() == 2 && !oldKeyRemains &&
				firstKeyPresent && secondKeyPresent,
				"Persistent GPU tables can replace a full prior key set without transient capacity overflow");
		}

		void RunDepthCoverageContractTests(SelfTestContext& context) noexcept
		{
			const auto makeVariantBits = [](RenderBucket bucket, bool doubleSided) noexcept
				{
					uint64_t bits = static_cast<uint64_t>(bucket)
						<< RenderQueueBuilder::VariantBit::BucketShift;
					if (doubleSided)
					{
						bits |= RenderQueueBuilder::VariantBit::DoubleSided;
					}
					return bits;
				};

			GraphicsPhysicalPipelineKey basePhysicalKey{};
			basePhysicalKey.m_VSId = ShaderID{ 17 };
			basePhysicalKey.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			basePhysicalKey.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			basePhysicalKey.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			basePhysicalKey.m_Formats.m_SampleCount = 1;
			basePhysicalKey.m_Formats.m_SampleQuality = 0;
			basePhysicalKey.m_RasterizerPreset = RasterizerPreset::Default;
			basePhysicalKey.m_DepthPreset = DepthPreset::ReversedZWrite;
			basePhysicalKey.m_BlendPreset = BlendPreset::Default;

			const auto opaque =
				RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
			const auto opaqueRepeat =
				RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
			const auto alphaTest =
				RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey, makeVariantBits(RenderBucket::AlphaTest, false));
			const auto doubleSided =
				RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey, makeVariantBits(RenderBucket::Opaque, true));
			const auto transparent =
				RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
					basePhysicalKey, makeVariantBits(RenderBucket::Transparent, false));
			GraphicsPhysicalPipelineKey prepassPhysicalKey = basePhysicalKey;
			prepassPhysicalKey.m_Formats.m_RenderTargetCount = 0;
			prepassPhysicalKey.m_BlendPreset = BlendPreset::ColorWriteDisable;
			const auto prepassOpaque =
				RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
					prepassPhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
			const auto prepassAlpha =
				RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
					prepassPhysicalKey, makeVariantBits(RenderBucket::AlphaTest, false));
			context.Check(
				(opaque && opaqueRepeat && alphaTest && doubleSided && prepassOpaque && prepassAlpha) &&
				(*opaque == *opaqueRepeat && *opaque == *prepassOpaque &&
					*alphaTest == *prepassAlpha) &&
				!transparent,
				"Prepass and Forward variants generate matching stable coverage signatures");
			if (!opaque || !alphaTest || !doubleSided || !prepassOpaque || !prepassAlpha)
			{
				return;
			}

			const uint64_t opaqueVariant = makeVariantBits(RenderBucket::Opaque, false);
			const GraphicsLogicalPipelineMetadata forwardMetadata =
				RenderPassForwardOpaque::BuildLogicalPipelineMetadataForVariant(
					basePhysicalKey, opaqueVariant);
			const GraphicsLogicalPipelineMetadata forwardMetadataRepeat =
				RenderPassForwardOpaque::BuildLogicalPipelineMetadataForVariant(
					basePhysicalKey, opaqueVariant);
			const GraphicsLogicalPipelineMetadata prepassMetadata =
				RenderPassDepthPrepass::BuildLogicalPipelineMetadataForVariant(
					prepassPhysicalKey, opaqueVariant);
			context.Check(
				forwardMetadata == forwardMetadataRepeat && forwardMetadata == prepassMetadata,
				"Logical coverage metadata is deterministic before any physical pipeline is resolved");

			DepthCoveragePipelineSignature alphaNormalized = *alphaTest;
			alphaNormalized.m_AlphaVariant = DepthCoverageAlphaVariant::Opaque;
			DepthCoveragePipelineSignature doubleSidedNormalized = *doubleSided;
			doubleSidedNormalized.m_CullMode = opaque->m_CullMode;
			doubleSidedNormalized.m_DoubleSided = false;
			context.Check(alphaNormalized == *opaque && doubleSidedNormalized == *opaque,
				"Coverage variants differ only in their declared pipeline fields");

			const auto differsAfter = [&opaque](auto mutate) noexcept
				{
					DepthCoveragePipelineSignature changed = *opaque;
					mutate(changed);
					return changed != *opaque;
				};
			const bool pipelineFieldsParticipate =
				differsAfter([](auto& value) { value.m_CoverageVertexShader = ShaderID{ 18 }; }) &&
				differsAfter([](auto& value)
					{ value.m_VertexProgram = DepthCoverageVertexProgram::SkinnedMesh; }) &&
				differsAfter([](auto& value)
					{ value.m_Deformation = DepthCoverageDeformationVariant::Skinned; }) &&
				differsAfter([](auto& value) { value.m_InputLayout = InputLayoutID::P3C4; }) &&
				differsAfter([](auto& value)
					{ value.m_PositionPrecision = DepthCoveragePositionPrecision::Float16; }) &&
				differsAfter(
					[](auto& value) { value.m_PositionFormat = RHIFormat::R32G32B32A32Float; }) &&
				differsAfter(
					[](auto& value) { value.m_TopologyType = RHIPrimitiveTopologyType::Line; }) &&
				differsAfter([](auto& value)
					{ value.m_PrimitiveTopology = RHIPrimitiveTopology::LineList; }) &&
				differsAfter([](auto& value) { value.m_FillMode = RHIFillMode::Wireframe; }) &&
				differsAfter([](auto& value) { value.m_CullMode = RHICullMode::Front; }) &&
				differsAfter([](auto& value) { value.m_FrontCounterClockwise = true; }) &&
				differsAfter([](auto& value) { value.m_DepthClipEnable = false; }) &&
				differsAfter([](auto& value) { value.m_DoubleSided = true; }) &&
				differsAfter([](auto& value) { ++value.m_DepthBias; }) &&
				differsAfter([](auto& value) { value.m_DepthBiasClamp = 1.0f; }) &&
				differsAfter([](auto& value) { value.m_SlopeScaledDepthBias = 1.0f; }) &&
				differsAfter([](auto& value) { value.m_SampleCount = 4; }) &&
				differsAfter([](auto& value) { value.m_SampleQuality = 1; }) &&
				differsAfter([](auto& value) { value.m_SampleMask = 0x7FFFFFFFu; }) &&
				differsAfter([](auto& value) { value.m_AlphaToCoverageEnable = true; }) &&
				differsAfter([](auto& value)
					{ value.m_AlphaVariant = DepthCoverageAlphaVariant::BaseColorMask; });
			context.Check(pipelineFieldsParticipate,
				"Every coverage pipeline field participates in signature identity");

			GraphicsPhysicalPipelineKey nonCoveragePhysicalKey = basePhysicalKey;
			nonCoveragePhysicalKey.m_DepthPreset = DepthPreset::ReversedZReadOnly;
			nonCoveragePhysicalKey.m_BlendPreset = BlendPreset::AlphaBlend;
			nonCoveragePhysicalKey.m_Formats.m_RenderTargetFormats[0] = RHIFormat::R8G8B8A8Unorm;
			const auto nonCoverageSignature =
				RenderPassForwardOpaque::BuildDepthCoveragePipelineSignatureForVariant(
					nonCoveragePhysicalKey, makeVariantBits(RenderBucket::Opaque, false));
			context.Check(nonCoverageSignature && *nonCoverageSignature == *opaque,
				"Depth, blend, and render-target state do not change coverage identity");

			const GraphicsLogicalPipelineMetadata opaqueMetadata{
				.m_DepthCoveragePipelineSignature = *opaque,
			};
			const GraphicsLogicalPipelineMetadata alphaMetadata{
				.m_DepthCoveragePipelineSignature = *alphaTest,
			};
			const GraphicsPhysicalPipelineKey opaquePhysicalKey = basePhysicalKey;
			const GraphicsPhysicalPipelineKey alphaPhysicalKey = basePhysicalKey;
			context.Check(opaquePhysicalKey == alphaPhysicalKey && opaqueMetadata != alphaMetadata,
				"Logical coverage metadata changes without changing the physical pipeline key");

			const DepthCoverageRasterDomain rasterDomain{
				.m_FrameSerial = 23,
				.m_ViewBindingId = 1,
				.m_CurrentViewSource =
					{
						.m_Buffer = RHIBufferHandle{4, 5},
						.m_ElementIndex = 29,
					},
				.m_CurrentJitteredProjectionSource =
					{
						.m_Buffer = RHIBufferHandle{4, 5},
						.m_ElementIndex = 29,
					},
				.m_ProjectionSource = DepthCoverageProjectionSource::ViewDataProjection,
				.m_TargetWidth = 1280,
				.m_TargetHeight = 720,
				.m_Viewport =
					{
						.m_Width = 1280.0f,
						.m_Height = 720.0f,
					},
				.m_Scissor =
					{
						.m_Right = 1280,
						.m_Bottom = 720,
					},
				.m_DepthConvention = DepthConvention::Reversed,
			};
			const auto rasterDomainDiffersAfter = [&rasterDomain](auto mutate) noexcept
				{
					DepthCoverageRasterDomain changed = rasterDomain;
					mutate(changed);
					return changed != rasterDomain;
				};
			context.Check(
				rasterDomain.IsValid() &&
				CompareDepthCoverageRasterDomains(rasterDomain, rasterDomain).m_Matches &&
				rasterDomainDiffersAfter([](auto& value) { ++value.m_FrameSerial; }) &&
				rasterDomainDiffersAfter([](auto& value) { ++value.m_ViewBindingId; }) &&
				rasterDomainDiffersAfter([](auto& value)
					{ value.m_CurrentViewSource.m_Buffer = RHIBufferHandle{ 4, 6 }; }) &&
				rasterDomainDiffersAfter([](auto& value)
					{ ++value.m_CurrentJitteredProjectionSource.m_ElementIndex; }) &&
				rasterDomainDiffersAfter(
					[](auto& value)
					{
						value.m_ProjectionSource =
							DepthCoverageProjectionSource::DedicatedJitteredProjection;
					}) &&
				rasterDomainDiffersAfter([](auto& value) { ++value.m_TargetWidth; }) &&
						rasterDomainDiffersAfter([](auto& value) { ++value.m_TargetHeight; }) &&
						rasterDomainDiffersAfter([](auto& value) { value.m_Viewport.m_X = 1.0f; }) &&
						rasterDomainDiffersAfter([](auto& value) { value.m_Viewport.m_Y = 1.0f; }) &&
						rasterDomainDiffersAfter(
							[](auto& value) { value.m_Viewport.m_Width = 1920.0f; }) &&
						rasterDomainDiffersAfter(
							[](auto& value) { value.m_Viewport.m_Height = 1080.0f; }) &&
						rasterDomainDiffersAfter(
							[](auto& value) { value.m_Viewport.m_MinDepth = 0.25f; }) &&
						rasterDomainDiffersAfter(
							[](auto& value) { value.m_Viewport.m_MaxDepth = 0.75f; }) &&
						rasterDomainDiffersAfter([](auto& value) { value.m_Scissor.m_Left = 1; }) &&
						rasterDomainDiffersAfter([](auto& value) { value.m_Scissor.m_Top = 1; }) &&
						rasterDomainDiffersAfter([](auto& value) { value.m_Scissor.m_Right = 1920; }) &&
						rasterDomainDiffersAfter(
							[](auto& value) { value.m_Scissor.m_Bottom = 1080; }) &&
						rasterDomainDiffersAfter(
							[](auto& value) { value.m_DepthConvention = DepthConvention::Standard; }),
						"Raster-domain identity covers the complete view and raster state");

			DepthCoverageRasterDomain undersizedDomain = rasterDomain;
			undersizedDomain.m_TargetWidth = 1279;
			DepthCoverageRasterDomain oversizedViewportDomain = rasterDomain;
			oversizedViewportDomain.m_Viewport.m_Width = 1281.0f;
			const RHITextureDesc coverageColorDesc{
				.m_Format = RHIFormat::R16G16B16A16Float,
				.m_Extent = {1280, 720, 1},
			};
			const RHITextureDesc coverageDepthDesc{
				.m_Format = RHIFormat::R32Typeless,
				.m_Extent = {1280, 720, 1},
			};
			RHITextureDesc mismatchedCoverageDepthDesc = coverageDepthDesc;
			mismatchedCoverageDepthDesc.m_Extent.m_Height = 719;
			context.Check(rasterDomain.MatchesTargetExtent(1280, 720) &&
				!rasterDomain.MatchesTargetExtent(1281, 720) &&
				!undersizedDomain.IsValid() && !oversizedViewportDomain.IsValid() &&
				AreDepthCoverageTargetExtentsCompatible(
					rasterDomain, coverageColorDesc, coverageDepthDesc) &&
				!AreDepthCoverageTargetExtentsCompatible(
					rasterDomain, coverageColorDesc, mismatchedCoverageDepthDesc),
				"Raster domains and target descriptors reject color-depth extent mismatches");

			DepthCoverageRasterDomain changedRasterDomain = rasterDomain;
			++changedRasterDomain.m_FrameSerial;
			changedRasterDomain.m_TargetWidth = 1920;
			changedRasterDomain.m_Viewport.m_Width = 1920.0f;
			const auto pipelineComparison =
				CompareDepthCoveragePipelineSignatures(*opaque, *alphaTest);
			const auto rasterComparison =
				CompareDepthCoverageRasterDomains(rasterDomain, changedRasterDomain);
			context.Check(
				!pipelineComparison.m_Matches &&
				pipelineComparison.m_Mismatch.find("AlphaVariant") != std::string::npos &&
				!rasterComparison.m_Matches &&
				rasterComparison.m_Mismatch.find("FrameSerial") != std::string::npos &&
				rasterComparison.m_Mismatch.find("Viewport") != std::string::npos &&
				DescribeDepthCoveragePipelineSignature(*opaque).find("InputLayout") !=
				std::string::npos &&
				DescribeDepthCoverageRasterDomain(rasterDomain).find("Scissor") !=
				std::string::npos,
				"Coverage diagnostics identify pipeline and raster-domain mismatches");

			const auto invalidPipelineComparison = CompareDepthCoveragePipelineSignatures(
				DepthCoveragePipelineSignature{}, DepthCoveragePipelineSignature{});
			DepthCoverageRasterDomain invalidRhsDomain = rasterDomain;
			invalidRhsDomain.m_FrameSerial = 0;
			const auto invalidRasterComparison =
				CompareDepthCoverageRasterDomains(rasterDomain, invalidRhsDomain);
			context.Check(!invalidPipelineComparison.m_Matches &&
				invalidPipelineComparison.m_Mismatch.find("Pipeline.LhsInvalid") !=
				std::string::npos &&
				!invalidRasterComparison.m_Matches &&
				invalidRasterComparison.m_Mismatch.find("RasterDomain.RhsInvalid") !=
				std::string::npos,
				"Coverage comparison fails closed for invalid contract domains");

			const DepthCoverageDrawPacket packet{
				.m_Geometry =
					{
						.m_MeshId = ProceduralCubeMeshID,
						.m_VertexBuffer =
							{
								.m_Buffer = RHIBufferHandle{8, 9},
								.m_Offset = 64,
								.m_Stride = 48,
								.m_SizeInBytes = 480,
							},
						.m_IndexBuffer =
							{
								.m_Buffer = RHIBufferHandle{10, 11},
								.m_Offset = 32,
								.m_SizeInBytes = 72,
								.m_Format = RHIFormat::R32Uint,
							},
					},
				.m_IndexedDraw =
					{
						.m_IndexCount = 18,
						.m_InstanceCount = 2,
						.m_StartIndexLocation = 3,
						.m_BaseVertexLocation = -2,
						.m_StartInstanceLocation = 4,
					},
				.m_DrawParameters =
					{
						.ObjectOffset = 17,
					},
				.m_CurrentModelSource =
					{
						.m_Buffer = RHIBufferHandle{2, 3},
						.m_ElementIndex = 17,
					},
				.m_MaterialAlphaSource =
					{
						.m_Buffer = RHIBufferHandle{6, 7},
						.m_ElementIndex = 31,
					},
			};
			const auto packetDiffersAfter = [&packet](auto mutate) noexcept
				{
					DepthCoverageDrawPacket changed = packet;
					mutate(changed);
					return changed != packet;
				};
			RenderQueue queue{
				.m_DrawItems =
					{
						DrawItem{
							.m_CoverageDrawPacket = packet,
							.m_Bucket = RenderBucket::Opaque,
							.m_VariantBits = opaqueVariant,
						},
					},
				.m_ViewId = RenderViewID::Main,
				.m_CoverageRasterDomain = rasterDomain,
			};
			queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::Opaque)] = {
				.m_Start = 0,
				.m_Count = 1,
			};
			queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::AlphaTest)] = {
				.m_Start = 1,
			};
			queue.m_BucketDrawRanges[utils::ToIndex(RenderBucket::Transparent)] = {
				.m_Start = 1,
			};
			const auto& prepassPacket = queue.m_DrawItems.front().m_CoverageDrawPacket;
			const auto& forwardPacket = queue.m_DrawItems.front().m_CoverageDrawPacket;
			const DepthCoverageDrawPacket copiedPacket = packet;
			context.Check(
				packet.IsValid() && IsSameDepthCoverageDrawPacket(prepassPacket, forwardPacket) &&
				!IsSameDepthCoverageDrawPacket(packet, copiedPacket) &&
				packetDiffersAfter(
					[](auto& value) { value.m_Geometry.m_MeshId = ProceduralSphereMeshID; }) &&
				packetDiffersAfter([](auto& value)
					{ value.m_Geometry.m_VertexBuffer.m_Buffer = RHIBufferHandle{ 8, 10 }; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_Geometry.m_VertexBuffer.m_Offset; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_Geometry.m_VertexBuffer.m_Stride; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_Geometry.m_VertexBuffer.m_SizeInBytes; }) &&
				packetDiffersAfter([](auto& value)
					{ value.m_Geometry.m_IndexBuffer.m_Buffer = RHIBufferHandle{ 10, 12 }; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_Geometry.m_IndexBuffer.m_Offset; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_Geometry.m_IndexBuffer.m_SizeInBytes; }) &&
				packetDiffersAfter([](auto& value)
					{ value.m_Geometry.m_IndexBuffer.m_Format = RHIFormat::R32Float; }) &&
				packetDiffersAfter([](auto& value) { ++value.m_IndexedDraw.m_IndexCount; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_IndexedDraw.m_InstanceCount; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_IndexedDraw.m_StartIndexLocation; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_IndexedDraw.m_BaseVertexLocation; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_IndexedDraw.m_StartInstanceLocation; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_DrawParameters.ObjectOffset; }) &&
				packetDiffersAfter([](auto& value)
					{ value.m_CurrentModelSource.m_Buffer = RHIBufferHandle{ 2, 4 }; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_CurrentModelSource.m_ElementIndex; }) &&
				packetDiffersAfter([](auto& value)
					{ value.m_MaterialAlphaSource.m_Buffer = RHIBufferHandle{ 6, 8 }; }) &&
				packetDiffersAfter(
					[](auto& value) { ++value.m_MaterialAlphaSource.m_ElementIndex; }) &&
				DescribeDepthCoverageDrawPacket(packet).find("DrawIndexed") !=
				std::string::npos &&
				DescribeDepthCoverageDrawPacket(packet).find("MaterialAlpha") !=
				std::string::npos,
				"RenderQueue owns one complete coverage draw packet shared by both passes");

			DepthCoverageFramePlanBuildInfo framePlanBuildInfo{
				.m_RenderQueue = std::addressof(queue),
				.m_ExpectedViewId = RenderViewID::Main,
				.m_TargetWidth = 1280,
				.m_TargetHeight = 720,
				.m_DepthConvention = DepthConvention::Reversed,
			};
			framePlanBuildInfo.m_PrepassPipelineSignatures[opaqueVariant] = *prepassOpaque;
			framePlanBuildInfo.m_ForwardPipelineSignatures[opaqueVariant] = *opaque;
			const DepthCoverageFramePlan equalPlan =
				BuildDepthCoverageFramePlan(framePlanBuildInfo);

			DepthCoverageFramePlanBuildInfo mismatchedShaderBuildInfo = framePlanBuildInfo;
			mismatchedShaderBuildInfo.m_ForwardPipelineSignatures[opaqueVariant]
				->m_CoverageVertexShader = ShaderID{ 18 };
			const DepthCoverageFramePlan fallbackPlan =
				BuildDepthCoverageFramePlan(mismatchedShaderBuildInfo);

			RenderQueue invalidPacketQueue = queue;
			invalidPacketQueue.m_DrawItems.front().m_CoverageDrawPacket = {};
			DepthCoverageFramePlanBuildInfo invalidPacketBuildInfo = framePlanBuildInfo;
			invalidPacketBuildInfo.m_RenderQueue = std::addressof(invalidPacketQueue);
			const DepthCoverageFramePlan rejectedPlan =
				BuildDepthCoverageFramePlan(invalidPacketBuildInfo);

			DepthCoverageFramePlanBuildInfo standardDepthBuildInfo = framePlanBuildInfo;
			standardDepthBuildInfo.m_DepthConvention = DepthConvention::Standard;
			const DepthCoverageFramePlan standardDepthPlan =
				BuildDepthCoverageFramePlan(standardDepthBuildInfo);

			RenderQueue emptyQueue{
				.m_ViewId = RenderViewID::Main,
				.m_CoverageRasterDomain = rasterDomain,
			};
			DepthCoverageFramePlanBuildInfo emptyQueueBuildInfo = framePlanBuildInfo;
			emptyQueueBuildInfo.m_RenderQueue = std::addressof(emptyQueue);
			const DepthCoverageFramePlan emptyQueuePlan =
				BuildDepthCoverageFramePlan(emptyQueueBuildInfo);
			context.Check(
				equalPlan.UsesDepthPrepassEqual() && equalPlan.RendersGeometry() &&
				equalPlan.AddsForwardOpaquePass() && !equalPlan.AddsForwardTransparentPass() &&
				fallbackPlan.UsesForwardDepthWrite() && fallbackPlan.RendersGeometry() &&
				fallbackPlan.m_Diagnostic.find("CoverageVertexShader") != std::string::npos &&
				!rejectedPlan.RendersGeometry() &&
				rejectedPlan.m_ExecutionMode == DepthCoverageExecutionMode::SkipGeometry &&
				!standardDepthPlan.RendersGeometry() &&
				standardDepthPlan.m_Diagnostic.find("Reversed-Z") != std::string::npos &&
				emptyQueuePlan.UsesDepthPrepassEqual() &&
				!emptyQueuePlan.AddsForwardOpaquePass() &&
				!emptyQueuePlan.AddsForwardTransparentPass(),
				"Frame-level coverage planning selects one consistent EQUAL, Forward-write, or reject path and omits empty Forward buckets");
		}

		void RunScreenSpaceAndDepthContractTests(SelfTestContext& context) noexcept
		{
			RunProjectionConventionTests(context);
			RunPositionReconstructionTests(context);
			RunScreenCoordinateTests(context);
			RunRenderViewConventionTests(context);
			RunSampleableDepthFormatTests(context);
			RunDepthCoverageContractTests(context);
			RunForwardPlusContractTests(context);

			context.Check(MakeGTAOHalfResolutionExtent(1920, 1080) == GTAOExtent{ 960, 540 } &&
				MakeGTAOHalfResolutionExtent(1919, 1079) == GTAOExtent{ 960, 540 } &&
				!MakeGTAOHalfResolutionExtent(0, 0).IsValid(),
				"GTAO half-resolution extents use deterministic per-axis ceiling division");

			const std::array<GTAOSurfaceCandidate, 4> reversedCandidates = {
				GTAOSurfaceCandidate{.m_RawDepth = 0.0f, .m_ViewZ = 0.0f},
				GTAOSurfaceCandidate{.m_RawDepth = 0.4f, .m_ViewZ = 3.0f},
				GTAOSurfaceCandidate{.m_RawDepth = 0.7f, .m_ViewZ = 2.0f},
				GTAOSurfaceCandidate{.m_RawDepth = 0.7f, .m_ViewZ = 2.0f},
			};
			const GTAOSurfaceSelection reversedSelection = SelectGTAOHalfResolutionSurface(
				reversedCandidates, DepthConvention::Reversed);
			const std::array<GTAOSurfaceCandidate, 4> standardTieCandidates = {
				GTAOSurfaceCandidate{.m_RawDepth = 0.25f, .m_ViewZ = 2.0f},
				GTAOSurfaceCandidate{.m_RawDepth = 0.25f, .m_ViewZ = 2.0f},
				GTAOSurfaceCandidate{.m_RawDepth = 0.5f, .m_ViewZ = 4.0f},
				GTAOSurfaceCandidate{.m_RawDepth = 1.0f, .m_ViewZ = 0.0f},
			};
			const GTAOSurfaceSelection standardSelection = SelectGTAOHalfResolutionSurface(
				standardTieCandidates, DepthConvention::Standard);
			const std::array<GTAOSurfaceCandidate, 4> backgroundCandidates{};
			const GTAOSurfaceSelection backgroundSelection = SelectGTAOHalfResolutionSurface(
				backgroundCandidates, DepthConvention::Reversed);
			context.Check(reversedSelection.m_IsValid &&
				reversedSelection.m_SelectedIndex == 2 && reversedSelection.m_RawDepth == 0.7f &&
				standardSelection.m_IsValid && standardSelection.m_SelectedIndex == 0 &&
				!backgroundSelection.m_IsValid && backgroundSelection.m_RawDepth == 0.0f &&
				backgroundSelection.m_ViewZ == 0.0f,
				"GTAO surface selection rejects background, chooses nearest depth, and preserves TL-to-BR ties");

			const float noise = GTAOInterleavedGradientNoise(37, 19);
			context.Check(noise >= 0.0f && noise < 1.0f &&
				noise == GTAOInterleavedGradientNoise(37, 19),
				"GTAO interleaved-gradient noise is finite, normalized, and deterministic");

			const auto leftBoundaryNeighbors = GetGTAONormalAxisNeighborAvailability(0, 8);
			const auto interiorNeighbors = GetGTAONormalAxisNeighborAvailability(3, 8);
			const auto rightBoundaryNeighbors = GetGTAONormalAxisNeighborAvailability(7, 8);
			const auto singlePixelNeighbors = GetGTAONormalAxisNeighborAvailability(0, 1);
			context.Check(!leftBoundaryNeighbors.m_HasNegativeNeighbor &&
				leftBoundaryNeighbors.m_HasPositiveNeighbor &&
				interiorNeighbors.m_HasNegativeNeighbor &&
				interiorNeighbors.m_HasPositiveNeighbor &&
				rightBoundaryNeighbors.m_HasNegativeNeighbor &&
				!rightBoundaryNeighbors.m_HasPositiveNeighbor &&
				!singlePixelNeighbors.m_HasNegativeNeighbor &&
				!singlePixelNeighbors.m_HasPositiveNeighbor,
				"GTAO normal reconstruction never substitutes the center pixel for a missing edge neighbor");

			const RHITextureSupportResult supportedRequirement{ .m_Supported = true };
			const RHITextureSupportResult unsupportedTypedStore{
				.m_Reason = RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported,
				.m_Supported = false,
			};
			const RHITextureSupportResult unsupportedShaderResource{
				.m_Reason = RHITextureSupportReason::ShaderResourceUnsupported,
				.m_Supported = false,
			};
			const GTAOSurfaceFormatSupport supportedFormat{
				.m_ShaderResource = supportedRequirement,
				.m_TypedUavStore = supportedRequirement,
			};
			const GTAOSurfaceFormatSupport unsupportedR8Store{
				.m_ShaderResource = supportedRequirement,
				.m_TypedUavStore = unsupportedTypedStore,
			};
			const GTAOSurfaceFormatSupport unsupportedR8Srv{
				.m_ShaderResource = unsupportedShaderResource,
				.m_TypedUavStore = supportedRequirement,
			};
			const GTAOFinalAOFormatResolution preferredFinalAO =
				ResolveGTAOFinalAOFormat(supportedFormat, supportedFormat);
			const GTAOFinalAOFormatResolution storeFallbackFinalAO =
				ResolveGTAOFinalAOFormat(unsupportedR8Store, supportedFormat);
			const GTAOFinalAOFormatResolution srvFallbackFinalAO =
				ResolveGTAOFinalAOFormat(unsupportedR8Srv, supportedFormat);
			const GTAOFinalAOFormatResolution unavailableFinalAO =
				ResolveGTAOFinalAOFormat(unsupportedR8Store, unsupportedR8Srv);
			context.Check(preferredFinalAO.m_Format == RHIFormat::R8Unorm &&
				!preferredFinalAO.UsesFallback() &&
				storeFallbackFinalAO.m_Format == RHIFormat::R16Float &&
				storeFallbackFinalAO.UsesFallback() &&
				storeFallbackFinalAO.m_PreferredR8Unorm.m_TypedUavStore.m_Reason ==
				RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported &&
				srvFallbackFinalAO.m_Format == RHIFormat::R16Float &&
				srvFallbackFinalAO.m_PreferredR8Unorm.m_ShaderResource.m_Reason ==
				RHITextureSupportReason::ShaderResourceUnsupported &&
				!unavailableFinalAO.IsAvailable(),
				"GTAO FinalAO requires both SRV and typed UAV store support and preserves failures");

			context.Check(ResolveGTAODiffuseIBLVisibility(0.5f, 0.25f) == 0.125f &&
				ResolveGTAOSpecularIBLVisibility(0.5f) == 0.5f &&
				ResolveGTAODiffuseIBLVisibility(0.5f, 1.0f) == 0.5f,
				"GTAO modulates material-occluded diffuse IBL without changing specular IBL visibility");

			context.Check(
				ResolveGTAOFrameStatus(false, false, false, false, false, false) ==
				GTAOFrameStatus::Disabled &&
				ResolveGTAOFrameStatus(true, false, false, false, false, false) ==
				GTAOFrameStatus::CoreCapabilityUnavailable &&
				ResolveGTAOFrameStatus(true, true, false, false, false, false) ==
				GTAOFrameStatus::PipelineUnavailable &&
				ResolveGTAOFrameStatus(true, true, true, false, false, false) ==
				GTAOFrameStatus::RenderSceneUnavailable &&
				ResolveGTAOFrameStatus(true, true, true, true, false, false) ==
				GTAOFrameStatus::DepthCoverageUnavailable &&
				ResolveGTAOFrameStatus(true, true, true, true, true, false) ==
				GTAOFrameStatus::NoOpaqueDraws &&
				ResolveGTAOFrameStatus(true, true, true, true, true, true) ==
				GTAOFrameStatus::Active,
				"GTAO frame status preserves deterministic disabled, fallback, idle, and active causes");

			ViewRenderProfile gtaoProfile{};
			context.Check(gtaoProfile.m_Lighting.m_GTAO.m_Enabled,
				"GTAO is enabled by the active-view authoring default");
			gtaoProfile.m_Lighting.m_GTAO.m_Radius = -1.0f;
			gtaoProfile.m_Lighting.m_GTAO.m_FalloffStart = 99.0f;
			gtaoProfile.m_Lighting.m_GTAO.m_FalloffEnd = -99.0f;
			gtaoProfile.m_Lighting.m_GTAO.m_Thickness = 99.0f;
			gtaoProfile.m_Lighting.m_GTAO.m_Power = 99.0f;
			gtaoProfile.m_Lighting.m_GTAO.m_DirectionCount = 0;
			gtaoProfile.m_Lighting.m_GTAO.m_StepCount = 99;
			gtaoProfile.m_Lighting.m_GTAO.m_DenoiseRadius = 99;
			const Camera gtaoCamera(Camera::CreateInfo{});
			const GTAOSettings resolvedGTAO =
				ResolveViewRenderSettings(gtaoProfile, gtaoCamera).m_Lighting.m_GTAO;
			context.Check(resolvedGTAO.m_Radius == 0.01f &&
				resolvedGTAO.m_FalloffStart == resolvedGTAO.m_Radius &&
				resolvedGTAO.m_FalloffEnd == resolvedGTAO.m_Radius &&
				resolvedGTAO.m_Thickness == resolvedGTAO.m_Radius &&
				resolvedGTAO.m_Power == 8.0f &&
				resolvedGTAO.m_DirectionCount == 1 &&
				resolvedGTAO.m_StepCount == GTAOMaxStepCount &&
				resolvedGTAO.m_DenoiseRadius == GTAOMaxDenoiseRadius,
				"GTAO authoring inputs resolve to bounded deterministic spatial settings");
		}

		void RunTextureFormatCapabilityTests(SelfTestContext& context) noexcept
		{
			RHITextureDesc desc{};
			desc.m_Format = RHIFormat::R8Unorm;
			desc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::RenderTarget |
				RHITextureUsage::UnorderedAccess;
			const auto fullSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(
				D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
				D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
				D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
			const auto noTypedUavSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(
				fullSupport1 & ~D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
			const auto noShaderResourceSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(
				fullSupport1 & ~D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
			const auto noRenderTargetSupport1 = static_cast<D3D12_FORMAT_SUPPORT1>(
				fullSupport1 & ~D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
			constexpr auto TypedStoreSupport =
				static_cast<D3D12_FORMAT_SUPPORT2>(D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);

			RHITextureViewDesc viewDesc{
				.m_Type = RHITextureViewType::UnorderedAccess,
				.m_Dimension = RHITextureViewDimension::Texture2D,
				.m_Format = RHIFormat::R8Unorm,
			};
			context.Check(EvaluateD3D12TextureFormatSupport(desc, fullSupport1) ==
				RHITextureSupportReason::None &&
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, fullSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::None,
				"R8Unorm accepts the complete GTAO texture and typed-UAV capability set");
			context.Check(EvaluateD3D12TextureFormatSupport(desc, noTypedUavSupport1) ==
				RHITextureSupportReason::TypedUnorderedAccessUnsupported &&
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, noTypedUavSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::TypedUnorderedAccessUnsupported &&
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, fullSupport1, D3D12_FORMAT_SUPPORT2_NONE) ==
				RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported,
				"R8Unorm capability queries distinguish typed-UAV and typed-store fallback causes");

			viewDesc.m_Type = RHITextureViewType::ShaderResource;
			context.Check(EvaluateD3D12TextureViewFormatSupport(
				desc, viewDesc, noShaderResourceSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::ShaderResourceUnsupported,
				"Single-channel SRV capability validation rejects formats without shader access");
			viewDesc.m_Type = RHITextureViewType::RenderTarget;
			context.Check(
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, fullSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::None &&
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, noRenderTargetSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::RenderTargetUnsupported,
				"Single-channel RTV capability validation requires render-target support");

			desc.m_Format = RHIFormat::R16Float;
			viewDesc.m_Format = RHIFormat::R16Float;
			viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
			const bool r16Supported = EvaluateD3D12TextureFormatSupport(desc, fullSupport1) ==
				RHITextureSupportReason::None &&
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, fullSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::None;
			desc.m_Format = RHIFormat::R32Float;
			viewDesc.m_Format = RHIFormat::R32Float;
			context.Check(r16Supported &&
				EvaluateD3D12TextureFormatSupport(desc, fullSupport1) ==
				RHITextureSupportReason::None &&
				EvaluateD3D12TextureViewFormatSupport(
					desc, viewDesc, fullSupport1, TypedStoreSupport) ==
				RHITextureSupportReason::None,
				"R16Float and existing R32Float use the same typed-UAV capability contract");
			context.Check(RHITextureSupportReasonText(
				RHITextureSupportReason::TypedUnorderedAccessStoreUnsupported) ==
				"typed unordered-access store unsupported",
				"Texture capability failures expose a stable diagnostic reason");
			context.Check(
				RHITextureSupportReasonForUsage(RHITextureUsage::Sampled) ==
				RHITextureSupportReason::ShaderResourceUnsupported &&
				RHITextureSupportReasonForUsage(RHITextureUsage::CopyDest) ==
				RHITextureSupportReason::CopyDestUnsupported &&
				RHITextureSupportReasonForUsage(RHITextureUsage::None) ==
				RHITextureSupportReason::FormatCombinationUnsupported,
				"Texture usage failures map to backend-neutral support reasons");
		}

		[[nodiscard]] bool HasDependencyEdge(const RGSnapshot& snapshot, uint32_t fromPass,
			uint32_t toPass, RGDependencyReason reason) noexcept
		{
			return std::ranges::any_of(snapshot.m_DependencyEdges,
				[=](const RGSnapshotDependencyEdge& edge)
				{
					return edge.m_FromPassIndex == static_cast<int32_t>(fromPass) &&
						edge.m_ToPassIndex == static_cast<int32_t>(toPass) &&
						edge.m_Reason == reason;
				});
		}

		void RunGTAORenderGraphDataflowTests(SelfTestContext& context) noexcept
		{
			RenderGraph graph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			const RHITextureDesc halfAODesc{
				.m_Format = RHIFormat::R16Float,
				.m_Extent = { 640, 360, 1 },
			};
			RHITextureDesc halfDepthDesc = halfAODesc;
			halfDepthDesc.m_Format = RHIFormat::R32Float;
			const RHITextureDesc finalAODesc{
				.m_Format = RHIFormat::R8Unorm,
				.m_Extent = { 1280, 720, 1 },
			};

			RGTextureId rawAO;
			RGTextureId halfDepth;
			RGTextureId denoiseX;
			RGTextureId denoiseY;
			RGTextureId finalAO;
			graph.AddPass<GTAODataflowPassData>("Lighting.GTAO", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
				{
					rawAO = builder.CreateTexture("GTAO.RawAO", halfAODesc);
					halfDepth = builder.CreateTexture("GTAO.HalfDepthViewZ", halfDepthDesc);
					builder.WriteInPlace(
						rawAO, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					builder.WriteInPlace(
						halfDepth, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Views[0] =
						builder.CreateView<RHITextureViewType::UnorderedAccess>(rawAO);
					data.m_Views[1] =
						builder.CreateView<RHITextureViewType::UnorderedAccess>(halfDepth);
				});
			graph.AddPass<GTAODataflowPassData>(
				"Lighting.GTAO.DenoiseX", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
				{
					const RGTextureId rawAOSrv = builder.Read(
						rawAO, RGTextureAccess::Sample, RHIStage::ComputeShader);
					const RGTextureId halfDepthSrv = builder.Read(
						halfDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
					denoiseX = builder.CreateTexture("GTAO.DenoiseX", halfAODesc);
					builder.WriteInPlace(
						denoiseX, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Views[0] =
						builder.CreateView<RHITextureViewType::ShaderResource>(rawAOSrv);
					data.m_Views[1] =
						builder.CreateView<RHITextureViewType::ShaderResource>(halfDepthSrv);
					data.m_Views[2] =
						builder.CreateView<RHITextureViewType::UnorderedAccess>(denoiseX);
				});
			graph.AddPass<GTAODataflowPassData>(
				"Lighting.GTAO.DenoiseY", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
				{
					const RGTextureId denoiseXSrv = builder.Read(
						denoiseX, RGTextureAccess::Sample, RHIStage::ComputeShader);
					const RGTextureId halfDepthSrv = builder.Read(
						halfDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
					denoiseY = builder.CreateTexture("GTAO.DenoiseY", halfAODesc);
					builder.WriteInPlace(
						denoiseY, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Views[0] =
						builder.CreateView<RHITextureViewType::ShaderResource>(denoiseXSrv);
					data.m_Views[1] =
						builder.CreateView<RHITextureViewType::ShaderResource>(halfDepthSrv);
					data.m_Views[2] =
						builder.CreateView<RHITextureViewType::UnorderedAccess>(denoiseY);
				});
			graph.AddPass<GTAODataflowPassData>(
				"Lighting.GTAO.Upsample", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
				{
					const RGTextureId denoiseYSrv = builder.Read(
						denoiseY, RGTextureAccess::Sample, RHIStage::ComputeShader);
					const RGTextureId halfDepthSrv = builder.Read(
						halfDepth, RGTextureAccess::Sample, RHIStage::ComputeShader);
					finalAO = builder.CreateTexture("GTAO.FinalAO", finalAODesc);
					builder.WriteInPlace(
						finalAO, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Views[0] =
						builder.CreateView<RHITextureViewType::ShaderResource>(denoiseYSrv);
					data.m_Views[1] =
						builder.CreateView<RHITextureViewType::ShaderResource>(halfDepthSrv);
					data.m_Views[2] =
						builder.CreateView<RHITextureViewType::UnorderedAccess>(finalAO);
				});
			graph.AddPass<GTAODataflowPassData>("Geometry.ForwardPBR.Opaque",
				[&](RenderGraph::RGBuilder& builder, GTAODataflowPassData& data)
				{
					const RGTextureId finalAOSrv = builder.Read(
						finalAO, RGTextureAccess::Sample, RHIStage::PixelShader);
					data.m_Views[0] =
						builder.CreateView<RHITextureViewType::ShaderResource>(finalAOSrv);
					builder.SideEffect();
				});

			const bool compiled = graph.Compile();
			context.Check(compiled, "GTAO production RenderGraph dataflow fixture compiles");
			if (!compiled)
			{
				return;
			}

			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const bool allPassesLive = snapshot.m_Passes.size() == 5 &&
				std::ranges::none_of(snapshot.m_Passes,
					[](const RGSnapshotPassInfo& pass) noexcept { return pass.m_Culled; });
			context.Check(allPassesLive &&
				HasDependencyEdge(snapshot, 0, 1, RGDependencyReason::WriterToReader) &&
				HasDependencyEdge(snapshot, 1, 2, RGDependencyReason::WriterToReader) &&
				HasDependencyEdge(snapshot, 2, 3, RGDependencyReason::WriterToReader) &&
				HasDependencyEdge(snapshot, 3, 4, RGDependencyReason::WriterToReader),
				"GTAO evaluate, denoise, upsample, and opaque consumption remain one live chain");

			const auto& opaqueBarriers = snapshot.m_Passes[4].m_PreBarriers;
			context.Check(opaqueBarriers.size() == 1 &&
				opaqueBarriers[0].m_ResourceName == "GTAO.FinalAO" &&
				opaqueBarriers[0].m_Kind == RGBarrierKind::Transition &&
				opaqueBarriers[0].m_Reason == RGBarrierReason::AccessTransition &&
				opaqueBarriers[0].m_Before.m_Stages == RHIStage::ComputeShader &&
				opaqueBarriers[0].m_Before.m_Access == RHIAccess::UnorderedAccess &&
				opaqueBarriers[0].m_After.m_Stages == RHIStage::PixelShader &&
				opaqueBarriers[0].m_After.m_Access == RHIAccess::ShaderResource,
				"GTAO FinalAO transitions exactly from compute UAV-write to opaque pixel SRV-read");
		}

		void RunRenderGraphAccessAndBarrierContractTests(SelfTestContext& context) noexcept
		{
			struct TextureCompatibilityRow
			{
				RGTextureAccess m_Access;
				std::array<bool, 3> m_DependencyAccesses;
			};
			constexpr std::array TextureCompatibility = {
				TextureCompatibilityRow{RGTextureAccess::None, {false, false, false}},
				TextureCompatibilityRow{RGTextureAccess::Sample, {true, false, false}},
				TextureCompatibilityRow{RGTextureAccess::RenderTarget, {false, true, true}},
				TextureCompatibilityRow{RGTextureAccess::DepthStencilWrite, {false, true, true}},
				TextureCompatibilityRow{RGTextureAccess::DepthStencilRead, {true, false, false}},
				TextureCompatibilityRow{RGTextureAccess::StorageRead, {true, false, false}},
				TextureCompatibilityRow{RGTextureAccess::StorageWrite, {false, true, false}},
				TextureCompatibilityRow{RGTextureAccess::StorageReadWrite, {false, false, true}},
				TextureCompatibilityRow{RGTextureAccess::CopySource, {true, false, false}},
				TextureCompatibilityRow{RGTextureAccess::CopyDest, {false, true, false}},
				TextureCompatibilityRow{RGTextureAccess::Present, {false, false, false}},
			};
			struct BufferCompatibilityRow
			{
				RGBufferAccess m_Access;
				std::array<bool, 3> m_DependencyAccesses;
			};
			constexpr std::array BufferCompatibility = {
				BufferCompatibilityRow{RGBufferAccess::None, {false, false, false}},
				BufferCompatibilityRow{RGBufferAccess::Vertex, {true, false, false}},
				BufferCompatibilityRow{RGBufferAccess::Index, {true, false, false}},
				BufferCompatibilityRow{RGBufferAccess::Constant, {true, false, false}},
				BufferCompatibilityRow{RGBufferAccess::StructuredRead, {true, false, false}},
				BufferCompatibilityRow{RGBufferAccess::StorageRead, {true, false, false}},
				BufferCompatibilityRow{RGBufferAccess::StorageWrite, {false, true, false}},
				BufferCompatibilityRow{RGBufferAccess::StorageReadWrite, {false, false, true}},
				BufferCompatibilityRow{RGBufferAccess::CopySource, {true, false, false}},
				BufferCompatibilityRow{RGBufferAccess::CopyDest, {false, true, false}},
				BufferCompatibilityRow{RGBufferAccess::IndirectArgument, {true, false, false}},
			};
			constexpr std::array DependencyAccesses = {
				RGDependencyAccess::Read,
				RGDependencyAccess::Write,
				RGDependencyAccess::ReadWrite,
			};
			static_assert(
				TextureCompatibility.size() == static_cast<size_t>(RGTextureAccess::Present) + 1);
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
							row.m_Access, dependencyAccess, RGOrderingRequirement::Ordered) ==
						row.m_DependencyAccesses[accessIndex];
					completeCompatibilityMatrixMatches &=
						IsRGAccessCompatible(
							row.m_Access, dependencyAccess, RGOrderingRequirement::Unordered) ==
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
							row.m_Access, dependencyAccess, RGOrderingRequirement::Ordered) ==
						row.m_DependencyAccesses[accessIndex];
					completeCompatibilityMatrixMatches &=
						IsRGAccessCompatible(
							row.m_Access, dependencyAccess, RGOrderingRequirement::Unordered) ==
						(row.m_DependencyAccesses[accessIndex] && IsStorageAccess(row.m_Access));
				}
			}
			context.Check(completeCompatibilityMatrixMatches,
				"RenderGraph validates the complete texture and buffer access compatibility matrix");

			bool orderedUavMatrixMatches = true;
			for (const auto beforeAccess : DependencyAccesses)
			{
				for (const auto afterAccess : DependencyAccesses)
				{
					const bool expected = beforeAccess != RGDependencyAccess::Read ||
						afterAccess != RGDependencyAccess::Read;
					orderedUavMatrixMatches &=
						NeedsOrderedUavBarrier(beforeAccess, RGOrderingRequirement::Ordered,
							afterAccess, RGOrderingRequirement::Ordered) == expected;
					orderedUavMatrixMatches &=
						!NeedsOrderedUavBarrier(beforeAccess, RGOrderingRequirement::Unordered,
							afterAccess, RGOrderingRequirement::Ordered);
					orderedUavMatrixMatches &=
						!NeedsOrderedUavBarrier(beforeAccess, RGOrderingRequirement::Ordered,
							afterAccess, RGOrderingRequirement::Unordered);
				}
			}
			context.Check(orderedUavMatrixMatches,
				"Ordered UAV hazards cover the complete read, write, and read-write matrix");
			const RHIResourceState pixelReadState =
				ToRHIResourceState(RGBufferAccess::StructuredRead, RHIStage::PixelShader);
			const RHIResourceState computeReadState =
				ToRHIResourceState(RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
			context.Check(!NeedsRHIResourceTransition(pixelReadState, computeReadState),
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
				ToRHIUsage(RGTextureAccess::StorageRead) == RHITextureUsage::UnorderedAccess &&
				ToRHIUsage(RGTextureAccess::StorageWrite) == RHITextureUsage::UnorderedAccess &&
				ToRHIUsage(RGTextureAccess::StorageReadWrite) ==
				RHITextureUsage::UnorderedAccess &&
				ToRHIUsage(RGBufferAccess::StorageRead) == RHIBufferUsage::UnorderedAccess &&
				ToRHIUsage(RGBufferAccess::StorageWrite) == RHIBufferUsage::UnorderedAccess &&
				ToRHIUsage(RGBufferAccess::StorageReadWrite) ==
				RHIBufferUsage::UnorderedAccess &&
				HasUavAccess(textureStorageState) && HasUavAccess(bufferStorageState),
				"Storage read, write, and read-write accesses map to UAV resource states");
			context.Check(
				IsRGAccessCompatible(RGTextureAccess::StorageRead, RGDependencyAccess::Read,
					RGOrderingRequirement::Ordered) &&
				IsRGAccessCompatible(RGTextureAccess::StorageWrite, RGDependencyAccess::Write,
					RGOrderingRequirement::Ordered) &&
				IsRGAccessCompatible(RGBufferAccess::StorageReadWrite,
					RGDependencyAccess::ReadWrite, RGOrderingRequirement::Unordered) &&
				!IsRGAccessCompatible(RGTextureAccess::StorageRead, RGDependencyAccess::Write,
					RGOrderingRequirement::Ordered) &&
				!IsRGAccessCompatible(RGBufferAccess::StructuredRead, RGDependencyAccess::Read,
					RGOrderingRequirement::Unordered),
				"Storage access and ordering declarations reject incompatible semantics");

			RenderGraph graph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGBufferId storageBuffer;
			graph.AddPass<StorageAccessPassData>("InitialStorageWrite",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					storageBuffer = builder.CreateBuffer("StorageBuffer");
					builder.WriteInPlace(storageBuffer, RGBufferAccess::StorageWrite);
					data.m_Buffer = storageBuffer;
				});
			graph.AddPass<StorageAccessPassData>("StorageRead",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(storageBuffer, RGBufferAccess::StorageRead,
						std::nullopt, RGOrderingRequirement::Unordered);
					builder.SideEffect();
				});
			graph.AddPass<StorageAccessPassData>("SecondStorageWrite",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					builder.WriteInPlace(storageBuffer, RGBufferAccess::StorageWrite);
					data.m_Buffer = storageBuffer;
				});
			graph.AddPass<StorageAccessPassData>("StorageReadWrite",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(storageBuffer, RGBufferAccess::StorageReadWrite);
					data.m_Buffer = storageBuffer;
				});
			graph.AddPass<StorageAccessPassData>("FinalStorageRead",
				[&storageBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(storageBuffer, RGBufferAccess::StorageRead);
					builder.SideEffect();
				});
			graph.AddPass<StorageAccessPassData>("UnusedStorageWrite",
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
				HasDependencyEdge(snapshot, 0, 1, RGDependencyReason::WriterToReader) &&
				HasDependencyEdge(snapshot, 1, 2, RGDependencyReason::PreviousReaderToWriter),
				"RenderGraph creates writer-to-reader and reader-to-writer edges");
			context.Check(
				HasDependencyEdge(snapshot, 0, 2, RGDependencyReason::PreviousWriterToWriter),
				"RenderGraph creates writer-to-writer edges");
			context.Check(HasDependencyEdge(snapshot, 2, 3, RGDependencyReason::WriterToReader) &&
				HasDependencyEdge(snapshot, 3, 4, RGDependencyReason::WriterToReader),
				"Read-write storage access participates in incoming and outgoing dependencies");
			context.Check(!initialWrite.m_PreBarriers.empty() &&
				initialWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Transition &&
				storageRead.m_PreBarriers.empty() &&
				secondWrite.m_PreBarriers.size() == 1 &&
				secondWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav &&
				storageReadWrite.m_PreBarriers.size() == 1 &&
				storageReadWrite.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav &&
				finalRead.m_PreBarriers.size() == 1 &&
				finalRead.m_PreBarriers[0].m_Kind == RGBarrierKind::Uav,
				"Storage barriers distinguish transitions, ordered hazards, and explicit unordered access");
			context.Check(unusedWrite.m_Culled && unusedWrite.m_ExecutionOrder < 0 &&
				unusedWrite.m_PreBarriers.empty() &&
				unusedWrite.m_PostBarriers.empty(),
				"Culled storage writers leave no execution or barrier work");

			RenderGraph transitionGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGBufferId transitionBuffer;
			transitionGraph.AddPass<StorageAccessPassData>("TransitionStorageWrite",
				[&transitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					transitionBuffer = builder.CreateBuffer("TransitionBuffer");
					builder.WriteInPlace(transitionBuffer, RGBufferAccess::StorageWrite,
						RHIStage::ComputeShader);
					data.m_Buffer = transitionBuffer;
				});
			transitionGraph.AddPass<StorageAccessPassData>("TransitionStructuredRead",
				[&transitionBuffer](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(transitionBuffer, RGBufferAccess::StructuredRead,
						RHIStage::PixelShader);
					builder.SideEffect();
				});
			const bool transitionCompiled = transitionGraph.Compile();
			context.Check(transitionCompiled, "Compute UAV-write to pixel SRV-read fixture compiles");
			if (transitionCompiled)
			{
				RGSnapshot transitionSnapshot;
				BuildRenderGraphSnapshot(transitionGraph, transitionSnapshot);
				const auto& barriers = transitionSnapshot.m_Passes[1].m_PreBarriers;
				context.Check(barriers.size() == 1 &&
					barriers[0].m_ResourceName == "TransitionBuffer" &&
					barriers[0].m_ResourceType == RGResourceType::RGBuffer &&
					barriers[0].m_Kind == RGBarrierKind::Transition &&
					barriers[0].m_Reason == RGBarrierReason::AccessTransition &&
					barriers[0].m_Before.m_Stages == RHIStage::ComputeShader &&
					barriers[0].m_Before.m_Access == RHIAccess::UnorderedAccess &&
					barriers[0].m_Before.m_Layout == RHILayout::Common &&
					barriers[0].m_After.m_Stages == RHIStage::PixelShader &&
					barriers[0].m_After.m_Access == RHIAccess::ShaderResource &&
					barriers[0].m_After.m_Layout == RHILayout::Common,
					"Forward+ cull outputs transition exactly from compute UAV-write to pixel SRV-read");
			}

			RenderGraph computeTransitionGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGBufferId computeTransitionBuffer;
			computeTransitionGraph.AddPass<StorageAccessPassData>("ComputeTransitionStorageWrite",
				[&computeTransitionBuffer](
					RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					computeTransitionBuffer = builder.CreateBuffer("ComputeTransitionBuffer");
					builder.WriteInPlace(computeTransitionBuffer, RGBufferAccess::StorageWrite,
						RHIStage::ComputeShader);
					data.m_Buffer = computeTransitionBuffer;
				});
			computeTransitionGraph.AddPass<StorageAccessPassData>("ComputeTransitionStructuredRead",
				[&computeTransitionBuffer](
					RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(computeTransitionBuffer,
						RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
					builder.SideEffect();
				});
			const bool computeTransitionCompiled = computeTransitionGraph.Compile();
			context.Check(
				computeTransitionCompiled, "Compute UAV-write to compute SRV-read fixture compiles");
			if (computeTransitionCompiled)
			{
				RGSnapshot transitionSnapshot;
				BuildRenderGraphSnapshot(computeTransitionGraph, transitionSnapshot);
				const auto& barriers = transitionSnapshot.m_Passes[1].m_PreBarriers;
				context.Check(barriers.size() == 1 &&
					barriers[0].m_ResourceName == "ComputeTransitionBuffer" &&
					barriers[0].m_ResourceType == RGResourceType::RGBuffer &&
					barriers[0].m_Kind == RGBarrierKind::Transition &&
					barriers[0].m_Reason == RGBarrierReason::AccessTransition &&
					barriers[0].m_Before.m_Stages == RHIStage::ComputeShader &&
					barriers[0].m_Before.m_Access == RHIAccess::UnorderedAccess &&
					barriers[0].m_Before.m_Layout == RHILayout::Common &&
					barriers[0].m_After.m_Stages == RHIStage::ComputeShader &&
					barriers[0].m_After.m_Access == RHIAccess::ShaderResource &&
					barriers[0].m_After.m_Layout == RHILayout::Common,
					"HDR diff metrics transition exactly from compute UAV-write to compute SRV-read");
			}

			RenderGraph resourceSpecificGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGBufferId firstBuffer;
			RGBufferId secondBuffer;
			resourceSpecificGraph.AddPass<DualStorageAccessPassData>("WriteTwoStorageBuffers",
				[&](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
				{
					firstBuffer = builder.CreateBuffer("FirstStorageBuffer");
					secondBuffer = builder.CreateBuffer("SecondStorageBuffer");
					builder.WriteInPlace(firstBuffer, RGBufferAccess::StorageWrite);
					builder.WriteInPlace(secondBuffer, RGBufferAccess::StorageWrite);
					data.m_FirstBuffer = firstBuffer;
					data.m_SecondBuffer = secondBuffer;
				});
			resourceSpecificGraph.AddPass<DualStorageAccessPassData>("ReadWriteTwoStorageBuffers",
				[&](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(firstBuffer, RGBufferAccess::StorageReadWrite);
					builder.ReadWriteInPlace(secondBuffer, RGBufferAccess::StorageReadWrite);
					data.m_FirstBuffer = firstBuffer;
					data.m_SecondBuffer = secondBuffer;
					builder.SideEffect();
				});
			const bool resourceSpecificCompiled = resourceSpecificGraph.Compile();
			context.Check(
				resourceSpecificCompiled, "Resource-specific UAV barrier fixture compiles");
			if (resourceSpecificCompiled)
			{
				RGSnapshot resourceSpecificSnapshot;
				BuildRenderGraphSnapshot(resourceSpecificGraph, resourceSpecificSnapshot);
				const auto& barriers = resourceSpecificSnapshot.m_Passes[1].m_PreBarriers;
				context.Check(
					barriers.size() == 2 && barriers[0].m_Kind == RGBarrierKind::Uav &&
					barriers[1].m_Kind == RGBarrierKind::Uav &&
					barriers[0].m_VirtualResourceIndex != barriers[1].m_VirtualResourceIndex,
					"Each ordered UAV hazard retains its own resource identity for physical resolution");
			}

			RenderGraph multiSubresourceGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGTextureId mipTexture;
			const RHITextureDesc mipTextureDesc = {
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Extent = {.m_Width = 4, .m_Height = 4, .m_Depth = 1},
				.m_MipLevels = 3,
			};
			const RHISubresourceRange firstMip = {
				.m_BaseMip = 0,
				.m_MipCount = 1,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange firstTwoMips = {
				.m_BaseMip = 0,
				.m_MipCount = 2,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange secondMip = {
				.m_BaseMip = 1,
				.m_MipCount = 1,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange thirdMip = {
				.m_BaseMip = 2,
				.m_MipCount = 1,
				.m_BaseArraySlice = 0,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("WriteFirstMip",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					mipTexture = builder.CreateTexture("MipTexture", mipTextureDesc);
					// Every subresource later consumed in this fixture must be
					// defined first: mip0 (UAV), mip1 (copy destination) and
					// mip2 (UAV, later sampled and copied).
					builder.WriteInPlace(mipTexture, RGTextureAccess::StorageWrite, firstMip);
					builder.WriteInPlace(mipTexture, RGTextureAccess::CopyDest, secondMip);
					builder.WriteInPlace(mipTexture, RGTextureAccess::StorageWrite, thirdMip);
					data.m_Texture = mipTexture;
				});
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("SampleThirdMip",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					data.m_Texture = builder.Read(
						mipTexture, RGTextureAccess::Sample, RHIStage::PixelShader, thirdMip);
					builder.SideEffect();
				});
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("ReadWriteFirstTwoMips",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(
						mipTexture, RGTextureAccess::StorageReadWrite, firstTwoMips);
					data.m_Texture = mipTexture;
					builder.SideEffect();
				});
			multiSubresourceGraph.AddPass<TextureStorageAccessPassData>("CopyToThirdMip",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					builder.WriteInPlace(mipTexture, RGTextureAccess::CopyDest, thirdMip);
					data.m_Texture = mipTexture;
					builder.SideEffect();
				});
			const bool multiSubresourceCompiled = multiSubresourceGraph.Compile();
			context.Check(
				multiSubresourceCompiled, "Multi-subresource texture UAV barrier fixture compiles");
			if (multiSubresourceCompiled)
			{
				RGSnapshot multiSubresourceSnapshot;
				BuildRenderGraphSnapshot(multiSubresourceGraph, multiSubresourceSnapshot);
				const auto& barriers = multiSubresourceSnapshot.m_Passes[2].m_PreBarriers;
				const auto transition = std::ranges::find(
					barriers, RGBarrierKind::Transition, &RGSnapshotBarrierInfo::m_Kind);
				const auto uav =
					std::ranges::find(barriers, RGBarrierKind::Uav, &RGSnapshotBarrierInfo::m_Kind);
				context.Check(barriers.size() == 2 && transition != barriers.end() &&
					transition->m_Subresources &&
					transition->m_Subresources->m_BaseMip == 1 &&
					transition->m_Subresources->m_MipCount == 1 &&
					uav != barriers.end() && uav->m_Subresources == firstMip &&
					uav->m_Before.m_Layout == RHILayout::UnorderedAccess &&
					uav->m_After.m_Layout == RHILayout::UnorderedAccess,
					"Texture transitions and Enhanced UAV ordering retain exact subresource state");
				const auto& copyBarriers = multiSubresourceSnapshot.m_Passes[3].m_PreBarriers;
				context.Check(copyBarriers.size() == 1 &&
					copyBarriers[0].m_Kind == RGBarrierKind::Transition &&
					copyBarriers[0].m_Subresources == thirdMip &&
					Test(copyBarriers[0].m_Before.m_Stages, RHIStage::PixelShader),
					"A texture UAV barrier preserves non-UAV synchronization scopes on untouched subresources");
			}

			RenderGraph stageScopeGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGBufferId stageScopeBuffer;
			stageScopeGraph.AddPass<StorageAccessPassData>("InitializeStageScopeBuffer",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					stageScopeBuffer = builder.CreateBuffer("StageScopeBuffer");
					builder.WriteInPlace(stageScopeBuffer, RGBufferAccess::CopyDest);
					data.m_Buffer = stageScopeBuffer;
				});
			stageScopeGraph.AddPass<StorageAccessPassData>("PixelRead",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(
						stageScopeBuffer, RGBufferAccess::StructuredRead, RHIStage::PixelShader);
					builder.SideEffect();
				});
			stageScopeGraph.AddPass<StorageAccessPassData>("ComputeRead",
				[&](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.Read(
						stageScopeBuffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
					builder.SideEffect();
				});
			stageScopeGraph.AddPass<StorageAccessPassData>("CopyAfterReads",
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
				context.Check(computeRead.m_PreBarriers.empty() &&
					copyAfterReads.m_PreBarriers.size() == 1 &&
					Test(copyAfterReads.m_PreBarriers[0].m_Before.m_Stages,
						RHIStage::PixelShader) &&
					Test(copyAfterReads.m_PreBarriers[0].m_Before.m_Stages,
						RHIStage::ComputeShader),
					"Read-only stage scopes accumulate until a persistent state transition");
			}

			RenderGraph temporalResolvedInitializationGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RGTextureId temporalResolvedColor;
			temporalResolvedInitializationGraph.AddPass<TextureStorageAccessPassData>(
				"TAA.InitializeResolvedColor",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					builder.SideEffect();
					RHITextureDesc desc{};
					desc.m_Format = TemporalAAResolvedColorFormat;
					desc.m_Extent = { 1920, 1080, 1 };
					temporalResolvedColor =
						builder.CreateTexture("TAA.ResolvedColorInitialization", desc);
					builder.WriteInPlace(
						temporalResolvedColor, RGTextureAccess::RenderTarget);
					data.m_Texture = temporalResolvedColor;
				});
			temporalResolvedInitializationGraph.AddPass<TextureStorageAccessPassData>(
				"TAA.ComputeResolve", RGPassEncoderType::Compute,
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					// Production TAA stays live through its exported history writes.
					builder.SideEffect();
					builder.WriteInPlace(
						temporalResolvedColor, RGTextureAccess::StorageWrite);
					data.m_Texture = temporalResolvedColor;
				});
			temporalResolvedInitializationGraph.AddPass<TextureStorageAccessPassData>(
				"Forward.TransparentLoad",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					builder.ReadWriteInPlace(
						temporalResolvedColor, RGTextureAccess::RenderTarget);
					data.m_Texture = temporalResolvedColor;
					builder.SideEffect();
				});
			const bool temporalResolvedInitializationCompiled =
				temporalResolvedInitializationGraph.Compile();
			context.Check(temporalResolvedInitializationCompiled,
				"TAA resolved-color initialization fixture compiles");
			if (temporalResolvedInitializationCompiled)
			{
				RGSnapshot temporalResolvedSnapshot;
				BuildRenderGraphSnapshot(
					temporalResolvedInitializationGraph, temporalResolvedSnapshot);
				const auto resource = std::ranges::find(temporalResolvedSnapshot.m_Resources,
					"TAA.ResolvedColorInitialization", &RGSnapshotResourceInfo::m_Name);
				const auto isTransition = [](const RGSnapshotPassInfo& pass,
					RHILayout before, RHILayout after) noexcept
				{
					return std::ranges::any_of(pass.m_PreBarriers,
						[before, after](const RGSnapshotBarrierInfo& barrier) noexcept
						{
							return barrier.m_ResourceName ==
								"TAA.ResolvedColorInitialization" &&
								barrier.m_Kind == RGBarrierKind::Transition &&
								barrier.m_Before.m_Layout == before &&
								barrier.m_After.m_Layout == after;
						});
				};
				context.Check(resource != temporalResolvedSnapshot.m_Resources.end() &&
					Test(static_cast<RHITextureUsage>(resource->m_UsageBits),
						RHITextureUsage::RenderTarget) &&
					Test(static_cast<RHITextureUsage>(resource->m_UsageBits),
						RHITextureUsage::UnorderedAccess),
					"TAA resolved color preserves its mixed RTV/UAV usage contract");
				context.Check(temporalResolvedSnapshot.m_Passes.size() == 3 &&
					isTransition(temporalResolvedSnapshot.m_Passes[0], RHILayout::Undefined,
						RHILayout::RenderTarget),
					"TAA resolved color initializes through a first-use render-target transition");
				context.Check(temporalResolvedSnapshot.m_Passes.size() == 3 &&
					isTransition(temporalResolvedSnapshot.m_Passes[1], RHILayout::RenderTarget,
						RHILayout::UnorderedAccess),
					"TAA resolve transitions initialized color from RTV to UAV");
				context.Check(temporalResolvedSnapshot.m_Passes.size() == 3 &&
					isTransition(temporalResolvedSnapshot.m_Passes[2], RHILayout::UnorderedAccess,
						RHILayout::RenderTarget),
					"Transparent load transitions resolved color back from UAV to RTV");
			}

			RecordingGraphicsCommandContext graphicsContext;
			RecordingComputeCommandContext directComputeContext;
			uint32_t graphicsExecutions = 0;
			uint32_t computeExecutions = 0;
			uint32_t copyExecutions = 0;
			uint32_t culledComputeExecutions = 0;
			bool graphicsContextSelected = false;
			bool directComputeContextSelected = false;
			bool copyContextSelected = false;
			bool copyResourcesResolved = false;
			bool asyncComputeRemainsUnavailable = false;
			RecordingDevice encoderDevice;
			RenderGraph encoderGraph({
				.m_Device = &encoderDevice,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			encoderGraph.AddTrivialSideEffectPass("GraphicsEncoder",
				[&](RGExecuteContext& executeContext)
				{
					++graphicsExecutions;
					auto* selectedContext = executeContext.GetGraphicsCommandContext();
					graphicsContextSelected = selectedContext == &graphicsContext;
					const RHIRenderingAttachment colorAttachment{
						.m_View = RHITextureViewHandle{ 31, 1 },
						.m_LoadOp = RHIContentLoadOp::DontCare,
					};
					selectedContext->BeginRendering({ .m_ColorAttachments =
						std::span<const RHIRenderingAttachment>(&colorAttachment, 1) });
				});
			encoderGraph.AddTrivialSideEffectPass("ComputeEncoder", RGPassEncoderType::Compute,
				[&](RGExecuteContext& executeContext)
				{
					++computeExecutions;
					directComputeContextSelected =
						executeContext.GetDirectComputeCommandContext() == &directComputeContext;
					asyncComputeRemainsUnavailable =
						executeContext.GetAsyncComputeCommandContext() == nullptr;
				});
			encoderGraph.AddPass<DualStorageAccessPassData>(
				"CopyEncoder", RGPassEncoderType::Copy,
				[](RenderGraph::RGBuilder& builder, DualStorageAccessPassData& data)
				{
					const RHIBufferDesc copyBufferDesc{
						.m_SizeInBytes = 64,
						.m_Usage = RHIBufferUsage::CopySource | RHIBufferUsage::CopyDest,
					};
					data.m_FirstBuffer = builder.ImportBuffer("CopySource", RHIBufferHandle{ 21, 1 },
						copyBufferDesc, RGBufferAccess::CopySource, RGContentValidity::Defined);
					data.m_SecondBuffer = builder.ImportBuffer("CopyDestination",
						RHIBufferHandle{ 22, 1 }, copyBufferDesc, RGBufferAccess::CopyDest,
						RGContentValidity::Undefined);
					data.m_FirstBuffer = builder.Read(
						data.m_FirstBuffer, RGBufferAccess::CopySource, RHIStage::Copy);
					builder.WriteInPlace(
						data.m_SecondBuffer, RGBufferAccess::CopyDest, RHIStage::Copy);
					builder.SideEffect();
				},
				[&](RGExecuteContext& executeContext, DualStorageAccessPassData& data)
				{
					++copyExecutions;
					auto* copyContext = executeContext.GetCopyCommandContext();
					copyContextSelected = copyContext == &graphicsContext;
					const RHIBufferHandle source =
						executeContext.GetBufferHandle(data.m_FirstBuffer);
					const RHIBufferHandle destination =
						executeContext.GetBufferHandle(data.m_SecondBuffer);
					copyResourcesResolved =
						source == RHIBufferHandle{ 21, 1 } && destination == RHIBufferHandle{ 22, 1 };
					if (copyContext)
					{
						copyContext->CopyBuffer(destination, 4, source, 8, 16);
					}
				});
			encoderGraph.AddPass<StorageAccessPassData>(
				"CulledComputeEncoder", RGPassEncoderType::Compute,
				[](RenderGraph::RGBuilder&, StorageAccessPassData&) {},
				[&](RGExecuteContext&, StorageAccessPassData&) { ++culledComputeExecutions; });
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

				RGExecuteContext executeContext({
					.m_GraphicsCommandContext = &graphicsContext,
					.m_DirectComputeCommandContext = &directComputeContext,
					.m_AsyncComputeCommandContext = nullptr,
					});
				encoderGraph.Execute(executeContext);
				context.Check(graphicsExecutions == 1 && computeExecutions == 1 &&
					copyExecutions == 1 && culledComputeExecutions == 0 &&
					graphicsContextSelected && directComputeContextSelected &&
					copyContextSelected && copyResourcesResolved &&
					graphicsContext.m_CopyBufferCount == 1 &&
					graphicsContext.m_BeginRenderingCount == 1 &&
					graphicsContext.m_EndRenderingCount == 1 &&
					!graphicsContext.IsRendering() &&
					asyncComputeRemainsUnavailable,
					"RenderGraph closes graphics rendering before the next direct-list encoder");
				context.Check(graphicsContext.m_BeginProfileCount == 2 &&
					graphicsContext.m_EndProfileCount == 2 &&
					directComputeContext.m_BeginProfileCount == 1 &&
					directComputeContext.m_EndProfileCount == 1,
					"RenderGraph profiles Graphics, Compute, and Copy on their selected command contexts");
			}

			RecordingGraphicsCommandContext batchingGraphicsContext;
			RecordingComputeCommandContext batchingComputeContext;
			RecordingDevice batchingDevice;
			RenderGraph barrierBatchingGraph({
				.m_Device = &batchingDevice,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			barrierBatchingGraph.AddPass<BarrierBatchingPassData>(
				"BatchTextureAndBufferBarriers", RGPassEncoderType::Compute,
				[](RenderGraph::RGBuilder& builder, BarrierBatchingPassData& data)
				{
					const RHITextureDesc textureDesc = {
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Extent = {.m_Width = 4, .m_Height = 4, .m_Depth = 1},
					};
					const RHIBufferDesc bufferDesc = {
						.m_SizeInBytes = 64,
						.m_StrideInBytes = 16,
					};
					data.m_Texture = builder.ImportTexture("BatchingTexture",
						RHITextureHandle{ 1, 1 }, textureDesc, RGTextureAccess::CopyDest,
						RGContentValidity::Defined);
					data.m_Buffer = builder.ImportBuffer("BatchingBuffer", RHIBufferHandle{ 1, 1 },
						bufferDesc, RGBufferAccess::CopyDest, RGContentValidity::Defined);
					data.m_Texture = builder.Read(
						data.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
					data.m_Buffer = builder.Read(
						data.m_Buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
					builder.Export(
						data.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
					builder.Export(
						data.m_Buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader);
					builder.SideEffect();
				},
				[](RGExecuteContext&, BarrierBatchingPassData&) {});
			const bool barrierBatchingCompiled = barrierBatchingGraph.Compile();
			context.Check(barrierBatchingCompiled, "RenderGraph barrier batching fixture compiles");
			if (barrierBatchingCompiled)
			{
				RGExecuteContext batchingExecuteContext({
					.m_GraphicsCommandContext = &batchingGraphicsContext,
					.m_DirectComputeCommandContext = &batchingComputeContext,
					.m_AsyncComputeCommandContext = nullptr,
					});
				barrierBatchingGraph.Execute(batchingExecuteContext);
				context.Check(batchingComputeContext.m_TextureBarrierCount == 1 &&
					batchingComputeContext.m_BufferBarrierCount == 1 &&
					batchingComputeContext.m_FlushBarrierCount == 1,
					"RenderGraph batches texture and buffer barriers into one explicit flush");
			}
		}

		void RunPersistentTexturePoolContractTests(SelfTestContext& context) noexcept
		{
			static_assert(!std::is_copy_constructible_v<PersistentTextureAllocation>);
			static_assert(!std::is_copy_assignable_v<PersistentTextureAllocation>);
			static_assert(std::is_nothrow_move_constructible_v<PersistentTextureAllocation>);
			static_assert(std::is_nothrow_move_assignable_v<PersistentTextureAllocation>);

			RecordingDevice device;
			device.m_UseControlledFenceCompletion = true;
			PersistentTexturePool pool(&device);

			auto makeCreateInfo = [](RHIFormat format, uint32_t width, uint32_t height) noexcept
			{
				return RHIOwnedTextureCreateInfo{
					.m_Desc = {
						.m_Format = format,
						.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
						.m_Extent = { width, height, 1 },
					},
				};
			};
			auto acquireHistorySet = [&](uint32_t width, uint32_t height)
			{
				const RHIOwnedTextureCreateInfo colorInfo =
					makeCreateInfo(RHIFormat::R16G16B16A16Float, width, height);
				const RHIOwnedTextureCreateInfo depthInfo =
					makeCreateInfo(RHIFormat::R32Float, width, height);
				return std::array<PersistentTextureAllocation, 4>{
					pool.AcquireTexture(colorInfo, "TAA.HistoryColor0"),
					pool.AcquireTexture(colorInfo, "TAA.HistoryColor1"),
					pool.AcquireTexture(depthInfo, "TAA.HistoryDepth0"),
					pool.AcquireTexture(depthInfo, "TAA.HistoryDepth1"),
				};
			};
			auto releaseHistorySet = [&](auto& allocations, const RHIFencePoint& fencePoint)
			{
				bool released = true;
				for (PersistentTextureAllocation& allocation : allocations)
				{
					released = pool.ReleaseTexture(std::move(allocation), fencePoint) && released;
				}
				return released;
			};
			auto allPendingAtFence = [](const PersistentTexturePoolDiagnostics& diagnostics,
				uint64_t fenceValue) noexcept
			{
				return std::ranges::all_of(diagnostics.m_PendingRetirements,
					[fenceValue](const PersistentTexturePendingRetirementDiagnostics& pending)
					{
						return pending.m_Texture.IsValid() &&
							pending.m_FencePoint.m_Value == fenceValue &&
							!pending.m_LogicalName.empty();
					});
			};

			const auto invalidAllocation = pool.AcquireTexture({}, "Invalid");
			auto firstGeneration = acquireHistorySet(1920, 1080);
			const uint64_t fullSizeBytes =
				2 * EstimatePersistentTextureBytes(firstGeneration[0].GetCreateInfo().m_Desc) +
				2 * EstimatePersistentTextureBytes(firstGeneration[2].GetCreateInfo().m_Desc);
			PersistentTextureAllocation movedAllocation = std::move(firstGeneration[0]);
			const bool moveTransferredOwnership =
				!firstGeneration[0].IsValid() && movedAllocation.IsValid();
			firstGeneration[0] = std::move(movedAllocation);
			const PersistentTexturePoolDiagnostics initialDiagnostics = pool.GetDiagnostics();
			context.Check(!invalidAllocation.IsValid() && moveTransferredOwnership &&
				firstGeneration[0].IsValid() && !movedAllocation.IsValid() &&
				initialDiagnostics.m_ActiveTextureCount == 4 &&
				initialDiagnostics.m_PendingRetirementTextureCount == 0 &&
				initialDiagnostics.m_EstimatedActiveBytes == fullSizeBytes &&
				device.m_CreateTextureCount == 4,
				"Persistent texture allocations are explicit, move-only, and byte-accounted");

			const bool invalidFenceRejected =
				!pool.ReleaseTexture(std::move(firstGeneration[0]), {});
			const RHIFencePoint firstFence{ RHIFenceHandle{ 1, 1 }, 10 };
			const bool firstReleased = releaseHistorySet(firstGeneration, firstFence);
			const PersistentTexturePoolDiagnostics firstPending = pool.GetDiagnostics();
			pool.Tick();
			const PersistentTexturePoolDiagnostics beforeFirstCompletion = pool.GetDiagnostics();
			context.Check(invalidFenceRejected && firstReleased &&
				firstPending.m_ActiveTextureCount == 0 &&
				firstPending.m_PendingRetirementTextureCount == 4 &&
				firstPending.m_EstimatedPendingRetirementBytes == fullSizeBytes &&
				allPendingAtFence(firstPending, 10) &&
				beforeFirstCompletion.m_PendingRetirementTextureCount == 4 &&
				device.m_DestroyTextureCount == 0 && device.m_RecordTextureUseCount == 4,
				"Persistent texture release is logical immediately and physical only after its fence");

			auto resizedGeneration = acquireHistorySet(1280, 720);
			const uint64_t resizedBytes =
				2 * EstimatePersistentTextureBytes(resizedGeneration[0].GetCreateInfo().m_Desc) +
				2 * EstimatePersistentTextureBytes(resizedGeneration[2].GetCreateInfo().m_Desc);
			const PersistentTexturePoolDiagnostics resizePressure = pool.GetDiagnostics();
			const RHIFencePoint resizeFence{ RHIFenceHandle{ 1, 1 }, 20 };
			const bool resizedReleased = releaseHistorySet(resizedGeneration, resizeFence);
			auto reenabledGeneration = acquireHistorySet(1920, 1080);
			const PersistentTexturePoolDiagnostics togglePressure = pool.GetDiagnostics();
			context.Check(resizePressure.m_ActiveTextureCount == 4 &&
				resizePressure.m_PendingRetirementTextureCount == 4 &&
				resizePressure.m_EstimatedActiveBytes == resizedBytes &&
				resizePressure.m_EstimatedPendingRetirementBytes == fullSizeBytes &&
				resizedReleased && togglePressure.m_ActiveTextureCount == 4 &&
				togglePressure.m_PendingRetirementTextureCount == 8 &&
				togglePressure.m_TotalAcquireCount == 12 && device.m_CreateTextureCount == 12,
				"Resize and toggle pressure create fresh persistent textures while old generations retire");

			device.m_CompletedFenceValue = 10;
			pool.Tick();
			const PersistentTexturePoolDiagnostics partiallyRetired = pool.GetDiagnostics();
			const uint32_t partiallyDestroyedTextureCount = device.m_DestroyTextureCount;
			const RHIFencePoint reenabledFence{ RHIFenceHandle{ 1, 1 }, 30 };
			const bool reenabledReleased = releaseHistorySet(reenabledGeneration, reenabledFence);
			const bool doubleReleaseRejected =
				!pool.ReleaseTexture(std::move(reenabledGeneration[0]), reenabledFence);
			device.m_CompletedFenceValue = 30;
			pool.Tick();
			const PersistentTexturePoolDiagnostics fullyRetired = pool.GetDiagnostics();
			context.Check(partiallyRetired.m_ActiveTextureCount == 4 &&
				partiallyRetired.m_PendingRetirementTextureCount == 4 &&
				partiallyRetired.m_CompletedRetirementCount == 4 &&
				partiallyDestroyedTextureCount == 4 && reenabledReleased &&
				doubleReleaseRejected && fullyRetired.m_ActiveTextureCount == 0 &&
				fullyRetired.m_PendingRetirementTextureCount == 0 &&
				fullyRetired.m_TotalReleaseCount == 12 &&
				fullyRetired.m_CompletedRetirementCount == 12 &&
				fullyRetired.m_RejectedReleaseCount == 2 &&
				device.m_DestroyTextureCount == 12 &&
				device.m_RecordTextureUseCount == 12 &&
				device.m_LastRecordedFencePoint == reenabledFence,
				"Fence retirement drains only completed generations and rejects duplicate release");
		}

		void RunTemporalHistoryTransactionContractTests(SelfTestContext& context) noexcept
		{
			RecordingDevice writeProofDevice;
			RHITextureDesc writeProofDesc{};
			writeProofDesc.m_Dimension = RHITextureDimension::Texture2D;
			writeProofDesc.m_Format = TemporalHistoryColorFormat;
			writeProofDesc.m_Usage =
				RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess;
			writeProofDesc.m_Extent = { 8, 8, 1 };
			const RHITextureHandle writeProofTexture = writeProofDevice.CreateTexture(
				RHIOwnedTextureCreateInfo{ .m_Desc = writeProofDesc }, {});
			RenderGraph writeProofGraph({
				.m_Device = &writeProofDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			bool initiallyDefinedCountsAsCurrentWrite = true;
			bool fullCurrentWriteRecognized = false;
			writeProofGraph.AddPass<RGTextureId>("TemporalHistory.WriteProof",
				[&](RenderGraph::RGBuilder& builder, RGTextureId& texture)
				{
					texture = builder.ImportTexture("TemporalHistory.WriteProof.Texture",
						writeProofTexture, writeProofDesc, CommonRHIResourceState(),
						RGContentValidity::Defined);
					initiallyDefinedCountsAsCurrentWrite =
						builder.IsTextureFullyWrittenByCurrentPass(texture);
					builder.WriteInPlace(texture, RGTextureAccess::StorageWrite);
					fullCurrentWriteRecognized =
						builder.IsTextureFullyWrittenByCurrentPass(texture);
					builder.Export(texture, RGTextureAccess::None);
				});
			context.Check(!initiallyDefinedCountsAsCurrentWrite && fullCurrentWriteRecognized &&
				writeProofGraph.Compile(),
				"RenderGraph distinguishes prior Defined contents from a full current-pass write");

			RenderGraph historyColorPreviewGraph({
				.m_Device = &writeProofDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			RHITextureDesc historyColorPreviewPayloadDesc = writeProofDesc;
			historyColorPreviewPayloadDesc.m_Usage = RHITextureUsage::None;
			RGTemporalAAResources historyColorPreviewResources{};
			bool historyColorPreviewUsesTransientPayload = false;
			historyColorPreviewGraph.AddPass<TextureStorageAccessPassData>(
				"TemporalHistory.ExportPrevious",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					historyColorPreviewResources.m_History.m_PreviousColor =
						builder.ImportTexture("TAA.Preview.PreviousColor", writeProofTexture,
							writeProofDesc, CommonRHIResourceState(), RGContentValidity::Defined);
					historyColorPreviewResources.m_History.m_PreviousColor = builder.Read(
						historyColorPreviewResources.m_History.m_PreviousColor,
						RGTextureAccess::Sample);
					builder.Export(
						historyColorPreviewResources.m_History.m_PreviousColor,
						RGTextureAccess::None);

					historyColorPreviewResources.m_ReprojectionDiagnostics =
						builder.CreateTexture(
							"TAA.Preview.AccumulatedHistory", historyColorPreviewPayloadDesc);
					builder.WriteInPlace(
						historyColorPreviewResources.m_ReprojectionDiagnostics,
						RGTextureAccess::StorageWrite);
					data.m_Texture =
						historyColorPreviewResources.m_ReprojectionDiagnostics;
				});
			historyColorPreviewGraph.AddPass<TextureStorageAccessPassData>(
				"PostProcess.Preview.HistoryColor",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					const RGTextureId previewSource = ResolveTemporalAAPreviewSource(
						historyColorPreviewResources,
						PostProcessDebugTap::TemporalHistoryColor);
					historyColorPreviewUsesTransientPayload =
						previewSource ==
							historyColorPreviewResources.m_ReprojectionDiagnostics &&
						previewSource !=
							historyColorPreviewResources.m_History.m_PreviousColor;
					data.m_Texture = builder.Read(previewSource, RGTextureAccess::Sample);
					builder.SideEffect();
				});
			context.Check(historyColorPreviewUsesTransientPayload &&
				historyColorPreviewGraph.Compile(),
				"TAA history-color preview reads its transient accumulated payload after previous history export");

			RecordingDevice device;
			device.m_TextureViewsSupported = true;
			const TemporalHistoryFormatSupport formatSupport =
				QueryTemporalHistoryFormatSupport(device);
			context.Check(formatSupport.IsSupported() &&
				device.m_TextureViewQueryCount == 4 &&
				device.m_LastTextureViewQueryTextureDesc.m_Format == TemporalHistoryDepthFormat &&
				Test(device.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::Sampled) &&
				Test(device.m_LastTextureViewQueryTextureDesc.m_Usage,
					RHITextureUsage::UnorderedAccess) &&
				device.m_LastTextureViewQueryDesc.m_Type == RHITextureViewType::UnorderedAccess,
				"Temporal history capability requires SRV and typed-UAV support for both fixed formats");
			device.m_UseControlledFenceCompletion = true;
			PersistentTexturePool texturePool(&device);
			TemporalHistoryManager historyManager(&texturePool);
			TemporalViewHistory viewHistory;
			TemporalObjectHistory objectHistory;
			ResolvedTemporalFramePlan activePlan{
				.m_DisplayViewId = RenderViewID::Main,
				.m_SceneExtensionParticipation =
					SceneExtensionTemporalParticipation::TemporalIntegrated,
				.m_Status = TemporalAAFrameStatus::Active,
				.m_DisableReason = TemporalAADisableReason::None,
				.m_ResetIdentity = 7,
				.m_SessionIdentity = 11,
				.m_Requested = true,
				.m_Active = true,
				.m_InternalContractMode = true,
				.m_DisplayViewEligible = true,
				.m_DepthVelocityPathAvailable = true,
			};

			auto prepareDisplayView = [&](TemporalFrameTransaction& transaction)
			{
				RenderView view{};
				view.m_ViewId = RenderViewID::Main;
				view.m_Width = 64;
				view.m_Height = 64;
				view.m_IsValid = true;
				transaction.PrepareDisplayView(view);
				return view;
			};
			auto buildHistoryGraph = [&](TemporalFrameTransaction& transaction,
				bool expectPrevious, bool inspectColdStartContract)
			{
				RenderGraph graph({
					.m_Device = &device,
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				bool imported = false;
				bool exported = false;
				bool previousMatched = false;
				graph.AddPass<TemporalHistoryContractPassData>("TemporalHistory.Contract",
					[&](RenderGraph::RGBuilder& builder, TemporalHistoryContractPassData& data)
					{
						imported = transaction.ImportHistoryResources(builder, data.m_History);
						previousMatched = data.m_History.m_PreviousValid == expectPrevious;
						if (data.m_History.m_PreviousValid)
						{
							data.m_History.m_PreviousColor = builder.Read(
								data.m_History.m_PreviousColor, RGTextureAccess::Sample);
							data.m_History.m_PreviousDepth = builder.Read(
								data.m_History.m_PreviousDepth, RGTextureAccess::Sample);
						}
						builder.WriteInPlace(
							data.m_History.m_NextColor, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(
							data.m_History.m_NextDepth, RGTextureAccess::StorageWrite);
						exported =
							transaction.ExportHistoryResources(builder, data.m_History);
					});
				const bool compiled = graph.Compile();
				RGSnapshot snapshot;
				BuildRenderGraphSnapshot(graph, snapshot);
				const bool previousExportsCommon = !expectPrevious ||
					std::ranges::count_if(snapshot.m_Resources,
						[](const RGSnapshotResourceInfo& resource) noexcept
						{
							return resource.m_Name.starts_with("TAA.History.Previous") &&
								resource.m_HasFinalBarrierState &&
								resource.m_FinalBarrierState == CommonRHIResourceState();
						}) == 2;
				if (!inspectColdStartContract)
				{
					return imported && exported && previousMatched && compiled &&
						previousExportsCommon;
				}

				const bool importedFour = snapshot.m_Resources.size() == 4 &&
					std::ranges::all_of(snapshot.m_Resources,
						[](const RGSnapshotResourceInfo& resource) noexcept
						{ return resource.m_Imported; });
				const bool nextExportsCommon = std::ranges::count_if(snapshot.m_Resources,
					[](const RGSnapshotResourceInfo& resource) noexcept
					{
						return resource.m_Name.starts_with("TAA.History.Next") &&
							resource.m_InitialBarrierState == UndefinedRHITextureState() &&
							resource.m_HasFinalBarrierState &&
							resource.m_FinalBarrierState == CommonRHIResourceState();
					}) == 2;
				return imported && exported && previousMatched && compiled &&
					previousExportsCommon && importedFour && nextExportsCommon;
			};
			auto buildNoWriteHistoryGraph = [&](TemporalFrameTransaction& transaction)
			{
				RenderGraph graph({
					.m_Device = &device,
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				bool imported = false;
				bool exported = true;
				graph.AddPass<TemporalHistoryContractPassData>("TemporalHistory.NoWrite",
					[&](RenderGraph::RGBuilder& builder, TemporalHistoryContractPassData& data)
					{
						imported = transaction.ImportHistoryResources(builder, data.m_History);
						exported = transaction.ExportHistoryResources(builder, data.m_History);
					});
				return imported && !exported && !transaction.ParticipatedInResolve() &&
					graph.Compile();
			};

			TemporalFrameTransaction noWriteTransaction;
			noWriteTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			GGLAB_UNUSED(prepareDisplayView(noWriteTransaction));
			const bool noWriteGraphValid = buildNoWriteHistoryGraph(noWriteTransaction);
			noWriteTransaction.CommitCompleted(RHIFencePoint{ RHIFenceHandle{ 1, 1 }, 5 });
			const TemporalHistoryManagerDiagnostics afterNoWrite =
				historyManager.GetDiagnostics();
			context.Check(noWriteGraphValid &&
				noWriteTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
				!afterNoWrite.m_HistoryValid && afterNoWrite.m_ReadIndex == 0 &&
				!viewHistory.m_Valid && viewHistory.m_NextJitterIndex == 0,
				"Export without full current-frame history writes cannot participate or commit");

			TemporalFrameTransaction firstTransaction;
			firstTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			const RenderView firstView = prepareDisplayView(firstTransaction);
			const bool coldStartGraphValid = buildHistoryGraph(firstTransaction, false, true);
			const RHIFencePoint firstFence{ RHIFenceHandle{ 1, 1 }, 10 };
			firstTransaction.CommitCompleted(firstFence);
			const TemporalHistoryManagerDiagnostics firstCommitted =
				historyManager.GetDiagnostics();
			const uint64_t firstGeneration = firstCommitted.m_AllocationGeneration;
			context.Check(coldStartGraphValid && !firstView.m_HasPreviousTemporalState &&
				firstTransaction.GetState() == TemporalFrameTransactionState::Committed &&
				firstCommitted.m_HasActiveHistory && firstCommitted.m_HistoryValid &&
				firstCommitted.m_ReadIndex == 1 && firstCommitted.m_ActiveBytes > 0 &&
				firstCommitted.m_LastCommitted.m_JitterIndex == 0 &&
				firstCommitted.m_LastCommitted.m_GraphicsFence == firstFence &&
				device.m_CreateTextureCount == 4,
				"Temporal history cold start imports four persistent textures and commits one write pair");

			TemporalViewHistory incompatibleViewHistory = viewHistory;
			incompatibleViewHistory.Invalidate();
			TemporalObjectHistory incompatibleObjectHistory;
			TemporalFrameTransaction incompatibleViewTransaction;
			incompatibleViewTransaction.Begin(incompatibleViewHistory,
				incompatibleObjectHistory, activePlan, 64, 64, &historyManager);
			const RenderView incompatibleView = prepareDisplayView(incompatibleViewTransaction);
			const bool managerHistoryStillValid =
				buildHistoryGraph(incompatibleViewTransaction, true, false);
			const bool finalHistoryCompatibility =
				incompatibleViewTransaction.HasCompatiblePreviousHistory();
			incompatibleViewTransaction.Abort();
			context.Check(managerHistoryStillValid && !finalHistoryCompatibility &&
				!incompatibleView.m_HasPreviousTemporalState,
				"Temporal resolve gates imported history with the transaction's final view-and-history compatibility");

			TemporalFrameTransaction abortedTransaction;
			abortedTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			const RenderView abortedView = prepareDisplayView(abortedTransaction);
			const bool abortGraphValid = buildHistoryGraph(abortedTransaction, true, false);
			const RHIFencePoint vulkanAbortFence{ RHIFenceHandle{ 1, 1 }, 20 };
			abortedTransaction.Abort(vulkanAbortFence);
			const TemporalHistoryManagerDiagnostics afterAbort = historyManager.GetDiagnostics();
			context.Check(abortGraphValid && abortedView.m_HasPreviousTemporalState &&
				abortedTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
				afterAbort.m_HistoryValid && afterAbort.m_ReadIndex == 1 &&
				afterAbort.m_AllocationGeneration == firstGeneration &&
				afterAbort.m_LastCommitted.m_GraphicsFence == firstFence &&
				viewHistory.m_Valid && viewHistory.m_NextJitterIndex == 1,
				"Vulkan abort fences gate lifetime without advancing logical temporal history");

			TemporalFrameTransaction noResolveTransaction;
			noResolveTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			const RenderView noResolveView = prepareDisplayView(noResolveTransaction);
			const RHIFencePoint noResolveFence{ RHIFenceHandle{ 1, 1 }, 30 };
			noResolveTransaction.CommitCompleted(noResolveFence);
			const TemporalHistoryManagerDiagnostics afterNoResolve =
				historyManager.GetDiagnostics();
			context.Check(noResolveView.m_HasPreviousTemporalState &&
				noResolveTransaction.GetJitterIndex() == 1 &&
				noResolveTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
				afterNoResolve.m_ReadIndex == 1 &&
				afterNoResolve.m_LastCommitted.m_GraphicsFence == firstFence &&
				viewHistory.m_NextJitterIndex == 1,
				"A completed frame without temporal resolve advances no temporal participant");
			context.Check(afterAbort.m_ReadIndex == firstCommitted.m_ReadIndex &&
				afterNoResolve.m_ReadIndex == firstCommitted.m_ReadIndex,
				"History age remains on the committed color/depth ping-pong read index across abort and non-resolve paths");

			TemporalFrameTransaction invalidFenceTransaction;
			invalidFenceTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			GGLAB_UNUSED(prepareDisplayView(invalidFenceTransaction));
			const bool invalidFenceGraphValid =
				buildHistoryGraph(invalidFenceTransaction, true, false);
			invalidFenceTransaction.CommitCompleted();
			const TemporalHistoryManagerDiagnostics afterInvalidFence =
				historyManager.GetDiagnostics();
			context.Check(invalidFenceGraphValid &&
				invalidFenceTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
				afterInvalidFence.m_HistoryValid && afterInvalidFence.m_ReadIndex == 1 &&
				afterInvalidFence.m_LastCommitted.m_GraphicsFence == firstFence &&
				viewHistory.m_NextJitterIndex == 1,
				"A completed transaction without a valid submitted fence cannot commit history");

			TemporalFrameTransaction fatalTransaction;
			fatalTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			GGLAB_UNUSED(prepareDisplayView(fatalTransaction));
			const bool fatalGraphValid = buildHistoryGraph(fatalTransaction, true, false);
			const RHIFencePoint fatalFence{ RHIFenceHandle{ 1, 1 }, 40 };
			fatalTransaction.InvalidateAfterFatal(fatalFence);
			const TemporalHistoryManagerDiagnostics afterFatal = historyManager.GetDiagnostics();
			const bool fatalFencesAreRetirementOnly =
				!afterFatal.m_PendingRetirementFences.empty() &&
				std::ranges::all_of(afterFatal.m_PendingRetirementFences,
					[&](const RHIFencePoint& fence) noexcept { return fence == fatalFence; });
			device.m_CompletedFenceValue = 39;
			texturePool.Tick();
			const uint32_t destroyedBeforeFatalFence = device.m_DestroyTextureCount;
			device.m_CompletedFenceValue = 40;
			texturePool.Tick();
			context.Check(fatalGraphValid &&
				fatalTransaction.GetState() == TemporalFrameTransactionState::Invalidated &&
				!viewHistory.m_Valid && !afterFatal.m_HasActiveHistory &&
				afterFatal.m_LastResetReason == TemporalHistoryResetReason::FatalSubmission &&
				afterFatal.m_PendingRetirementBytes > 0 && fatalFencesAreRetirementOnly &&
				destroyedBeforeFatalFence == 0 && device.m_DestroyTextureCount == 4,
				"Fatal-after-submit invalidates history and uses its fence only for retirement");

			TemporalFrameTransaction reenabledTransaction;
			reenabledTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			const TemporalHistoryManagerDiagnostics reenabled = historyManager.GetDiagnostics();
			reenabledTransaction.Abort();
			context.Check(
				reenabled.m_LastResetReason == TemporalHistoryResetReason::FatalSubmission,
				"Fresh history allocation preserves its explicit reset cause instead of ColdStart");
			ResolvedTemporalFramePlan disabledPlan = activePlan;
			disabledPlan.m_Status = TemporalAAFrameStatus::Disabled;
			disabledPlan.m_DisableReason = TemporalAADisableReason::NotRequested;
			disabledPlan.m_Requested = false;
			disabledPlan.m_Active = false;
			TemporalFrameTransaction disabledTransaction;
			disabledTransaction.Begin(
				viewHistory, objectHistory, disabledPlan, 64, 64, &historyManager);
			disabledTransaction.Abort();
			const TemporalHistoryManagerDiagnostics disabled = historyManager.GetDiagnostics();
			context.Check(reenabled.m_HasActiveHistory && !reenabled.m_HistoryValid &&
				reenabled.m_AllocationGeneration > firstGeneration &&
				device.m_CreateTextureCount == 8 && !disabled.m_HasActiveHistory &&
				disabled.m_LastResetReason == TemporalHistoryResetReason::Disabled &&
				device.m_DestroyTextureCount == 8,
				"Disabled history releases immediately and re-enable never revives retired allocations");

			TemporalFrameTransaction preResizeTransaction;
			preResizeTransaction.Begin(
				viewHistory, objectHistory, activePlan, 64, 64, &historyManager);
			GGLAB_UNUSED(prepareDisplayView(preResizeTransaction));
			const bool preResizeGraphValid =
				buildHistoryGraph(preResizeTransaction, false, false);
			const RHIFencePoint preResizeFence{ RHIFenceHandle{ 1, 1 }, 50 };
			preResizeTransaction.CommitCompleted(preResizeFence);
			const uint64_t preResizeGeneration =
				historyManager.GetDiagnostics().m_AllocationGeneration;
			TemporalFrameTransaction resizedTransaction;
			resizedTransaction.Begin(
				viewHistory, objectHistory, activePlan, 128, 72, &historyManager);
			const TemporalHistoryManagerDiagnostics resized = historyManager.GetDiagnostics();
			resizedTransaction.Abort();
			context.Check(preResizeGraphValid && resized.m_HasActiveHistory &&
				!resized.m_HistoryValid && resized.m_Compatibility.m_Width == 128 &&
				resized.m_Compatibility.m_Height == 72 &&
				resized.m_AllocationGeneration > preResizeGeneration &&
				resized.m_LastResetReason == TemporalHistoryResetReason::ExtentChanged &&
				resized.m_PendingRetirementFences.size() == 4 &&
				device.m_CreateTextureCount == 16,
				"Extent changes retire committed history and allocate a fresh invalid generation");

			ResolvedTemporalFramePlan unavailablePlan = activePlan;
			unavailablePlan.m_CoreAvailable = false;
			unavailablePlan.m_Active = false;
			unavailablePlan.m_Status = TemporalAAFrameStatus::Unavailable;
			unavailablePlan.m_DisableReason = TemporalAADisableReason::CoreCapabilityUnavailable;
			TemporalFrameTransaction unavailableTransaction;
			unavailableTransaction.Begin(
				viewHistory, objectHistory, unavailablePlan, 128, 72, &historyManager);
			unavailableTransaction.Abort();
			const TemporalHistoryManagerDiagnostics unavailable = historyManager.GetDiagnostics();
			context.Check(!unavailable.m_HasActiveHistory &&
				unavailable.m_LastResetReason == TemporalHistoryResetReason::AvailabilityChanged,
				"Requested TAA availability loss retires history separately from an explicit toggle off");

			historyManager.Shutdown();
			device.m_CompletedFenceValue = 50;
			texturePool.Tick();
			context.Check(device.m_DestroyTextureCount == 16 &&
				texturePool.GetDiagnostics().m_ActiveTextureCount == 0 &&
				texturePool.GetDiagnostics().m_PendingRetirementTextureCount == 0,
				"Temporal history shutdown leaves no active or pending persistent allocation");
		}

		void RunResourceStateAndPortabilityContractTests(SelfTestContext& context) noexcept
		{
			const RGPersistentTextureImportContract pendingTexture =
				ResolveRGPersistentTextureImportContract(false);
			const RGPersistentTextureImportContract initializedTexture =
				ResolveRGPersistentTextureImportContract(true);
			const RHIResourceState sampledState =
				ToRHIResourceState(RGTextureAccess::Sample, RHIStage::PixelShader);
			const RGPersistentTextureImportContract sampledTexture =
				ResolveRGPersistentTextureImportContract(true, sampledState);
			context.Check(pendingTexture.m_InitialState == UndefinedRHITextureState() &&
				pendingTexture.m_InitialContentValidity == RGContentValidity::Undefined &&
				initializedTexture.m_InitialState == CommonRHIResourceState() &&
				initializedTexture.m_InitialContentValidity == RGContentValidity::Defined &&
				sampledTexture.m_InitialState == sampledState &&
				sampledTexture.m_InitialContentValidity == RGContentValidity::Defined,
				"Persistent texture imports expose Undefined first use and their explicit terminal state thereafter");

			const RHIOwnedTextureCreateInfo ownedCreateInfo{};
			context.Check(ownedCreateInfo.m_InitialState == UndefinedRHITextureState(),
				"Owned texture create info defaults to Undefined");
			context.Check(IsRHIResourceStateValid(
				ownedCreateInfo.m_InitialState, RHIResourceStateUsage::TextureInitial),
				"Undefined is a valid owned texture initial state");
			context.Check(IsRHIResourceStateValid(
				ownedCreateInfo.m_InitialState, RHIResourceStateUsage::TextureBarrierBefore),
				"Undefined is a valid texture barrier source state");
			context.Check(!IsRHIResourceStateValid(
				ownedCreateInfo.m_InitialState, RHIResourceStateUsage::TextureBarrierAfter),
				"Undefined is rejected as a texture barrier destination state");
			context.Check(!IsRHIResourceStateValid(
				RHIResourceState{ .m_Layout = RHILayout::Unknown },
				RHIResourceStateUsage::TextureInitial),
				"Owned textures default to initial-only Undefined while Unknown remains invalid");
			context.Check(IsRHIResourceStateValid(
				PresentRHITextureState(), RHIResourceStateUsage::TextureBarrierAfter) &&
				!IsRHIResourceStateValid(
					PresentRHITextureState(), RHIResourceStateUsage::Buffer),
				"Present is an exact texture-only special state");
			context.Check(ToD3D12BarrierLayout(RHILayout::Undefined) ==
				D3D12_BARRIER_LAYOUT_UNDEFINED &&
				ToD3D12TextureInitialLayout(RHILayout::Undefined) ==
				D3D12_BARRIER_LAYOUT_COMMON &&
				ToD3D12ResourceStates(UndefinedRHITextureState()) == D3D12_RESOURCE_STATE_COMMON,
				"DX12 preserves Undefined for discard barriers while owned textures start in COMMON");

			D3D12_RESOURCE_DESC discardResourceDesc{};
			discardResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			discardResourceDesc.Width = 8;
			discardResourceDesc.Height = 8;
			discardResourceDesc.DepthOrArraySize = 1;
			discardResourceDesc.MipLevels = 1;
			discardResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			discardResourceDesc.SampleDesc = { 1, 0 };
			const RHIResourceState storageWriteState{
				.m_Stages = RHIStage::ComputeShader,
				.m_Access = RHIAccess::UnorderedAccess,
				.m_Layout = RHILayout::UnorderedAccess,
			};
			const D3D12_TEXTURE_BARRIER discardBarrier = BuildD3D12TextureBarrier({
				.m_Texture = RHITextureHandle{ 1, 1 },
				.m_Before = UndefinedRHITextureState(),
				.m_After = storageWriteState,
				}, reinterpret_cast<ID3D12Resource*>(uintptr_t{ 0x1234 }), discardResourceDesc);
			context.Check(discardBarrier.LayoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED &&
				discardBarrier.LayoutAfter == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS &&
				discardBarrier.Flags == D3D12_TEXTURE_BARRIER_FLAG_DISCARD,
				"DX12 first-use barriers discard stale pooled texture layouts and contents");

			const RHITextureDesc graphTextureDesc{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Extent = { 8, 8, 1 },
			};
			RenderGraph initializedGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			initializedGraph.AddPass<TextureStorageAccessPassData>("InitializeTexture",
				[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
				{
					data.m_Texture = builder.CreateTexture("UndefinedTexture", graphTextureDesc);
					builder.WriteInPlace(data.m_Texture, RGTextureAccess::StorageWrite);
					builder.SideEffect();
				});
			const bool initializedCompiled = initializedGraph.Compile();
			RGSnapshot initializedSnapshot;
			BuildRenderGraphSnapshot(initializedGraph, initializedSnapshot);
			context.Check(initializedCompiled && initializedSnapshot.m_Resources.size() == 1 &&
				initializedSnapshot.m_Resources[0].m_InitialBarrierState == UndefinedRHITextureState(),
				"RenderGraph-created textures begin in Undefined state and Write defines contents");
			const auto* initializedPlan = initializedGraph.GetExecutionPlan();
			const RHIResourceState initializedWriteState =
				ToRHIResourceState(RGTextureAccess::StorageWrite);
			context.Check(initializedCompiled && initializedPlan &&
				initializedPlan->GetPasses().size() == 1 &&
				initializedPlan->GetPasses()[0].m_PreBarriers.size() == 1 &&
				initializedPlan->GetPasses()[0].m_PreBarriers[0].m_Before ==
				UndefinedRHITextureState() &&
				initializedPlan->GetPasses()[0].m_PreBarriers[0].m_After ==
				initializedWriteState && initializedPlan->GetPasses()[0].m_PostBarriers.empty(),
				"Discardable graph textures keep their terminal layout for the next Undefined acquire");

			RecordingDevice reuseDevice;
			TransientResourcePool reusePool(&reuseDevice);
			auto discardAllocation = reusePool.AcquireTexture(graphTextureDesc, "Discardable");
			const TransientResourcePoolSlot discardSlot = discardAllocation.m_PoolSlot;
			reusePool.RetireTexture(std::move(discardAllocation), {});
			reusePool.Tick();
			auto commonAllocation = reusePool.AcquireTexture(graphTextureDesc, "PreserveCommon",
				RHIResourceDebugBindingMode::Aliased,
				TransientTextureReuseMode::PreserveCommon);
			const TransientResourcePoolSlot commonSlot = commonAllocation.m_PoolSlot;
			reusePool.RetireTexture(std::move(commonAllocation), {});
			reusePool.Tick();
			auto reusedDiscardAllocation =
				reusePool.AcquireTexture(graphTextureDesc, "DiscardableAgain");
			context.Check(discardSlot.IsValid() && commonSlot.IsValid() &&
				discardSlot != commonSlot && reusedDiscardAllocation.m_PoolSlot == discardSlot,
				"Transient texture reuse keeps discardable and Common-preserving contracts isolated");

			auto hasUndefinedRead = [](const RenderGraph& graph) noexcept
				{
					return std::ranges::any_of(graph.GetCompileDiagnostics(),
						[](const RGCompileDiagnostic& diagnostic) noexcept
						{
							return diagnostic.m_Code == RGCompileDiagnosticCode::UndefinedContentRead;
						});
				};
			RenderGraph transientReadGraph({
				.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
				});
			transientReadGraph.AddPass<StorageAccessPassData>("ReadUndefinedBuffer",
				[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
				{
					data.m_Buffer = builder.CreateBuffer("UndefinedBuffer");
					data.m_Buffer = builder.Read(data.m_Buffer, RGBufferAccess::StructuredRead);
					builder.SideEffect();
				});
			context.Check(!transientReadGraph.Compile() && hasUndefinedRead(transientReadGraph),
				"RenderGraph reports release-visible texture/buffer read-before-write diagnostics");

			{
				RecordingDevice recordingDevice;
				const RHITextureDesc importedDesc{
					.m_Format = RHIFormat::R8G8B8A8Unorm,
					.m_Usage = RHITextureUsage::Sampled,
					.m_Extent = { 8, 8, 1 },
				};
				auto buildImportedRead = [&](RGContentValidity validity)
					{
						auto graph = std::make_unique<RenderGraph>(RenderGraph::CreateInfo{
							.m_Device = &recordingDevice,
							.m_TransientResourcePool =
								reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
							});
						graph->AddPass<TextureStorageAccessPassData>("ReadImportedTexture",
							[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
							{
								data.m_Texture = builder.ImportTexture("ImportedTexture",
									RHITextureHandle{ 9, 1 }, importedDesc, UndefinedRHITextureState(), validity);
								data.m_Texture = builder.Read(data.m_Texture, RGTextureAccess::Sample);
								builder.SideEffect();
							});
						return graph;
					};
				auto undefinedImportGraph = buildImportedRead(RGContentValidity::Undefined);
				auto definedImportGraph = buildImportedRead(RGContentValidity::Defined);
				context.Check(!undefinedImportGraph->Compile() && hasUndefinedRead(*undefinedImportGraph) &&
					definedImportGraph->Compile(),
					"Imported state and content validity are explicit, independent contracts");
			}

			RHITextureDesc cubeDesc{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Usage = RHITextureUsage::Sampled,
				.m_Extent = { 8, 8, 1 },
				.m_ArraySize = 6,
			};
			const RHITextureViewDesc cubeView{
				.m_Type = RHITextureViewType::ShaderResource,
				.m_Dimension = RHITextureViewDimension::TextureCube,
				.m_Subresources = {.m_ArraySliceCount = 6 },
			};
			context.Check(ValidateRHITextureViewDesc(cubeDesc, cubeView).m_Error ==
				RHITextureValidationError::MissingCubeCompatible,
				"Cube views require an explicit CubeCompatible resource flag");
			cubeDesc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;
			context.Check(ValidateRHITextureViewDesc(cubeDesc, cubeView).IsValid(),
				"CubeCompatible resources accept complete cube views");

			const RHIPortabilityCapabilities vulkanCapabilities{};
			RHITextureViewDesc minLodView{};
			minLodView.m_ResourceMinLODClamp = 1.0f;
			RHISamplerDesc customBorderSampler{};
			customBorderSampler.m_AddressU = RHITextureAddressMode::Border;
			customBorderSampler.m_BorderColor[0] = 0.5f;
			RHIGraphicsPipelineDesc pipelineDesc{};
			context.Check(
				IsRHIPrimitiveTopologyCompatible(
					RHIPrimitiveTopologyType::Triangle, RHIPrimitiveTopology::TriangleList) &&
				!IsRHIPrimitiveTopologyCompatible(
					RHIPrimitiveTopologyType::Line, RHIPrimitiveTopology::TriangleList) &&
				!IsRHIPrimitiveTopologyCompatible(
					RHIPrimitiveTopologyType::Patch, RHIPrimitiveTopology::TriangleList),
				"Primitive topology compatibility is a backend-neutral pipeline contract");
			const RHIIndexBufferBinding indexBinding{
				.m_Offset = 16,
				.m_SizeInBytes = 24,
				.m_Format = RHIFormat::R32Uint,
			};
			RHIIndexBufferBinding misalignedBinding = indexBinding;
			misalignedBinding.m_Offset = 18;
			RHIIndexBufferBinding misalignedSizeBinding = indexBinding;
			misalignedSizeBinding.m_SizeInBytes = 22;
			RHIIndexBufferBinding unknownFormatBinding = indexBinding;
			unknownFormatBinding.m_Format = RHIFormat::Unknown;
			context.Check(
				IsRHIIndexBufferBindingRangeValid(indexBinding, 64) &&
				!IsRHIIndexBufferBindingRangeValid(misalignedBinding, 64) &&
				!IsRHIIndexBufferBindingRangeValid(misalignedSizeBinding, 64) &&
				!IsRHIIndexBufferBindingRangeValid(unknownFormatBinding, 64) &&
				!IsRHIIndexBufferBindingRangeValid(indexBinding, 32) &&
				IsRHIIndexBufferDrawRangeValid(indexBinding, 6, 0) &&
				IsRHIIndexBufferDrawRangeValid(indexBinding, 2, 4) &&
				!IsRHIIndexBufferDrawRangeValid(indexBinding, 3, 4) &&
				!IsRHIIndexBufferDrawRangeValid(indexBinding, 1, 6) &&
				!IsRHIIndexBufferDrawRangeValid(indexBinding, 1, UINT32_MAX),
				"Index buffer bindings and indexed draws stay within the declared RHI range");
			pipelineDesc.m_VertexInput.m_VertexBufferCount = 1;
			pipelineDesc.m_VertexInput.m_VertexBuffers[0].m_InputRate =
				RHIVertexInputRate::PerInstance;
			pipelineDesc.m_VertexInput.m_VertexBuffers[0].m_InstanceStepRate = 2;
			context.Check(
				ValidateRHITextureViewPortability(minLodView, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::ImageViewMinLodUnsupported &&
				ValidateRHISamplerPortability(customBorderSampler, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::CustomBorderColorUnsupported &&
				ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::InstanceDivisorUnsupported,
				"Vulkan profile rejects unsupported view, sampler, and instance-divisor semantics");
			pipelineDesc = {};
			pipelineDesc.m_Rasterizer.m_FillMode = RHIFillMode::Wireframe;
			const bool rejectsWireframe =
				ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::WireframeUnsupported;
			pipelineDesc = {};
			pipelineDesc.m_Rasterizer.m_DepthClipEnable = false;
			const bool rejectsDepthClamp =
				ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::DepthClampUnsupported;
			pipelineDesc = {};
			pipelineDesc.m_Rasterizer.m_DepthBiasClamp = 1.0f;
			const bool rejectsBiasClamp =
				ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::DepthBiasClampUnsupported;
			pipelineDesc = {};
			pipelineDesc.m_RenderTargetCount = 2;
			pipelineDesc.m_Blend.m_RenderTargets[1].m_BlendEnable = true;
			const bool rejectsIndependentBlend =
				ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::IndependentBlendUnsupported;
			pipelineDesc = {};
			pipelineDesc.m_SampleQuality = 1;
			const bool rejectsSampleQuality =
				ValidateRHIGraphicsPipelinePortability(pipelineDesc, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::SampleQualityUnsupported;
			context.Check(rejectsWireframe && rejectsDepthClamp && rejectsBiasClamp &&
				rejectsIndependentBlend && rejectsSampleQuality,
				"Vulkan profile rejects unsupported rasterizer, blend, and sample-quality semantics");

			const RHIResourceState shaderReadState{
				.m_Stages = RHIStage::PixelShader,
				.m_Access = RHIAccess::ShaderResource,
				.m_Layout = RHILayout::ShaderResource,
			};

			{
				RecordingTransferContext transferContext;
				TransferBatch batch(transferContext);

				const RHITextureHandle uploadedTexture{ 3, 1 };
				const RHIBufferHandle uploadedBuffer{ 4, 1 };
				const uint32_t bufferData = 17;
				const bool transferRecorded = batch.UploadTexture(uploadedTexture, {},
					UndefinedRHITextureState(), shaderReadState) &&
					batch.UploadBuffer(uploadedBuffer, 0, &bufferData, sizeof(bufferData)) &&
					batch.UploadBuffer(uploadedBuffer, sizeof(bufferData), &bufferData,
						sizeof(bufferData));
				const RHITransferSubmission submission = batch.Submit(false);
				context.Check(transferRecorded && transferContext.m_TextureBarriers.size() == 2 &&
					transferContext.m_BufferBarriers.size() == 4 &&
					transferContext.m_TextureBarriers[0].m_Before == UndefinedRHITextureState() &&
					transferContext.m_TextureBarriers[1].m_After == shaderReadState &&
					transferContext.m_BufferBarriers.front().m_Before.m_Layout == RHILayout::Common &&
					transferContext.m_BufferBarriers.front().m_After.m_Layout == RHILayout::CopyDest &&
					transferContext.m_BufferBarriers.back().m_After.m_Layout == RHILayout::Common &&
					submission.m_Completion.IsValid() && submission.m_Publications.size() == 2 &&
					submission.m_Publications[0].m_Type == RHIResourceType::Texture &&
					submission.m_Publications[0].m_PublishedState == shaderReadState &&
					submission.m_Publications[1].m_Type == RHIResourceType::Buffer,
					"Transfer submission publishes one terminal state per resource with its completion fence");
			}

			// --- Subresource-granular texture content validity ---
			const RHITextureDesc mipTextureDesc{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Extent = { 8, 8, 1 },
				.m_MipLevels = 2,
			};
			const RHISubresourceRange colorMip0{
				.m_MipCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange colorMip1{
				.m_BaseMip = 1,
				.m_MipCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};

			{
				RenderGraph mipReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId mipTextureId{};
				mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip0",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
						builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, colorMip0);
						builder.SideEffect();
					});
				mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip0",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(
							mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip0);
						builder.SideEffect();
					});
				context.Check(mipReadGraph.Compile(),
					"Undefined texture written on mip0 accepts reads of mip0");
			}
			{
				RenderGraph mipReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId mipTextureId{};
				mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip0",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
						builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, colorMip0);
						builder.SideEffect();
					});
				mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(
							mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip1);
						builder.SideEffect();
					});
				context.Check(!mipReadGraph.Compile() && hasUndefinedRead(mipReadGraph),
					"Undefined texture written on mip0 rejects reads of mip1");
			}
			{
				RenderGraph mipReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId mipTextureId{};
				mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip0",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
						builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, colorMip0);
						builder.SideEffect();
					});
				mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, colorMip1);
						builder.SideEffect();
					});
				mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip0AfterMip1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(
							mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip0);
						builder.SideEffect();
					});
				context.Check(mipReadGraph.Compile(),
					"Version inheritance preserves previously defined mip0 after mip1 write");
			}
			{
				RenderGraph mipReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId mipTextureId{};
				mipReadGraph.AddPass<TextureStorageAccessPassData>("WriteMip1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						mipTextureId = builder.CreateTexture("MipValidityTexture", mipTextureDesc);
						builder.WriteInPlace(mipTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, colorMip1);
						builder.SideEffect();
					});
				mipReadGraph.AddPass<TextureStorageAccessPassData>("ReadMip1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(
							mipTextureId, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip1);
						builder.SideEffect();
					});
				context.Check(mipReadGraph.Compile(),
					"Undefined texture written on mip1 accepts reads of mip1");
			}

			// Array slice partial write/read.
			const RHITextureDesc arrayTextureDesc{
				.m_Format = RHIFormat::R8G8B8A8Unorm,
				.m_Extent = { 8, 8, 1 },
				.m_ArraySize = 2,
			};
			const RHISubresourceRange slice0Range{
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			const RHISubresourceRange slice1Range{
				.m_BaseArraySlice = 1,
				.m_ArraySliceCount = 1,
				.m_Aspects = RHITextureAspect::Color,
			};
			{
				RenderGraph sliceReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId sliceTextureId{};
				sliceReadGraph.AddPass<TextureStorageAccessPassData>("WriteSlice0",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						sliceTextureId = builder.CreateTexture("SliceValidityTexture", arrayTextureDesc);
						builder.WriteInPlace(sliceTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, slice0Range);
						builder.SideEffect();
					});
				sliceReadGraph.AddPass<TextureStorageAccessPassData>("ReadSlice1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(sliceTextureId, RGTextureAccess::Sample,
							RHIStage::PixelShader, slice1Range);
						builder.SideEffect();
					});
				context.Check(!sliceReadGraph.Compile() && hasUndefinedRead(sliceReadGraph),
					"Array slice writes do not define other slices");
			}
			{
				RenderGraph sliceReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId sliceTextureId{};
				sliceReadGraph.AddPass<TextureStorageAccessPassData>("WriteSlice1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						sliceTextureId = builder.CreateTexture("SliceValidityTexture", arrayTextureDesc);
						builder.WriteInPlace(sliceTextureId, RGTextureAccess::RenderTarget,
							RHIStage::RenderTarget, slice1Range);
						builder.SideEffect();
					});
				sliceReadGraph.AddPass<TextureStorageAccessPassData>("ReadSlice1",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(sliceTextureId, RGTextureAccess::Sample,
							RHIStage::PixelShader, slice1Range);
						builder.SideEffect();
					});
				context.Check(sliceReadGraph.Compile(),
					"Array slice writes define their own slice");
			}

			// Depth/stencil aspect granularity.
			const RHITextureDesc depthStencilTextureDesc{
				.m_Format = RHIFormat::D24UnormS8Uint,
				.m_Extent = { 8, 8, 1 },
			};
			const RHISubresourceRange depthAspectRange{ .m_Aspects = RHITextureAspect::Depth };
			const RHISubresourceRange stencilAspectRange{ .m_Aspects = RHITextureAspect::Stencil };
			{
				RenderGraph aspectReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId aspectTextureId{};
				aspectReadGraph.AddPass<TextureStorageAccessPassData>("WriteDepthAspect",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						aspectTextureId =
							builder.CreateTexture("AspectValidityTexture", depthStencilTextureDesc);
						builder.WriteInPlace(aspectTextureId, RGTextureAccess::DepthStencilWrite,
							RHIStage::DepthStencil, depthAspectRange);
						builder.SideEffect();
					});
				aspectReadGraph.AddPass<TextureStorageAccessPassData>("ReadDepthAspect",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(aspectTextureId, RGTextureAccess::DepthStencilRead,
							RHIStage::DepthStencil, depthAspectRange);
						builder.SideEffect();
					});
				aspectReadGraph.AddPass<TextureStorageAccessPassData>("ReadStencilAspect",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(aspectTextureId, RGTextureAccess::DepthStencilRead,
							RHIStage::DepthStencil, stencilAspectRange);
						builder.SideEffect();
					});
				context.Check(!aspectReadGraph.Compile() && hasUndefinedRead(aspectReadGraph),
					"Depth aspect writes do not define the stencil aspect");
			}
			{
				RenderGraph aspectWriteGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				RGTextureId aspectTextureId{};
				aspectWriteGraph.AddPass<TextureStorageAccessPassData>("WriteBothAspects",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						aspectTextureId =
							builder.CreateTexture("AspectValidityTexture", depthStencilTextureDesc);
						builder.WriteInPlace(aspectTextureId, RGTextureAccess::DepthStencilWrite,
							RHIStage::DepthStencil, depthAspectRange);
						builder.SideEffect();
					});
				aspectWriteGraph.AddPass<TextureStorageAccessPassData>("WriteStencilAspect",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.WriteInPlace(aspectTextureId, RGTextureAccess::DepthStencilWrite,
							RHIStage::DepthStencil, stencilAspectRange);
						builder.SideEffect();
					});
				aspectWriteGraph.AddPass<TextureStorageAccessPassData>("ReadStencilAspect",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData&)
					{
						builder.Read(aspectTextureId, RGTextureAccess::DepthStencilRead,
							RHIStage::DepthStencil, stencilAspectRange);
						builder.SideEffect();
					});
				context.Check(aspectWriteGraph.Compile(),
					"Stencil aspect writes define the stencil aspect");
			}

			// Imported Defined texture with partial reads.
			{
				RecordingDevice recordingDevice;
				RenderGraph importedPartialGraph({
					.m_Device = &recordingDevice,
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				importedPartialGraph.AddPass<TextureStorageAccessPassData>("ReadImportedPartial",
					[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
					{
						data.m_Texture =
							builder.ImportTexture("PartialImportedTexture", RHITextureHandle{ 10, 1 },
								mipTextureDesc, UndefinedRHITextureState(), RGContentValidity::Defined);
						data.m_Texture = builder.Read(
							data.m_Texture, RGTextureAccess::Sample, RHIStage::PixelShader, colorMip1);
						builder.SideEffect();
					});
				context.Check(importedPartialGraph.Compile(),
					"Imported Defined textures support partial reads");
			}

			// Read-before-write remains a declaration contract even when the
			// reading pass can be culled.
			{
				RenderGraph cullableReadGraph({
					.m_Device = reinterpret_cast<RHIDevice*>(uintptr_t{1}),
					.m_TransientResourcePool =
						reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
					});
				cullableReadGraph.AddPass<StorageAccessPassData>("CullableUndefinedRead",
					[](RenderGraph::RGBuilder& builder, StorageAccessPassData& data)
					{
						data.m_Buffer = builder.CreateBuffer("CullableBuffer");
						data.m_Buffer =
							builder.Read(data.m_Buffer, RGBufferAccess::StructuredRead);
					});
				context.Check(!cullableReadGraph.Compile() && hasUndefinedRead(cullableReadGraph),
					"Cullable passes still report read-before-write declarations");
			}

			// --- Portability fixed-array boundaries ---
			RHIGraphicsPipelineDesc maxCountPipeline{};
			maxCountPipeline.m_VertexInput.m_VertexBufferCount =
				RHIVertexInputLayoutDesc::MaxVertexBuffers;
			maxCountPipeline.m_RenderTargetCount = RHIGraphicsPipelineDesc::MaxRenderTargets;
			context.Check(ValidateRHIGraphicsPipelinePortability(
				maxCountPipeline, vulkanCapabilities).IsValid(),
				"Pipeline portability accepts maximum vertex buffer and render target counts");
			RHIGraphicsPipelineDesc vertexOverflowPipeline{};
			vertexOverflowPipeline.m_VertexInput.m_VertexBufferCount =
				RHIVertexInputLayoutDesc::MaxVertexBuffers + 1;
			context.Check(ValidateRHIGraphicsPipelinePortability(
				vertexOverflowPipeline, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::VertexBufferCountExceedsLimit,
				"Pipeline portability rejects vertex buffer counts above the input layout limit");
			RHIGraphicsPipelineDesc targetOverflowPipeline{};
			targetOverflowPipeline.m_RenderTargetCount =
				RHIGraphicsPipelineDesc::MaxRenderTargets + 1;
			context.Check(ValidateRHIGraphicsPipelinePortability(
				targetOverflowPipeline, vulkanCapabilities).m_Error ==
				RHIPortabilityValidationError::RenderTargetCountExceedsLimit,
				"Pipeline portability rejects render target counts above the pipeline limit");

			// --- Portability rejection reason text ---
			context.Check(
				GetRHIPortabilityValidationErrorText(
					RHIPortabilityValidationError::ImageViewMinLodUnsupported) ==
				"image view min LOD clamp is not supported" &&
				!GetRHIPortabilityValidationErrorText(
					RHIPortabilityValidationError::None).empty() &&
				GetRHIPortabilityValidationErrorText(
					RHIPortabilityValidationError::ImageViewMinLodUnsupported) !=
				GetRHIPortabilityValidationErrorText(
					RHIPortabilityValidationError::None),
				"Portability rejection reasons expose stable, distinct text");

			// --- TransferBatch poisoned state ---
			{
				RecordingTransferContext failingContext;
				TransferBatch failingBatch(failingContext);
				const RHITextureHandle failTexture{ 5, 1 };
				const bool firstUpload = failingBatch.UploadTexture(
					failTexture, {}, UndefinedRHITextureState(), shaderReadState);
				failingContext.m_FailUploads = true;
				const bool failedUpload = failingBatch.UploadTexture(
					failTexture, {}, UndefinedRHITextureState(), shaderReadState);
				context.Check(firstUpload && !failedUpload && failingBatch.IsFailed() &&
					failingContext.m_TextureBarriers.size() == 3,
					"UploadTexture failure after recorded barriers poisons the batch");
				const RHITransferSubmission failedSubmission = failingBatch.Submit(false);
				context.Check(failingContext.m_SubmitCallCount == 0 &&
					failingContext.m_AbortCallCount == 1 &&
					!failedSubmission.m_Completion.IsValid() &&
					failedSubmission.m_Publications.empty(),
					"Poisoned batches abort without submitting and return an invalid submission");
			}
			{
				RecordingTransferContext readbackContext;
				TransferBatch readbackBatch(readbackContext);
				const RHITextureReadbackRequest request = readbackBatch.ReadbackTexture(
					RHITextureHandle{ 6, 1 }, RHITextureDesc{
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Usage = RHITextureUsage::CopySource,
						.m_Extent = { 1, 1, 1 },
					});
				const RHITransferSubmission submission = readbackBatch.Submit(false);
				context.Check(!request.IsValid() && readbackBatch.IsFailed() &&
					readbackContext.m_TextureBarriers.size() == 1 &&
					readbackContext.m_AbortCallCount == 1 &&
					readbackContext.m_SubmitCallCount == 0 &&
					!submission.m_Completion.IsValid(),
					"Readback allocation failure poisons and aborts its partially recorded state transition");
			}
			{
				RecordingTransferContext readbackContext;
				readbackContext.m_FailReadbacks = false;
				TransferBatch readbackBatch(readbackContext);
				const RHITextureHandle readbackTexture{ 6, 1 };
				const RHITextureReadbackRequest request = readbackBatch.ReadbackTexture(
					readbackTexture, RHITextureDesc{
						.m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Usage = RHITextureUsage::CopySource,
						.m_Extent = { 1, 1, 1 },
					});
				const RHITransferSubmission submission = readbackBatch.Submit(false);
				const RHIResourceState commonState = CommonRHIResourceState();
				context.Check(request.IsValid() && readbackContext.m_TextureBarriers.size() == 2 &&
					readbackContext.m_TextureBarriers[0].m_Before == commonState &&
					readbackContext.m_TextureBarriers[0].m_After.m_Layout == RHILayout::CopySource &&
					readbackContext.m_TextureBarriers[1].m_Before.m_Layout == RHILayout::CopySource &&
					readbackContext.m_TextureBarriers[1].m_After == commonState &&
					submission.m_Completion.IsValid() && submission.m_Publications.size() == 2 &&
					submission.m_Publications[0].m_Texture == readbackTexture &&
					submission.m_Publications[0].m_PublishedState == commonState &&
					submission.m_Publications[1].m_Buffer == request.m_Buffer.Get() &&
					submission.m_Publications[1].m_PublishedState.m_Access == RHIAccess::CopyDest,
					"Texture readback returns its source to Common and publishes both terminal states");
			}
			{
				RecordingTransferContext submitFailureContext;
				TransferBatch submitFailureBatch(submitFailureContext);
				const uint32_t payload = 23;
				const bool uploadRecorded = submitFailureBatch.UploadBuffer(
					RHIBufferHandle{ 8, 1 }, 0, &payload, sizeof(payload));
				submitFailureContext.m_FailSubmit = true;
				const RHITransferSubmission submission = submitFailureBatch.Submit(false);
				context.Check(uploadRecorded && submitFailureBatch.IsFailed() &&
					submitFailureContext.m_SubmitCallCount == 1 &&
					!submission.m_Completion.IsValid() && submission.m_Publications.empty(),
					"Native submission failure discards the publication manifest");
			}
			{
				RecordingTransferContext moveContext;
				TransferBatch moveSource(moveContext);
				const RHITextureHandle moveTexture{ 7, 1 };
				GGLAB_UNUSED(moveSource.UploadTexture(
					moveTexture, {}, UndefinedRHITextureState(), shaderReadState));
				moveContext.m_FailUploads = true;
				GGLAB_UNUSED(moveSource.UploadTexture(
					moveTexture, {}, UndefinedRHITextureState(), shaderReadState));
				TransferBatch moveTarget(std::move(moveSource));
				context.Check(moveTarget.IsFailed(),
					"Move construction preserves the poisoned state");
				const RHITransferSubmission moveSubmission = moveTarget.Submit(false);
				context.Check(moveContext.m_SubmitCallCount == 0 &&
					moveContext.m_AbortCallCount == 1 && !moveSubmission.m_Completion.IsValid(),
					"Moved poisoned batches still abort without submitting");
			}

			// --- TransferBatch overlapping publication validation ---
			const RHIResourceState copyDestState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopyDest,
				.m_Layout = RHILayout::CopyDest,
			};
			const RHITextureHandle overlapTexture{ 6, 1 };
			const RHISubresourceRange colorMips{
				.m_MipCount = 2,
				.m_Aspects = RHITextureAspect::Color,
			};
			{
				RecordingTransferContext overlapContext;
				TransferBatch overlapBatch(overlapContext);
				const bool sameRangeAccepted =
					overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0) &&
					overlapBatch.PublishTexture(overlapTexture, copyDestState, colorMip0) &&
					overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0);
				const bool disjointAccepted =
					overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip1);
				const bool sameStateOverlapAccepted =
					overlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMips);
				context.Check(sameRangeAccepted && disjointAccepted && sameStateOverlapAccepted &&
					!overlapBatch.IsFailed(),
					"Exact, disjoint, and same-state overlapping texture publications are accepted");
				const RHITransferSubmission overlapSubmission = overlapBatch.Submit(false);
				context.Check(overlapSubmission.m_Completion.IsValid(),
					"Non-conflicting publications submit normally");
			}
			{
				RecordingTransferContext conflictContext;
				TransferBatch conflictBatch(conflictContext);
				GGLAB_UNUSED(conflictBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				const bool conflictRejected =
					!conflictBatch.PublishTexture(overlapTexture, copyDestState, colorMips);
				context.Check(conflictRejected && conflictBatch.IsFailed(),
					"Overlapping publications with conflicting terminal states poison the batch");
			}
			{
				RecordingTransferContext wholeContext;
				TransferBatch wholeBatch(wholeContext);
				GGLAB_UNUSED(
					wholeBatch.PublishTexture(overlapTexture, shaderReadState, std::nullopt));
				const bool wholeConflictRejected =
					!wholeBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
				context.Check(wholeConflictRejected && wholeBatch.IsFailed(),
					"Whole-resource publications overlap every partial range");
			}
			{
				RecordingTransferContext remainingContext;
				TransferBatch remainingBatch(remainingContext);
				const RHISubresourceRange remainingRange{ .m_Aspects = RHITextureAspect::Color };
				GGLAB_UNUSED(remainingBatch.PublishTexture(overlapTexture, shaderReadState, remainingRange));
				const bool remainingConflictRejected =
					!remainingBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
				context.Check(remainingConflictRejected && remainingBatch.IsFailed(),
					"Remaining-count ranges overlap explicit partial ranges");
			}

			// --- Exact-range updates must stay consistent with overlapping
			// publications (validate-first, mutate-second) ---
			const RHIResourceState copySourceState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::CopySource,
			};
			const RHISubresourceRange colorMips3{
				.m_MipCount = 3,
				.m_Aspects = RHITextureAspect::Color,
			};
			{
				// A narrow exact-range update must be rejected when a broader
				// overlapping publication keeps a different terminal state.
				RecordingTransferContext narrowUpdateContext;
				TransferBatch narrowUpdateBatch(narrowUpdateContext);
				GGLAB_UNUSED(
					narrowUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				GGLAB_UNUSED(
					narrowUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMips));
				const bool narrowUpdateRejected =
					!narrowUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
				context.Check(narrowUpdateRejected && narrowUpdateBatch.IsFailed(),
					"Exact-range updates cannot contradict a broader overlapping publication");
			}
			{
				// A broader exact-range update must be rejected when a narrower
				// overlapping publication keeps a different terminal state.
				RecordingTransferContext broadUpdateContext;
				TransferBatch broadUpdateBatch(broadUpdateContext);
				GGLAB_UNUSED(
					broadUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMips));
				GGLAB_UNUSED(
					broadUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				const bool broadUpdateRejected =
					!broadUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMips);
				context.Check(broadUpdateRejected && broadUpdateBatch.IsFailed(),
					"Broader exact-range updates cannot contradict a narrower overlapping publication");
			}
			{
				// A disjoint exact-range update must succeed without poisoning.
				RecordingTransferContext disjointUpdateContext;
				TransferBatch disjointUpdateBatch(disjointUpdateContext);
				GGLAB_UNUSED(
					disjointUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				GGLAB_UNUSED(
					disjointUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMip1));
				const bool disjointUpdateSucceeded =
					disjointUpdateBatch.PublishTexture(overlapTexture, copySourceState, colorMip0);
				context.Check(disjointUpdateSucceeded && !disjointUpdateBatch.IsFailed(),
					"Disjoint exact-range updates succeed without poisoning the batch");
			}
			{
				// An exact-range update must succeed when every overlapping
				// publication agrees with the updated terminal state.
				RecordingTransferContext agreeingOverlapContext;
				TransferBatch agreeingOverlapBatch(agreeingOverlapContext);
				GGLAB_UNUSED(
					agreeingOverlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				GGLAB_UNUSED(
					agreeingOverlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMips));
				const bool agreeingOverlapAccepted =
					agreeingOverlapBatch.PublishTexture(overlapTexture, shaderReadState, colorMips3);
				context.Check(agreeingOverlapAccepted && !agreeingOverlapBatch.IsFailed(),
					"Exact-range updates succeed when all overlapping entries agree on the state");
			}
			{
				// A partial exact-range update must be rejected when a
				// whole-resource publication keeps a different terminal state.
				RecordingTransferContext partialUpdateContext;
				TransferBatch partialUpdateBatch(partialUpdateContext);
				GGLAB_UNUSED(
					partialUpdateBatch.PublishTexture(overlapTexture, shaderReadState, std::nullopt));
				GGLAB_UNUSED(
					partialUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				const bool partialUpdateRejected =
					!partialUpdateBatch.PublishTexture(overlapTexture, copyDestState, colorMip0);
				context.Check(partialUpdateRejected && partialUpdateBatch.IsFailed(),
					"Partial exact-range updates cannot contradict a whole-resource publication");
			}
			{
				// A whole-resource exact-range update must be rejected when a
				// partial publication keeps a different terminal state.
				RecordingTransferContext wholeUpdateContext;
				TransferBatch wholeUpdateBatch(wholeUpdateContext);
				GGLAB_UNUSED(
					wholeUpdateBatch.PublishTexture(overlapTexture, shaderReadState, colorMip0));
				GGLAB_UNUSED(
					wholeUpdateBatch.PublishTexture(overlapTexture, shaderReadState, std::nullopt));
				const bool wholeUpdateRejected =
					!wholeUpdateBatch.PublishTexture(overlapTexture, copyDestState, std::nullopt);
				context.Check(wholeUpdateRejected && wholeUpdateBatch.IsFailed(),
					"Whole-resource exact-range updates cannot contradict a partial publication");
			}
			{
				// A Remaining-count exact-range update must be rejected when a
				// narrower overlapping publication keeps a different terminal state.
				RecordingTransferContext remainingUpdateContext;
				TransferBatch remainingUpdateBatch(remainingUpdateContext);
				const RHISubresourceRange remainingRange{ .m_Aspects = RHITextureAspect::Color };
				GGLAB_UNUSED(remainingUpdateBatch.PublishTexture(
					overlapTexture, shaderReadState, colorMip0));
				GGLAB_UNUSED(remainingUpdateBatch.PublishTexture(
					overlapTexture, shaderReadState, remainingRange));
				const bool remainingUpdateRejected =
					!remainingUpdateBatch.PublishTexture(
						overlapTexture, copyDestState, remainingRange);
				context.Check(remainingUpdateRejected && remainingUpdateBatch.IsFailed(),
					"Remaining-count exact-range updates cannot contradict a narrower overlapping publication");
			}
		}

		void RunTemporalCompatibilityAndHistoryContractTests(SelfTestContext& context) noexcept
		{
			static_assert(sizeof(ViewGPU) == 480);
			static_assert(offsetof(ViewGPU, PreviousViewMat) == 256);
			static_assert(offsetof(ViewGPU, PreviousDepthReconstructionParams) == 400);
			static_assert(offsetof(ViewGPU, CurrentJitterUV) == 432);
			static_assert(offsetof(ViewGPU, PreviousDepthConvention) == 464);

			const Vector2 staticMotion = ComputeTemporalMotionUV(
				Vector4(0.0f, 0.0f, 0.5f, 1.0f), Vector4(0.0f, 0.0f, 0.5f, 1.0f));
			const Vector2 cameraMotion = ComputeTemporalMotionUV(
				Vector4(-0.2f, 0.0f, 0.5f, 1.0f), Vector4(0.0f, 0.0f, 0.5f, 1.0f));
			const Vector2 objectMotion = ComputeTemporalMotionUV(
				Vector4(0.4f, 0.2f, 0.5f, 1.0f), Vector4(0.0f, 0.0f, 0.5f, 1.0f));
			const TemporalMotionReadbackSample objectMotionReadback =
				ResolveTemporalMotionReadbackSample(objectMotion, 100, 50);
			context.Check(NearlyEqual(staticMotion, Vector2::Zero) &&
				NearlyEqual(cameraMotion, Vector2(-0.1f, 0.0f)) &&
				NearlyEqual(objectMotion, Vector2(0.2f, -0.1f)) &&
				objectMotionReadback.m_Valid &&
				NearlyEqual(objectMotionReadback.m_MotionPixels, Vector2(20.0f, -5.0f)) &&
				NearlyEqual(objectMotionReadback.m_MagnitudePixels, std::sqrt(425.0f)) &&
				!ResolveTemporalMotionReadbackSample(objectMotion, 0, 50).m_Valid,
				"Temporal motion uses current-minus-previous top-left UV delta and deterministic pixel magnitude");

			const Vector2 currentJitterUV =
				temporal::JitterPixelsToUV(Vector2(0.25f, -0.25f), 100, 50);
			const Vector2 previousJitterUV =
				temporal::JitterPixelsToUV(Vector2(-0.25f, 0.25f), 100, 50);
			const Vector4 staticUnjitteredClip(0.2f, -0.1f, 0.5f, 1.0f);
			const Vector4 currentRasterClip = temporal::ApplyJitterToClipPosition(
				staticUnjitteredClip,
				temporal::JitterPixelsToNDC(Vector2(0.25f, -0.25f), 100, 50));
			const Vector4 previousRasterClip = temporal::ApplyJitterToClipPosition(
				staticUnjitteredClip,
				temporal::JitterPixelsToNDC(Vector2(-0.25f, 0.25f), 100, 50));
			const Vector2 jitterOnlyRasterMotion =
				ComputeTemporalMotionUV(currentRasterClip, previousRasterClip);
			const Vector2 currentUV(0.5f, 0.5f);
			const Vector2 previousRasterUV =
				temporal::ReprojectToPreviousUV(currentUV, jitterOnlyRasterMotion);
			const Vector2 staticHistoryMotion = ResolveTemporalHistoryMotionUV(
				jitterOnlyRasterMotion, currentJitterUV, previousJitterUV);
			const Vector2 previousAccumulatedUV =
				temporal::ReprojectToPreviousUV(currentUV, staticHistoryMotion);
			context.Check(NearlyEqual(jitterOnlyRasterMotion,
					currentJitterUV - previousJitterUV) &&
				!NearlyEqual(previousRasterUV, currentUV) &&
				NearlyEqual(staticHistoryMotion, Vector2::Zero) &&
				NearlyEqual(previousAccumulatedUV, currentUV),
				"TAA removes raster jitter displacement when reprojecting accumulated history color");

			const Vector2 unjitteredMotionUV(0.03f, -0.02f);
			const Vector2 movingRasterMotionUV =
				unjitteredMotionUV + currentJitterUV - previousJitterUV;
			const Vector2 movingHistoryMotionUV = ResolveTemporalHistoryMotionUV(
				movingRasterMotionUV, currentJitterUV, previousJitterUV);
			const Vector2 movingPreviousHistoryUV =
				temporal::ReprojectToPreviousUV(currentUV, movingHistoryMotionUV);
			context.Check(NearlyEqual(movingHistoryMotionUV, unjitteredMotionUV) &&
				NearlyEqual(movingPreviousHistoryUV, currentUV - unjitteredMotionUV),
				"TAA preserves real temporal motion while removing raster jitter displacement");

			const Vector2 currentSkyUV(0.15f, 0.08f);
			const Vector2 previousSkyRasterUV =
				temporal::ReprojectToPreviousUV(currentSkyUV, jitterOnlyRasterMotion);
			const Vector2 skyRasterMotionUV = currentSkyUV - previousSkyRasterUV;
			const Vector2 skyHistoryMotionUV = ResolveTemporalHistoryMotionUV(
				skyRasterMotionUV, currentJitterUV, previousJitterUV);
			const Vector2 previousSkyHistoryUV =
				temporal::ReprojectToPreviousUV(currentSkyUV, skyHistoryMotionUV);
			context.Check(NearlyEqual(skyHistoryMotionUV, Vector2::Zero) &&
				NearlyEqual(previousSkyHistoryUV, currentSkyUV),
				"Static sky reprojection removes jitter after resolving its previous raster UV");

			const Vector2 edgeCurrentUV(0.002f, 0.5f);
			const Vector2 edgePreviousRasterUV =
				temporal::ReprojectToPreviousUV(edgeCurrentUV, jitterOnlyRasterMotion);
			const Vector2 edgeHistoryMotionUV = ResolveTemporalHistoryMotionUV(
				jitterOnlyRasterMotion, currentJitterUV, previousJitterUV);
			const Vector2 edgePreviousHistoryUV =
				temporal::ReprojectToPreviousUV(edgeCurrentUV, edgeHistoryMotionUV);
			context.Check(IsTemporalAAUVInBounds(edgePreviousHistoryUV) &&
				!IsTemporalAAUVInBounds(edgePreviousRasterUV) &&
				!AreTemporalAAReprojectionUVsValid(
					edgePreviousHistoryUV, edgePreviousRasterUV),
				"TAA rejects history when raw-depth raster UV is out of bounds even if accumulated-history UV is valid");

			context.Check(IsTemporalHistoryDepthCompatible(10.0f, 10.04f) &&
				IsTemporalHistoryDepthCompatible(100.0f, 101.9f) &&
				!IsTemporalHistoryDepthCompatible(10.0f, 10.21f) &&
				!IsTemporalHistoryDepthCompatible(100.0f, 102.1f) &&
				!IsTemporalHistoryDepthCompatible(0.0f, 0.0f) &&
				!IsTemporalHistoryDepthCompatible(
					std::numeric_limits<float>::quiet_NaN(), 1.0f),
				"Temporal reprojection uses finite positive previous-view Z with max absolute/relative rejection tolerance");

			std::array<TemporalAAGeometryDepthSample, 9> geometryEdgeNeighborhood{};
			geometryEdgeNeighborhood[4] = { 10.0f, false };
			geometryEdgeNeighborhood[5] = { 10.04f, true };
			std::array<TemporalAAGeometryDepthSample, 9> backgroundOnlyNeighborhood{};
			backgroundOnlyNeighborhood[4] = { 10.0f, false };
			std::array<TemporalAAGeometryDepthSample, 9> mismatchedGeometryNeighborhood{};
			mismatchedGeometryNeighborhood[4] = { 10.21f, true };
			context.Check(HasCompatibleTemporalGeometryDepthSample(
					10.0f, geometryEdgeNeighborhood) &&
				!HasCompatibleTemporalGeometryDepthSample(
					10.0f, backgroundOnlyNeighborhood) &&
				!HasCompatibleTemporalGeometryDepthSample(
					10.0f, mismatchedGeometryNeighborhood),
				"TAA geometry depth validation accepts a matching 3x3 geometry neighbor and rejects background or mismatched candidates");

			context.Check(IsTemporalSkyHistoryCompatible(0.0f, DepthConvention::Reversed) &&
				IsTemporalSkyHistoryCompatible(1.0f, DepthConvention::Standard) &&
				!IsTemporalSkyHistoryCompatible(0.5f, DepthConvention::Reversed) &&
				!IsTemporalSkyHistoryCompatible(0.5f, DepthConvention::Standard) &&
				!IsTemporalSkyHistoryCompatible(
					std::numeric_limits<float>::quiet_NaN(), DepthConvention::Reversed),
				"Sky reprojection accepts only finite previous background depth and rejects previous geometry");

			context.Check(TemporalHistoryInitialAge == 1.0f &&
				TemporalHistoryMaxAge == 255.0f &&
				IsTemporalHistoryAgeValid(TemporalHistoryInitialAge) &&
				IsTemporalHistoryAgeValid(TemporalHistoryMaxAge) &&
				!IsTemporalHistoryAgeValid(0.0f) &&
				!IsTemporalHistoryAgeValid(TemporalHistoryMaxAge + 1.0f) &&
				!IsTemporalHistoryAgeValid(std::numeric_limits<float>::quiet_NaN()) &&
				ResolveTemporalHistoryNextAge(false, 37.0f) == TemporalHistoryInitialAge &&
				ResolveTemporalHistoryNextAge(true, 0.0f) == TemporalHistoryInitialAge &&
				ResolveTemporalHistoryNextAge(true, TemporalHistoryInitialAge) == 2.0f &&
				ResolveTemporalHistoryNextAge(true, TemporalHistoryMaxAge) ==
					TemporalHistoryMaxAge,
				"Temporal history age starts or resets at one, advances only for accepted valid history, and saturates at its provisional R16Float-exact bound");

			const TemporalAAOutputAlphaContract accumulatedOutputAlphas =
				ResolveTemporalAAOutputAlphas(37.0f);
			const TemporalAAOutputAlphaContract resetOutputAlphas =
				ResolveTemporalAAOutputAlphas(
					std::numeric_limits<float>::quiet_NaN());
			context.Check(accumulatedOutputAlphas.m_ResolvedAlpha == 1.0f &&
				accumulatedOutputAlphas.m_HistoryAlpha == 37.0f &&
				resetOutputAlphas.m_ResolvedAlpha == 1.0f &&
				resetOutputAlphas.m_HistoryAlpha == TemporalHistoryInitialAge,
				"TAA output alpha contract keeps resolved color opaque while history carries a finite bounded age");

			const TemporalAASettings defaultTemporalAA{};
			static_assert(PackTemporalAAUnitRangePair(0.0f, 1.0f) == 0xffff0000u);
			static_assert(PackTemporalAAUnitRangePair(1.0f, 0.0f) == 0x0000ffffu);
			static_assert(PackTemporalAAUnitRangePair(0.5f, 0.25f) == 0x40008000u);
			static_assert(PackTemporalAAUnitRangePair(
				TemporalAADepthAbsoluteThreshold,
				TemporalAADepthRelativeThreshold) == 0x051f0ccdu);
			static_assert(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
				TemporalAAProvisionalDefaultMaxHistoryFeedback,
				TemporalAADefaultNeighborhoodClampExpansion) == 0x0000e666u);
			static_assert(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
				TemporalAAProvisionalMaxHistoryFeedbackCeiling, 0.0f) == 0x0000feffu);
			static_assert(PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
				1.0f, 0.0f) == 0x0000fffeu);
			constexpr std::array<float, 2> packingGolden =
				UnpackTemporalAAUnitRangePair(0x40008000u);
			constexpr std::array<float, 2> feedbackPackingGolden =
				UnpackTemporalAAUnitRangePair(
					PackTemporalAAMaxHistoryFeedbackAndClampExpansion(1.0f, 0.0f));
			constexpr std::array<float, 2> feedbackCeilingPackingGolden =
				UnpackTemporalAAUnitRangePair(
					PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
						TemporalAAProvisionalMaxHistoryFeedbackCeiling, 0.0f));
			context.Check(NearlyEqual(packingGolden[0], 32768.0f / 65535.0f) &&
				NearlyEqual(packingGolden[1], 16384.0f / 65535.0f) &&
				feedbackPackingGolden[0] < 1.0f &&
				(feedbackPackingGolden[0] * 65535.0f) == 65534.0f &&
				feedbackCeilingPackingGolden[0] < 1.0f &&
				std::ceil(feedbackCeilingPackingGolden[0] /
					(1.0f - feedbackCeilingPackingGolden[0])) <= TemporalHistoryMaxAge,
				"Temporal AA CPU packing matches the HLSL low/high UNORM16 ABI, feedback can never encode one, and the provisional cap remains reachable before age saturation");

			constexpr std::array<float, 6> historyAges{ 1.0f, 2.0f, 3.0f, 7.0f, 15.0f,
				31.0f };
			constexpr std::array<float, 6> expectedHistoryWeights{ 0.5f, 2.0f / 3.0f,
				0.75f, 0.875f, TemporalAAProvisionalDefaultMaxHistoryFeedback,
				TemporalAAProvisionalDefaultMaxHistoryFeedback };
			bool ageWeightGoldenMatches = true;
			for (size_t index = 0; index < historyAges.size(); ++index)
			{
				ageWeightGoldenMatches &= NearlyEqual(
					ResolveTemporalAAHistoryWeight(
						historyAges[index], 0.0f, 1.0f, 1.0f, defaultTemporalAA),
					expectedHistoryWeights[index]);
			}
			const float staticHistoryWeight = ResolveTemporalAAHistoryWeight(
				15.0f, 0.0f, 1.0f, 1.0f, defaultTemporalAA);
			const float movingHistoryWeight =
				ResolveTemporalAAHistoryWeight(15.0f, 10.0f, 1.0f, 1.0f, defaultTemporalAA);
			const float defaultChangedLuminanceWeight = ResolveTemporalAAHistoryWeight(
				15.0f, 0.0f, 1.0f, 0.9f, defaultTemporalAA);
			TemporalAASettings luminanceResearchSettings = defaultTemporalAA;
			luminanceResearchSettings.m_LuminanceWeightScale = 4.0f;
			const float researchChangedLuminanceWeight = ResolveTemporalAAHistoryWeight(
				15.0f, 0.0f, 1.0f, 0.9f, luminanceResearchSettings);
			TemporalAASettings packedCeilingSettings = defaultTemporalAA;
			packedCeilingSettings.m_MaxHistoryFeedback = feedbackCeilingPackingGolden[0];
			const float saturatedCeilingWeight = ResolveTemporalAAHistoryWeight(
				TemporalHistoryMaxAge, 0.0f, 1.0f, 1.0f, packedCeilingSettings);
			context.Check(ageWeightGoldenMatches &&
				TemporalAADefaultLuminanceWeightScale == 0.0f &&
				NearlyEqual(staticHistoryWeight,
					TemporalAAProvisionalDefaultMaxHistoryFeedback) &&
				NearlyEqual(saturatedCeilingWeight, feedbackCeilingPackingGolden[0]) &&
				movingHistoryWeight > 0.0f && movingHistoryWeight < staticHistoryWeight &&
				NearlyEqual(defaultChangedLuminanceWeight, staticHistoryWeight) &&
				researchChangedLuminanceWeight > 0.0f &&
				researchChangedLuminanceWeight < staticHistoryWeight &&
				ResolveTemporalAAHistoryWeight(std::numeric_limits<float>::quiet_NaN(),
					0.0f, 1.0f, 1.0f, defaultTemporalAA) == 0.0f &&
				ResolveTemporalAAHistoryWeight(1.0f,
					std::numeric_limits<float>::quiet_NaN(),
					1.0f, 1.0f, defaultTemporalAA) == 0.0f,
				"Temporal blend follows the age golden sequence, caps at the provisional maximum, retains velocity attenuation, and keeps luminance attenuation research-only by default");

			RecordingDevice motionCapabilityDevice;
			motionCapabilityDevice.m_TextureViewsSupported = true;
			const TemporalMotionFormatSupport motionSupport =
				QueryTemporalMotionFormatSupport(motionCapabilityDevice);
			const RHITextureDesc motionDesc = MakeTemporalMotionTextureDesc(1280, 720);
			context.Check(motionSupport.IsSupported() &&
				motionCapabilityDevice.m_TextureViewQueryCount == 2 &&
				motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Format ==
					TemporalMotionFormat &&
				Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage,
					RHITextureUsage::RenderTarget) &&
				Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage,
					RHITextureUsage::Sampled) && motionDesc.m_Usage == RHITextureUsage::None &&
				motionDesc.m_Extent.m_Width == 1280 &&
				motionDesc.m_Extent.m_Height == 720 && motionDesc.m_ClearValue &&
				motionDesc.m_ClearValue->m_Color[0] == 0.0f &&
				motionDesc.m_ClearValue->m_Color[1] == 0.0f,
				"Temporal motion capability requires the combined R16G16Float render-target and SRV contract");

			motionCapabilityDevice.m_TextureViewQueryCount = 0;
			const TemporalAAResolvedColorFormatSupport resolvedColorSupport =
				QueryTemporalAAResolvedColorFormatSupport(motionCapabilityDevice);
			context.Check(resolvedColorSupport.IsSupported() &&
				motionCapabilityDevice.m_TextureViewQueryCount == 3 &&
				motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Format ==
					TemporalAAResolvedColorFormat &&
				Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage,
					RHITextureUsage::RenderTarget) &&
				Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage,
					RHITextureUsage::Sampled) &&
				Test(motionCapabilityDevice.m_LastTextureViewQueryTextureDesc.m_Usage,
					RHITextureUsage::UnorderedAccess),
				"Temporal resolved color capability requires the combined HDR RTV, SRV, and typed-UAV contract");

			GraphicsPhysicalPipelineKey depthOnlyMotionCoverage{};
			depthOnlyMotionCoverage.m_BindingLayout = RHIBindingLayoutHandle{ 1, 1 };
			depthOnlyMotionCoverage.m_InputLayoutId = InputLayoutID::P3N3T2T2Tan4;
			depthOnlyMotionCoverage.m_VSId = ShaderID{ 3 };
			depthOnlyMotionCoverage.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
			depthOnlyMotionCoverage.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
			depthOnlyMotionCoverage.m_Formats.m_DepthStencilFormat = RHIFormat::D32Float;
			depthOnlyMotionCoverage.m_DepthPreset = DepthPreset::ReversedZWrite;
			depthOnlyMotionCoverage.m_BlendPreset = BlendPreset::ColorWriteDisable;
			GraphicsPhysicalPipelineKey velocityMotionCoverage = depthOnlyMotionCoverage;
			velocityMotionCoverage.m_PSId = ShaderID{ 4 };
			velocityMotionCoverage.m_Formats.m_RenderTargetCount = 1;
			velocityMotionCoverage.m_Formats.m_RenderTargetFormats[0] = TemporalMotionFormat;
			velocityMotionCoverage.m_BlendPreset = BlendPreset::Default;
			const uint64_t opaqueMotionVariant =
				RenderQueueBuilder::EncodeVariantBits(RenderBucket::Opaque, false);
			const uint64_t alphaMotionVariant =
				RenderQueueBuilder::EncodeVariantBits(RenderBucket::AlphaTest, false);
			context.Check(
				RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
					depthOnlyMotionCoverage, opaqueMotionVariant) ==
					RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
						velocityMotionCoverage, opaqueMotionVariant) &&
				RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
					depthOnlyMotionCoverage, alphaMotionVariant) ==
					RenderPassDepthPrepass::BuildDepthCoveragePipelineSignatureForVariant(
						velocityMotionCoverage, alphaMotionVariant),
				"Velocity MRT preserves opaque and alpha-test depth coverage signatures");

			struct TemporalMotionFixturePassData
			{
				RGTextureId m_Motion{};
			};
			RenderGraph motionGraph({
				.m_Device = &motionCapabilityDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
			});
			motionGraph.GetBlackboard().Create<RGTemporalGeometryResources>(
				TemporalGeometryResourcesName);
			motionGraph.AddPass<TemporalMotionFixturePassData>(
				"RenderingContract.TemporalMotionSetup",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					builder.SideEffect();
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					resources.m_MotionVectors = builder.CreateTexture(
						"Temporal.MotionVectors", MakeTemporalMotionTextureDesc(1280, 720));
					resources.m_MotionSrvDesc =
						MakeRHITexture2DViewDesc(TemporalMotionFormat);
					resources.m_Width = 1280;
					resources.m_Height = 720;
					data.m_Motion = resources.m_MotionVectors;
				});
			motionGraph.AddPass<TemporalMotionFixturePassData>(
				"View.ClearMotionVectors",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					builder.SideEffect();
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					builder.WriteInPlace(resources.m_MotionVectors, RGTextureAccess::RenderTarget);
					data.m_Motion = resources.m_MotionVectors;
				});
			motionGraph.AddPass<TemporalMotionFixturePassData>(
				"Geometry.DepthPrepass.Velocity",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					builder.ReadWriteInPlace(
						resources.m_MotionVectors, RGTextureAccess::RenderTarget);
					data.m_Motion = resources.m_MotionVectors;
				});
			motionGraph.AddPass<TemporalMotionFixturePassData>(
				"RenderingContract.TemporalMotionDebugTap",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					data.m_Motion = builder.Read(
						resources.m_MotionVectors, RGTextureAccess::Sample);
					builder.SideEffect();
				});
			const bool motionGraphCompiled = motionGraph.Compile();
			RGSnapshot motionSnapshot;
			if (motionGraphCompiled)
			{
				BuildRenderGraphSnapshot(motionGraph, motionSnapshot);
			}
			const auto motionResource = std::ranges::find(
				motionSnapshot.m_Resources, "Temporal.MotionVectors", &RGSnapshotResourceInfo::m_Name);
			context.Check(motionGraphCompiled && motionSnapshot.m_Passes.size() == 4 &&
				motionSnapshot.m_Passes[1].m_Name == "View.ClearMotionVectors" &&
				motionSnapshot.m_Passes[1].m_ExecutionOrder <
					motionSnapshot.m_Passes[2].m_ExecutionOrder &&
				motionSnapshot.m_Passes[2].m_ExecutionOrder <
					motionSnapshot.m_Passes[3].m_ExecutionOrder &&
				motionResource != motionSnapshot.m_Resources.end() &&
				motionResource->m_TextureFormat == TemporalMotionFormat &&
				motionResource->m_TextureExtent.m_Width == 1280 &&
				motionResource->m_TextureExtent.m_Height == 720 &&
				Test(static_cast<RHITextureUsage>(motionResource->m_UsageBits),
					RHITextureUsage::RenderTarget) &&
				Test(static_cast<RHITextureUsage>(motionResource->m_UsageBits),
					RHITextureUsage::Sampled),
				"Active temporal geometry clears motion before velocity coverage and exposes an explicit sampled debug tap");

			RenderGraph skyOnlyGraph({
				.m_Device = &motionCapabilityDevice,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{1}),
			});
			skyOnlyGraph.GetBlackboard().Create<RGTemporalGeometryResources>(
				TemporalGeometryResourcesName);
			skyOnlyGraph.AddPass<TemporalMotionFixturePassData>(
				"RenderingContract.SkyOnlyTemporalSetup",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					builder.SideEffect();
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					resources.m_MotionVectors = builder.CreateTexture(
						"Temporal.SkyOnly.MotionVectors", MakeTemporalMotionTextureDesc(320, 180));
					resources.m_MotionSrvDesc = MakeRHITexture2DViewDesc(TemporalMotionFormat);
					resources.m_Width = 320;
					resources.m_Height = 180;
					data.m_Motion = resources.m_MotionVectors;
				});
			skyOnlyGraph.AddPass<TemporalMotionFixturePassData>(
				"View.ClearMotionVectors",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					builder.WriteInPlace(resources.m_MotionVectors, RGTextureAccess::RenderTarget);
					data.m_Motion = resources.m_MotionVectors;
				});
			skyOnlyGraph.AddPass<TemporalMotionFixturePassData>(
				"PostProcess.TemporalAA",
				[](RenderGraph::RGBuilder& builder, TemporalMotionFixturePassData& data)
				{
					auto& resources = builder.GetBlackboard().Get<
						RGTemporalGeometryResources>(TemporalGeometryResourcesName);
					data.m_Motion = builder.Read(
						resources.m_MotionVectors, RGTextureAccess::Sample);
					builder.SideEffect();
				});
			const bool skyOnlyGraphCompiled = skyOnlyGraph.Compile();
			RGSnapshot skyOnlySnapshot;
			if (skyOnlyGraphCompiled)
			{
				BuildRenderGraphSnapshot(skyOnlyGraph, skyOnlySnapshot);
			}
			context.Check(skyOnlyGraphCompiled && skyOnlySnapshot.m_Passes.size() == 3 &&
				skyOnlySnapshot.m_Passes[1].m_Name == "View.ClearMotionVectors" &&
				!skyOnlySnapshot.m_Passes[1].m_Culled &&
				skyOnlySnapshot.m_Passes[1].m_ExecutionOrder <
					skyOnlySnapshot.m_Passes[2].m_ExecutionOrder,
				"Zero-draw sky-only temporal frames retain motion clear before reprojection resolve");

			context.Check(IsTemporalColorCompatible(
				TemporalColorAbi::LinearRec709SceneReferredV1,
				PostProcessColorState::SceneLinearRec709, 1.0f) &&
				!IsTemporalColorCompatible(TemporalColorAbi::LinearRec709SceneReferredV1,
					PostProcessColorState::DisplayLinearRec709, 1.0f) &&
				!IsTemporalColorCompatible(TemporalColorAbi::LinearRec709SceneReferredV1,
					PostProcessColorState::SceneLinearRec709, 0.5f),
				"Temporal color ABI accepts only linear scene Rec.709 at unit pre-exposure");

			Camera rigCamera(Camera::CreateInfo{});
			CameraController rigController(CameraController::CreateInfo{});
			CameraRig cameraRig;
			cameraRig.AttachMainCamera(rigCamera, rigController);
			const CameraRig::EffectiveDisplayView mainDisplayView =
				cameraRig.ResolveEffectiveDisplayView();
			const size_t debugCameraIndex = cameraRig.AddDebugCameraFromActive();
			const CameraRig::CameraSlot* debugCameraSlot =
				cameraRig.GetCameraSlot(debugCameraIndex);
			const bool selectedDebugView =
				debugCameraSlot && cameraRig.SetDisplayViewId(debugCameraSlot->m_RenderViewId);
			const CameraRig::EffectiveDisplayView debugDisplayView =
				cameraRig.ResolveEffectiveDisplayView();
			CameraRig::CameraSlot* mutableDebugCameraSlot =
				cameraRig.GetCameraSlot(debugCameraIndex);
			if (mutableDebugCameraSlot)
			{
				mutableDebugCameraSlot->m_EnableRenderView = false;
			}
			const CameraRig::EffectiveDisplayView fallbackDisplayView =
				cameraRig.ResolveEffectiveDisplayView();
			context.Check(mainDisplayView.IsValid() &&
				mainDisplayView.m_ViewId == RenderViewID::Main && selectedDebugView &&
				debugDisplayView.IsValid() &&
				IsDebugCameraRenderViewID(debugDisplayView.m_ViewId) &&
				fallbackDisplayView.IsValid() &&
				fallbackDisplayView.m_ViewId == RenderViewID::Main,
				"CameraRig resolves the effective display view and fallback exactly once");

			const uint64_t initialResetSerial = rigCamera.GetTemporalResetSerial();
			rigCamera.SetPosition(Vector3(1.0f, 2.0f, 3.0f));
			rigCamera.SetFov(55.0f);
			const bool continuousChangesPreserveSerial =
				rigCamera.GetTemporalResetSerial() == initialResetSerial;
			rigCamera.RequestTemporalReset();
			const uint64_t explicitResetSerial = rigCamera.GetTemporalResetSerial();
			rigCamera.LookAt(Vector3(2.0f, 3.0f, 4.0f), Vector3::Zero);
			context.Check(continuousChangesPreserveSerial &&
				explicitResetSerial == initialResetSerial + 1 &&
				rigCamera.GetTemporalResetSerial() == explicitResetSerial + 1,
				"Camera temporal reset serial changes only for explicit discontinuities and cuts");

			ViewRenderProfile profile{};
			const Camera camera(Camera::CreateInfo{});
			const ResolvedViewRenderSettings defaultSettings =
				ResolveViewRenderSettings(profile, camera);
			profile.m_TemporalAA.m_Enabled = true;
			const ResolvedViewRenderSettings enabledSettings =
				ResolveViewRenderSettings(profile, camera);
			profile.m_TemporalAA.m_DepthAbsoluteThreshold = -1.0f;
			profile.m_TemporalAA.m_DepthRelativeThreshold = 2.0f;
			profile.m_TemporalAA.m_MaxHistoryFeedback = 2.0f;
			profile.m_TemporalAA.m_VelocityWeightScale = -1.0f;
			profile.m_TemporalAA.m_LuminanceWeightScale = 32.0f;
			profile.m_TemporalAA.m_NeighborhoodClampExpansion =
				std::numeric_limits<float>::quiet_NaN();
			const ResolvedViewRenderSettings clampedSettings =
				ResolveViewRenderSettings(profile, camera);
			context.Check(!defaultSettings.m_TemporalAA.m_Enabled &&
				enabledSettings.m_TemporalAA.m_Enabled &&
				NearlyEqual(enabledSettings.m_TemporalAA.m_DepthAbsoluteThreshold,
					TemporalAADepthAbsoluteThreshold) &&
				NearlyEqual(enabledSettings.m_TemporalAA.m_DepthRelativeThreshold,
					TemporalAADepthRelativeThreshold) &&
				NearlyEqual(enabledSettings.m_TemporalAA.m_MaxHistoryFeedback,
					TemporalAAProvisionalDefaultMaxHistoryFeedback) &&
				NearlyEqual(enabledSettings.m_TemporalAA.m_VelocityWeightScale,
					TemporalAADefaultVelocityWeightScale) &&
				NearlyEqual(enabledSettings.m_TemporalAA.m_LuminanceWeightScale,
					TemporalAADefaultLuminanceWeightScale) &&
				NearlyEqual(enabledSettings.m_TemporalAA.m_NeighborhoodClampExpansion,
					TemporalAADefaultNeighborhoodClampExpansion) &&
				NearlyEqual(clampedSettings.m_TemporalAA.m_DepthAbsoluteThreshold, 0.0f) &&
				NearlyEqual(clampedSettings.m_TemporalAA.m_DepthRelativeThreshold,
					TemporalAAMaxDepthThreshold) &&
				NearlyEqual(clampedSettings.m_TemporalAA.m_MaxHistoryFeedback,
					TemporalAAProvisionalMaxHistoryFeedbackCeiling) &&
				clampedSettings.m_TemporalAA.m_MaxHistoryFeedback < 1.0f &&
				NearlyEqual(clampedSettings.m_TemporalAA.m_VelocityWeightScale, 0.0f) &&
				NearlyEqual(clampedSettings.m_TemporalAA.m_LuminanceWeightScale,
					TemporalAAMaxLuminanceWeightScale) &&
				NearlyEqual(clampedSettings.m_TemporalAA.m_NeighborhoodClampExpansion, 0.0f),
				"Temporal AA authoring defaults off, feedback resolves strictly below one, and quality controls resolve to finite bounded frame settings");

			const TemporalAACapabilityStatus fullCapabilities{
				.m_MotionRenderTarget = true,
				.m_MotionShaderResource = true,
				.m_ResolvedColorRenderTarget = true,
				.m_ResolvedColorShaderResource = true,
				.m_ResolvedColorTypedUavStore = true,
				.m_HistoryColorShaderResource = true,
				.m_HistoryColorTypedUavStore = true,
				.m_HistoryDepthShaderResource = true,
				.m_HistoryDepthTypedUavStore = true,
				.m_VelocityProgramsAvailable = true,
				.m_ResolveProgramAvailable = true,
				.m_BindingLayoutAvailable = true,
			};
			context.Check(fullCapabilities.IsCoreAvailable() &&
				!TemporalAACapabilityStatus{}.IsCoreAvailable(),
				"Temporal AA core capability requires the complete texture, program, and binding closure");

			TemporalFramePlanResolveInfo resolveInfo{
				.m_Settings = {.m_Enabled = true},
				.m_Capabilities = fullCapabilities,
				.m_DisplayViewId = RenderViewID::Main,
				.m_SceneExtensionParticipation =
					SceneExtensionTemporalParticipation::PostTAA,
				.m_ResetIdentity = 17,
				.m_SessionIdentity = 23,
				.m_DisplayViewEligible = true,
				.m_DepthVelocityPathAvailable = true,
			};
			const ResolvedTemporalFramePlan activePlan = ResolveTemporalFramePlan(resolveInfo);
			TemporalFramePlanResolveInfo disabledInfo = resolveInfo;
			disabledInfo.m_Settings.m_Enabled = false;
			const ResolvedTemporalFramePlan disabledPlan = ResolveTemporalFramePlan(disabledInfo);
			TemporalFramePlanResolveInfo missingCoreInfo = resolveInfo;
			missingCoreInfo.m_Capabilities = {};
			const ResolvedTemporalFramePlan missingCorePlan =
				ResolveTemporalFramePlan(missingCoreInfo);
			TemporalFramePlanResolveInfo ineligibleInfo = resolveInfo;
			ineligibleInfo.m_DisplayViewEligible = false;
			const ResolvedTemporalFramePlan ineligiblePlan =
				ResolveTemporalFramePlan(ineligibleInfo);
			TemporalFramePlanResolveInfo missingDepthVelocityInfo = resolveInfo;
			missingDepthVelocityInfo.m_DepthVelocityPathAvailable = false;
			const ResolvedTemporalFramePlan missingDepthVelocityPlan =
				ResolveTemporalFramePlan(missingDepthVelocityInfo);
			TemporalFramePlanResolveInfo unsupportedExtensionInfo = resolveInfo;
			unsupportedExtensionInfo.m_SceneExtensionParticipation =
				SceneExtensionTemporalParticipation::TemporalUnsupported;
			const ResolvedTemporalFramePlan unsupportedExtensionPlan =
				ResolveTemporalFramePlan(unsupportedExtensionInfo);
			TemporalFramePlanResolveInfo internalInfo = missingCoreInfo;
			internalInfo.m_InternalContractMode = true;
			const ResolvedTemporalFramePlan internalPlan = ResolveTemporalFramePlan(internalInfo);
			context.Check(activePlan.m_Active && activePlan.m_CoreAvailable &&
				activePlan.m_Status == TemporalAAFrameStatus::Active &&
				activePlan.m_DisableReason == TemporalAADisableReason::None &&
				activePlan.m_ResetIdentity == 17 && activePlan.m_SessionIdentity == 23 &&
				!disabledPlan.m_Active &&
				disabledPlan.m_Status == TemporalAAFrameStatus::Disabled &&
				disabledPlan.m_DisableReason == TemporalAADisableReason::NotRequested &&
				missingCorePlan.m_DisableReason ==
					TemporalAADisableReason::CoreCapabilityUnavailable &&
				ineligiblePlan.m_DisableReason == TemporalAADisableReason::DisplayViewIneligible &&
				missingDepthVelocityPlan.m_DisableReason ==
					TemporalAADisableReason::DepthVelocityPathUnavailable &&
				unsupportedExtensionPlan.m_DisableReason ==
					TemporalAADisableReason::SceneExtensionUnsupported &&
				internalPlan.m_Active && !internalPlan.m_CoreAvailable &&
				internalPlan.m_InternalContractMode,
				"Temporal frame plan resolves one atomic active state and preserves every disable cause");

			context.Check(IsTemporalAADisplayViewEligible(RenderViewID::Main, 1920, 1080) &&
				IsTemporalAADisplayViewEligible(RenderViewID::DebugCamera0, 1, 1) &&
				!IsTemporalAADisplayViewEligible(RenderViewID::DirectionalShadow, 1920, 1080) &&
				!IsTemporalAADisplayViewEligible(RenderViewID::Main, 0, 1080),
				"Temporal AA eligibility is restricted to non-empty perspective display views");

			RenderPipelineForwardPBR forwardPipeline;
			const ResolvedTemporalFramePlan forwardPlan =
				forwardPipeline.ResolveTemporalFramePlan(resolveInfo);
			RenderPipelineForwardPBR integratedExtensionPipeline({
				.m_SceneExtension = std::make_unique<IntegratedTemporalSceneExtension>(),
			});
			TemporalFramePlanResolveInfo integratedExtensionInfo = resolveInfo;
			integratedExtensionInfo.m_DepthVelocityPathAvailable = true;
			const ResolvedTemporalFramePlan integratedExtensionPlan =
				integratedExtensionPipeline.ResolveTemporalFramePlan(integratedExtensionInfo);
			context.Check(forwardPlan.m_Active && forwardPlan.m_DepthVelocityPathAvailable &&
				forwardPlan.m_SceneExtensionParticipation ==
					SceneExtensionTemporalParticipation::PostTAA &&
				forwardPlan.m_DisableReason == TemporalAADisableReason::None &&
				!integratedExtensionPlan.m_Active &&
				integratedExtensionPlan.m_SceneExtensionParticipation ==
					SceneExtensionTemporalParticipation::TemporalUnsupported,
				"ForwardPBR exposes its velocity path and rejects unsupported integrated extensions");

			Renderer renderer;
			context.Check(!renderer.GetTemporalAACapabilityStatus().IsCoreAvailable(),
				"Renderer publishes temporal core capability as unavailable before pipeline closure");

			const ResolvedTemporalFramePlan viewPlan = ResolveTemporalFramePlan({
				.m_Settings = enabledSettings.m_TemporalAA,
				.m_Capabilities = {},
				.m_DisplayViewId = RenderViewID::Main,
				.m_SceneExtensionParticipation =
					SceneExtensionTemporalParticipation::PostTAA,
				.m_DisplayViewEligible = true,
			});
			RenderViewBuilder viewBuilder;
			RenderView view = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = defaultSettings,
				.m_TemporalFramePlan = viewPlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			TemporalViewHistory unavailableViewHistory{};
			TemporalObjectHistory unavailableObjectHistory{};
			TemporalFrameTransaction unavailableTransaction;
			unavailableTransaction.Begin(unavailableViewHistory, unavailableObjectHistory,
				viewPlan, 1920, 1080);
			unavailableTransaction.PrepareDisplayView(view);
			unavailableTransaction.CommitCompleted();
			const float nearRawDepth = ProjectPosition(
				Vector3(0.0f, 0.0f, view.m_Near), view.m_RasterProj).m_RawDepth;
			const float farRawDepth = ProjectPosition(
				Vector3(0.0f, 0.0f, view.m_Far), view.m_RasterProj).m_RawDepth;
			context.Check(viewPlan.m_Requested && !viewPlan.m_CoreAvailable &&
				!viewPlan.m_Active && viewPlan.m_DisableReason ==
					TemporalAADisableReason::CoreCapabilityUnavailable &&
				unavailableTransaction.GetState() == TemporalFrameTransactionState::Committed &&
				NearlyEqual(unavailableTransaction.GetJitterPixels(), Vector2::Zero) &&
				!unavailableTransaction.ParticipatedInResolve() &&
				!unavailableViewHistory.m_Valid &&
				unavailableObjectHistory.GetDiagnostics().m_EntryCount == 0 &&
				view.m_UnjitteredProj.ToArray() == view.m_RasterProj.ToArray() &&
				view.m_UnjitteredViewProj.ToArray() == view.m_RasterViewProj.ToArray() &&
				view.m_JitterPixels.m_X == 0.0f && view.m_JitterPixels.m_Y == 0.0f &&
				view.m_JitterUV.m_X == 0.0f && view.m_JitterUV.m_Y == 0.0f &&
				!view.m_HasPreviousTemporalState &&
				NearlyEqual(screen_space::RawDepthToPositiveViewZ(nearRawDepth,
					view.m_DepthReconstructionParams, view.m_DepthConvention), view.m_Near) &&
				NearlyEqual(screen_space::RawDepthToPositiveViewZ(farRawDepth,
					view.m_DepthReconstructionParams, view.m_DepthConvention), view.m_Far,
					PositionTolerance * view.m_Far),
				"Core-unavailable temporal frames disable jitter, history participation, and resolve atomically");

			const Vector2 jitterPixels(0.5f, -0.25f);
			const Vector2 jitterUV = temporal::JitterPixelsToUV(jitterPixels, 100, 50);
			const Vector2 jitterNDC = temporal::JitterPixelsToNDC(jitterPixels, 100, 50);
			const Vector4 jitteredClip = temporal::ApplyJitterToClipPosition(
				Vector4(1.0f, 2.0f, 3.0f, 2.0f), jitterNDC);
			context.Check(NearlyEqual(jitterUV, Vector2(0.005f, -0.005f)) &&
				NearlyEqual(jitterNDC, Vector2(0.01f, 0.01f)) &&
				NearlyEqual(jitteredClip.m_X, 1.02f) &&
				NearlyEqual(jitteredClip.m_Y, 2.02f) &&
				NearlyEqual(temporal::ReprojectToPreviousUV(Vector2(0.5f, 0.5f),
					Vector2(0.1f, -0.2f)), Vector2(0.4f, 0.7f)),
				"CPU temporal math fixes pixel, NDC, clip, and motion reprojection signs");

			constexpr std::array<Vector2, temporal::JitterSampleCount> expectedJitterSamples = {
				Vector2(0.0f, -1.0f / 6.0f),
				Vector2(-1.0f / 4.0f, 1.0f / 6.0f),
				Vector2(1.0f / 4.0f, -7.0f / 18.0f),
				Vector2(-3.0f / 8.0f, -1.0f / 18.0f),
				Vector2(1.0f / 8.0f, 5.0f / 18.0f),
				Vector2(-1.0f / 8.0f, -5.0f / 18.0f),
				Vector2(3.0f / 8.0f, 1.0f / 18.0f),
				Vector2(-7.0f / 16.0f, 7.0f / 18.0f),
			};
			bool jitterGoldenTableMatches = true;
			for (uint32_t index = 0; index < temporal::JitterSampleCount; ++index)
			{
				jitterGoldenTableMatches = jitterGoldenTableMatches &&
					NearlyEqual(temporal::GetJitterSamplePixels(index),
						expectedJitterSamples[index]);
			}
			context.Check(jitterGoldenTableMatches,
				"Temporal jitter sequence matches the exact Halton(2,3) eight-sample table");

			TemporalViewHistory temporalViewHistory{};
			TemporalObjectHistory temporalObjectHistory{};
			TemporalFrameTransaction abortedTransaction;
			abortedTransaction.Begin(
				temporalViewHistory, temporalObjectHistory, activePlan, 1920, 1080);
			RenderView abortedView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = activePlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			abortedTransaction.PrepareDisplayView(abortedView);
			const Vector4 unjitteredClip = math::Transform(
				Vector4(1.0f, 2.0f, 3.0f, 1.0f), abortedView.m_UnjitteredProj);
			const Vector4 rasterClip = math::Transform(
				Vector4(1.0f, 2.0f, 3.0f, 1.0f), abortedView.m_RasterProj);
			const Vector4 expectedRasterClip = temporal::ApplyJitterToClipPosition(
				unjitteredClip,
				temporal::JitterPixelsToNDC(abortedTransaction.GetJitterPixels(), 1920, 1080));
			abortedTransaction.Abort();

			TemporalFrameTransaction committedTransaction;
			committedTransaction.Begin(
				temporalViewHistory, temporalObjectHistory, activePlan, 1920, 1080);
			RenderView committedView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = activePlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			committedTransaction.PrepareDisplayView(committedView);
			committedTransaction.MarkResolveParticipated();
			committedTransaction.CommitCompleted();

			TemporalFrameTransaction noResolveTransaction;
			noResolveTransaction.Begin(
				temporalViewHistory, temporalObjectHistory, activePlan, 1920, 1080);
			RenderView noResolveView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = activePlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			noResolveTransaction.PrepareDisplayView(noResolveView);
			noResolveTransaction.CommitCompleted();

			ResolvedTemporalFramePlan changedSessionPlan = activePlan;
			++changedSessionPlan.m_SessionIdentity;
			TemporalFrameTransaction changedSessionTransaction;
			changedSessionTransaction.Begin(
				temporalViewHistory, temporalObjectHistory, changedSessionPlan, 1920, 1080);
			RenderView changedSessionView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = changedSessionPlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			changedSessionTransaction.PrepareDisplayView(changedSessionView);
			changedSessionTransaction.Abort();
			const RenderView temporalShadowView =
				viewBuilder.Build<RenderViewID::DirectionalShadow>({
					.m_MainView = noResolveView,
				});

			TemporalFrameTransaction fatalTransaction;
			fatalTransaction.Begin(
				temporalViewHistory, temporalObjectHistory, activePlan, 1920, 1080);
			RenderView fatalView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = activePlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			fatalTransaction.PrepareDisplayView(fatalView);
			fatalTransaction.MarkResolveParticipated();
			fatalTransaction.InvalidateAfterFatal();
			context.Check(abortedTransaction.GetJitterIndex() == 0 &&
				committedTransaction.GetJitterIndex() == 0 &&
				noResolveTransaction.GetJitterIndex() == 1 &&
				noResolveView.m_HasPreviousTemporalState &&
				noResolveView.m_PreviousJitterUV.m_X == committedView.m_JitterUV.m_X &&
				noResolveView.m_PreviousJitterUV.m_Y == committedView.m_JitterUV.m_Y &&
				noResolveTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
				changedSessionTransaction.GetJitterIndex() == 0 &&
				!changedSessionView.m_HasPreviousTemporalState &&
				temporalShadowView.m_RasterProj.ToArray() ==
					temporalShadowView.m_UnjitteredProj.ToArray() &&
				fatalTransaction.GetState() == TemporalFrameTransactionState::Invalidated &&
				!temporalViewHistory.m_Valid && temporalViewHistory.m_NextJitterIndex == 0 &&
				NearlyEqual(rasterClip.m_X, expectedRasterClip.m_X) &&
				NearlyEqual(rasterClip.m_Y, expectedRasterClip.m_Y) &&
				NearlyEqual(rasterClip.m_Z, expectedRasterClip.m_Z) &&
				NearlyEqual(rasterClip.m_W, expectedRasterClip.m_W),
				"Temporal frame transaction advances only on committed resolve and invalidates on fatal");

			TemporalViewHistory submittedViewHistory{};
			TemporalObjectHistory submittedObjectHistory{};
			const auto buildSubmittedHistoryView = [&]() noexcept
			{
				return viewBuilder.Build<RenderViewID::Main>({
					.m_Camera = camera,
					.m_RenderSettings = enabledSettings,
					.m_TemporalFramePlan = activePlan,
					.m_Width = 1920,
					.m_Height = 1080,
				});
			};
			const RenderObjectHistoryKey objectHistoryKey{
				.m_EntityIdentity = 7,
				.m_ModelId = ModelID{ 11 },
				.m_ModelContentGeneration = 3,
				.m_ModelMeshIndex = 2,
				.m_MeshId = MeshID{ 13 },
				.m_MeshContentGeneration = 5,
				.m_SessionIdentity = activePlan.m_SessionIdentity,
			};
			const Matrix initialObjectModel =
				math::CreateTranslation(Vector3(1.0f, 2.0f, 3.0f));
			const Matrix movedObjectModel =
				math::CreateTranslation(Vector3(4.0f, 5.0f, 6.0f));

			TemporalFrameTransaction initialObjectTransaction;
			initialObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				activePlan, 1920, 1080);
			RenderView initialObjectView = buildSubmittedHistoryView();
			initialObjectTransaction.PrepareDisplayView(initialObjectView);
			const Matrix initialPreviousModel =
				initialObjectTransaction.ResolvePreviousObjectModel(
					objectHistoryKey, initialObjectModel);
			const bool initialObjectStaged =
				initialObjectTransaction.StageSubmittedObject(
					objectHistoryKey, initialObjectModel);
			initialObjectTransaction.MarkResolveParticipated();
			initialObjectTransaction.CommitCompleted();
			const TemporalCommittedObjectState* initialCommittedObject =
				submittedObjectHistory.Find(objectHistoryKey);
			const bool initialObjectCommitted = initialCommittedObject &&
				initialCommittedObject->m_Model.ToArray() == initialObjectModel.ToArray() &&
				initialCommittedObject->m_LastSeenCommittedFrame == 1;

			TemporalFrameTransaction abortedObjectTransaction;
			abortedObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				activePlan, 1920, 1080);
			RenderView abortedObjectView = buildSubmittedHistoryView();
			abortedObjectTransaction.PrepareDisplayView(abortedObjectView);
			const Matrix abortedPreviousModel =
				abortedObjectTransaction.ResolvePreviousObjectModel(
					objectHistoryKey, movedObjectModel);
			const bool abortedObjectStaged =
				abortedObjectTransaction.StageSubmittedObject(
					objectHistoryKey, movedObjectModel);
			abortedObjectTransaction.CommitCompleted();
			const TemporalCommittedObjectState* committedAfterAbort =
				submittedObjectHistory.Find(objectHistoryKey);
			const bool objectAbortPreserved = committedAfterAbort &&
				committedAfterAbort->m_Model.ToArray() == initialObjectModel.ToArray() &&
				committedAfterAbort->m_LastSeenCommittedFrame == 1;

			TemporalFrameTransaction movedObjectTransaction;
			movedObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				activePlan, 1920, 1080);
			RenderView movedObjectView = buildSubmittedHistoryView();
			movedObjectTransaction.PrepareDisplayView(movedObjectView);
			const Matrix movedPreviousModel =
				movedObjectTransaction.ResolvePreviousObjectModel(
					objectHistoryKey, movedObjectModel);
			const bool movedObjectStaged =
				movedObjectTransaction.StageSubmittedObject(
					objectHistoryKey, movedObjectModel);
			movedObjectTransaction.MarkResolveParticipated();
			movedObjectTransaction.CommitCompleted();

			TemporalFrameTransaction abortedDisappearanceTransaction;
			abortedDisappearanceTransaction.Begin(submittedViewHistory,
				submittedObjectHistory, activePlan, 1920, 1080);
			RenderView abortedDisappearanceView = buildSubmittedHistoryView();
			abortedDisappearanceTransaction.PrepareDisplayView(abortedDisappearanceView);
			abortedDisappearanceTransaction.Abort();
			const TemporalCommittedObjectState* committedAfterAbortedDisappearance =
				submittedObjectHistory.Find(objectHistoryKey);
			const bool abortedDisappearancePreserved =
				committedAfterAbortedDisappearance &&
				committedAfterAbortedDisappearance->m_Model.ToArray() ==
					movedObjectModel.ToArray() &&
				committedAfterAbortedDisappearance->m_LastSeenCommittedFrame == 2;

			RenderObjectHistoryKey replacementKey = objectHistoryKey;
			replacementKey.m_ModelId = ModelID{ 12 };
			replacementKey.m_ModelContentGeneration = 1;
			RenderObjectHistoryKey republishedGeometryKey = objectHistoryKey;
			++republishedGeometryKey.m_MeshContentGeneration;
			const Matrix replacementModel =
				math::CreateTranslation(Vector3(8.0f, 9.0f, 10.0f));
			RenderObjectHistoryKey reusedEntityKey = objectHistoryKey;
			++reusedEntityKey.m_EntityIdentity;
			TemporalFrameTransaction replacementTransaction;
			replacementTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				activePlan, 1920, 1080);
			RenderView replacementView = buildSubmittedHistoryView();
			replacementTransaction.PrepareDisplayView(replacementView);
			const Matrix replacementPreviousModel =
				replacementTransaction.ResolvePreviousObjectModel(
					replacementKey, replacementModel);
			const Matrix reusedEntityPreviousModel =
				replacementTransaction.ResolvePreviousObjectModel(
					reusedEntityKey, replacementModel);
			const Matrix republishedGeometryPreviousModel =
				replacementTransaction.ResolvePreviousObjectModel(
					republishedGeometryKey, replacementModel);
			const bool replacementStaged = replacementTransaction.StageSubmittedObject(
				replacementKey, replacementModel);
			replacementTransaction.MarkResolveParticipated();
			replacementTransaction.CommitCompleted();
			const TemporalObjectHistoryDiagnostics replacementDiagnostics =
				submittedObjectHistory.GetDiagnostics();
			const TemporalCommittedObjectState* committedReplacement =
				submittedObjectHistory.Find(replacementKey);
			const bool replacementRetiredOldIdentity =
				!submittedObjectHistory.Find(objectHistoryKey) && committedReplacement &&
				committedReplacement->m_Model.ToArray() == replacementModel.ToArray();

			ResolvedTemporalFramePlan replacementSessionPlan = activePlan;
			++replacementSessionPlan.m_SessionIdentity;
			RenderObjectHistoryKey replacementSessionKey = replacementKey;
			replacementSessionKey.m_SessionIdentity = replacementSessionPlan.m_SessionIdentity;
			TemporalFrameTransaction replacementSessionTransaction;
			replacementSessionTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				replacementSessionPlan, 1920, 1080);
			RenderView replacementSessionView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = replacementSessionPlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			replacementSessionTransaction.PrepareDisplayView(replacementSessionView);
			const Matrix replacementSessionPrevious =
				replacementSessionTransaction.ResolvePreviousObjectModel(
					replacementSessionKey, initialObjectModel);
			replacementSessionTransaction.Abort();

			ResolvedTemporalFramePlan resetObjectPlan = activePlan;
			++resetObjectPlan.m_ResetIdentity;
			TemporalFrameTransaction resetObjectTransaction;
			resetObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				resetObjectPlan, 1920, 1080);
			RenderView resetObjectView = viewBuilder.Build<RenderViewID::Main>({
				.m_Camera = camera,
				.m_RenderSettings = enabledSettings,
				.m_TemporalFramePlan = resetObjectPlan,
				.m_Width = 1920,
				.m_Height = 1080,
			});
			resetObjectTransaction.PrepareDisplayView(resetObjectView);
			const Matrix resetPreviousModel =
				resetObjectTransaction.ResolvePreviousObjectModel(
					replacementKey, initialObjectModel);
			resetObjectTransaction.Abort();

			TemporalFrameTransaction fatalObjectTransaction;
			fatalObjectTransaction.Begin(submittedViewHistory, submittedObjectHistory,
				activePlan, 1920, 1080);
			RenderView fatalObjectView = buildSubmittedHistoryView();
			fatalObjectTransaction.PrepareDisplayView(fatalObjectView);
			fatalObjectTransaction.InvalidateAfterFatal();
			const TemporalObjectHistoryDiagnostics fatalObjectDiagnostics =
				submittedObjectHistory.GetDiagnostics();
			context.Check(objectHistoryKey.IsValid() && initialObjectStaged &&
				abortedObjectStaged && movedObjectStaged && replacementStaged &&
				abortedObjectTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
				initialPreviousModel.ToArray() == initialObjectModel.ToArray() &&
				initialObjectCommitted &&
				abortedPreviousModel.ToArray() == initialObjectModel.ToArray() &&
				objectAbortPreserved &&
				movedPreviousModel.ToArray() == initialObjectModel.ToArray() &&
				abortedDisappearancePreserved &&
				replacementPreviousModel.ToArray() == replacementModel.ToArray() &&
				reusedEntityPreviousModel.ToArray() == replacementModel.ToArray() &&
				republishedGeometryPreviousModel.ToArray() == replacementModel.ToArray() &&
				replacementRetiredOldIdentity &&
				replacementDiagnostics.m_EntryCount == 1 &&
				replacementDiagnostics.m_Capacity == MaxObjectCapacity &&
				replacementDiagnostics.m_LastCommittedFrame == 3 &&
				replacementSessionPrevious.ToArray() == initialObjectModel.ToArray() &&
				resetPreviousModel.ToArray() == initialObjectModel.ToArray() &&
				fatalObjectDiagnostics.m_EntryCount == 0 &&
				fatalObjectDiagnostics.m_LastCommittedFrame == 0,
				"Submitted object history commits atomically, rejects stale identities, and "
				"retires only on committed disappearance");

			RenderFrameBuilder::BuildResult frameResult{};
			frameResult.m_TemporalFramePlan = activePlan;
			const RenderFrameContext frameContext = frameResult.MakeRenderFrameContext();
			context.Check(frameContext.GetTemporalFramePlan() == activePlan,
				"RenderFrameContext preserves the single pre-frame temporal plan without re-resolution");
		}
	}

	void RunRenderingContractSelfTests(SelfTestContext& context) noexcept
	{
		RunSuiteSmokeTests(context);
		RunOpaqueSceneExtensionContractTests(context);
		RunOverlayExtensionContractTests(context);
		RunRuntimePathConfigurationContractTests(context);
		RunNapaVoxelRenderGraphContractTests(context);
		RunRHIFrameAndGraphicsScopeContractTests(context);
		RunDX12GraphicsContractLoweringTests(context);
		RunScreenSpaceAndDepthContractTests(context);
		RunTextureFormatCapabilityTests(context);
		RunPersistentTexturePoolContractTests(context);
		RunTemporalHistoryTransactionContractTests(context);
		RunResourceStateAndPortabilityContractTests(context);
		RunGTAORenderGraphDataflowTests(context);
		RunRenderGraphAccessAndBarrierContractTests(context);
		RunTemporalCompatibilityAndHistoryContractTests(context);
	}
}
