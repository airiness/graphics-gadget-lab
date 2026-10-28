#include "Application/Lab/Sessions/RayQueryLabSession.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr uint32_t ImageSize = 512;
		constexpr uint32_t RayCount = 8;
		constexpr LayoutBindingKey SceneKey{ 1 };
		constexpr LayoutBindingKey ResultsKey{ 2 };
		constexpr LayoutBindingKey ParametersKey{ 3 };
		constexpr LayoutBindingKey HeapKey{ 4 };
		struct HitResult
		{
			uint32_t m_Hit = 0;
			uint32_t m_Instance = 0;
			uint32_t m_Primitive = 0;
			uint32_t m_Reserved = 0;
			std::array<float, 2> m_Barycentrics{};
			float m_Distance = 0;
			float m_Padding = 0;
		};
		static_assert(sizeof(HitResult) == 32 && offsetof(HitResult, m_Barycentrics) == 16 &&
			offsetof(HitResult, m_Distance) == 24);
		struct Parameters
		{
			uint32_t m_Texture = 0;
			uint32_t m_Width = ImageSize;
			uint32_t m_Height = ImageSize;
			uint32_t m_Reserved = 0;
		};
		static_assert(sizeof(Parameters) == 16 && offsetof(Parameters, m_Reserved) == 12);
		constexpr uint64_t ResultBytes = RayCount * sizeof(HitResult);
		const RenderPassInfo QueryInfo{
			.m_TypeName = "Lab.RayQuery.Trace", .m_DisplayName = "Ray-query triangle validation",
			.m_CategoryName = "Ray Query", .m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Compute };
		const RenderPassInfo PreviewInfo{
			.m_TypeName = "Lab.RayQuery.Preview", .m_DisplayName = "Ray-query barycentrics",
			.m_CategoryName = "Ray Query", .m_Category = RenderPassCategory::Debug,
			.m_Type = RenderPassType::Graphics };
		struct ReadbackReceipt
		{
			RHIFencePoint m_Fence{};
		};
		struct ReadbackSlot
		{
			RHIBufferOwner m_Buffer;
			ReadbackReceipt m_Receipt;
		};
	}

	struct RayQueryLabState
	{
		RHIDevice* m_Device = nullptr;
		std::array<RHIBufferOwner, 2> m_Geometry;
		std::array<RHIBufferOwner, 2> m_Uploads;
		std::array<RHIBufferOwner, 2> m_Scratch;
		std::array<RHIBufferDesc, 2> m_GeometryDescs{};
		std::array<RHIBufferDesc, 2> m_ScratchDescs{};
		std::array<RHIASBuildSizes, 2> m_Sizes{};
		RHIAccelerationStructureHandle m_Bottom{};
		RHIAccelerationStructureHandle m_Top{};
		std::array<RHIASInstanceDesc, 4> m_Instances{};
		RHIBufferOwner m_Results;
		std::vector<ReadbackSlot> m_Readbacks;
		uint32_t m_VerifiedFrames = 0;
		bool m_Failed = false;
		bool m_Unavailable = false;

		~RayQueryLabState()
		{
			if (m_Device)
			{
				if (m_Top.IsValid())
				{
					m_Device->DestroyAccelerationStructure(m_Top);
				}
				if (m_Bottom.IsValid())
				{
					m_Device->DestroyAccelerationStructure(m_Bottom);
				}
			}
		}

		RHIASBuildInputs BottomInputs() const noexcept
		{
			return { .m_Input = RHIASIndexedTriangles{
				.m_Vertices = { m_Geometry[0].Get(), 0, m_GeometryDescs[0].m_SizeInBytes },
				.m_VertexCount = 3, .m_VertexStrideInBytes = 12,
				.m_Indices = { m_Geometry[1].Get(), 0, m_GeometryDescs[1].m_SizeInBytes },
				.m_IndexCount = 3 } };
		}

		bool Initialize(RHIContext& context) noexcept
		{
			m_Device = &context.GetDevice();
			constexpr std::array<float, 9> vertices{ -0.4f, -0.4f, 0, 0.4f, -0.4f, 0, 0, 0.4f, 0 };
			constexpr std::array<uint32_t, 3> indices{ 0, 1, 2 };
			const std::array<std::span<const std::byte>, 2> bytes{
				std::as_bytes(std::span(vertices)), std::as_bytes(std::span(indices)) };
			for (size_t index = 0; index < 2; ++index)
			{
				m_GeometryDescs[index] = { .m_SizeInBytes = bytes[index].size(),
					.m_Usage = RHIBufferUsage::CopyDest | RHIBufferUsage::AccelerationStructureInput,
					.m_DebugName = index == 0 ? "RayQuery.Vertices" : "RayQuery.Indices" };
				m_Geometry[index] = RHIBufferOwner(m_Device, m_Device->CreateBuffer(m_GeometryDescs[index]));
				m_Uploads[index] = RHIBufferOwner(m_Device, m_Device->CreateBuffer({
					.m_SizeInBytes = bytes[index].size(), .m_Usage = RHIBufferUsage::CopySource,
					.m_MemoryUsage = RHIMemoryUsage::CpuToGpu, .m_DebugName = "RayQuery.GeometryUpload" }));
				void* mapped = m_Uploads[index] ? m_Device->MapBuffer(m_Uploads[index].Get(), {}) : nullptr;
				if (!m_Geometry[index] || !mapped)
				{
					return false;
				}
				std::memcpy(mapped, bytes[index].data(), bytes[index].size());
				if (!m_Device->FlushMappedBufferWrites(m_Uploads[index].Get(), { 0, bytes[index].size() }))
				{
					return false;
				}
			}
			const auto bottomSizes = m_Device->QueryAccelerationStructureBuildSizes(BottomInputs());
			if (!bottomSizes)
			{
				return false;
			}
			m_Sizes[0] = *bottomSizes;
			m_Bottom = m_Device->CreateAccelerationStructure({
				.m_SizeInBytes = bottomSizes->m_StorageSizeInBytes, .m_DebugName = "RayQuery.BLAS" });
			if (!m_Bottom.IsValid())
			{
				return false;
			}
			for (uint32_t index = 0; index < m_Instances.size(); ++index)
			{
				m_Instances[index].m_BottomLevel = m_Bottom;
				m_Instances[index].m_InstanceIndex = 17 + index * 12;
				m_Instances[index].m_Mask = static_cast<uint8_t>(1u << index);
				m_Instances[index].m_Flags = RHIASInstanceFlags::TriangleCullDisable;
			}
			m_Instances[0].m_Transform[3] = -0.75f;
			m_Instances[1].m_Transform[3] = 0.75f;
			m_Instances[1].m_Transform[11] = 1.0f;
			m_Instances[2].m_Transform[0] = -1.0f;
			m_Instances[2].m_Transform[7] = 0.9f;
			m_Instances[2].m_Transform[11] = 0.5f;
			m_Instances[3].m_Transform[0] = 0.7f;
			m_Instances[3].m_Transform[5] = 1.3f;
			m_Instances[3].m_Transform[3] = -0.75f;
			m_Instances[3].m_Transform[7] = 0.9f;
			m_Instances[3].m_Transform[11] = 0.25f;
			const auto topSizes = m_Device->QueryAccelerationStructureBuildSizes({ .m_Input = std::span<const RHIASInstanceDesc>(m_Instances) });
			if (!topSizes)
			{
				return false;
			}
			m_Sizes[1] = *topSizes;
			m_Top = m_Device->CreateAccelerationStructure({
				.m_Type = RHIAccelerationStructureType::TopLevel,
				.m_SizeInBytes = topSizes->m_StorageSizeInBytes, .m_DebugName = "RayQuery.TLAS" });
			for (size_t index = 0; index < 2; ++index)
			{
				m_ScratchDescs[index] = { .m_SizeInBytes = m_Sizes[index].m_ScratchSizeInBytes,
					.m_Usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::AccelerationStructureScratch,
					.m_DebugName = index == 0 ? "RayQuery.BLASScratch" : "RayQuery.TLASScratch" };
				m_Scratch[index] = RHIBufferOwner(m_Device, m_Device->CreateBuffer(m_ScratchDescs[index]));
				if (!m_Scratch[index])
				{
					return false;
				}
			}
			m_Results = RHIBufferOwner(m_Device, m_Device->CreateBuffer({
				.m_SizeInBytes = ResultBytes, .m_StrideInBytes = sizeof(HitResult),
				.m_Usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource,
				.m_DebugName = "RayQuery.Hits" }));
			m_Readbacks.resize(context.GetFrameSlotCount());
			for (auto& slot : m_Readbacks)
			{
				slot.m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer({
					.m_SizeInBytes = ResultBytes, .m_Usage = RHIBufferUsage::CopyDest,
					.m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "RayQuery.Readback" }));
				if (!slot.m_Buffer)
				{
					return false;
				}
			}
			return m_Top.IsValid() && m_Results;
		}

		void CheckReadback(uint32_t slotIndex) noexcept
		{
			auto& slot = m_Readbacks[slotIndex];
			if (!slot.m_Receipt.m_Fence.IsValid())
			{
				return;
			}
			if (m_Device->QueryFencePoint(slot.m_Receipt.m_Fence) != RHIFenceQueryResult::Completed)
			{
				m_Failed = true;
				return;
			}
			const auto* mapped = m_Device->MapBuffer(slot.m_Buffer.Get(), { 0, ResultBytes });
			std::array<HitResult, RayCount> results{};
			if (!mapped)
			{
				m_Failed = true;
				return;
			}
			std::memcpy(results.data(), mapped, ResultBytes);
			m_Device->UnmapBuffer(slot.m_Buffer.Get(), {});
			constexpr std::array<float, 4> distances{ 2, 3, 2.5f, 2.25f };
			for (uint32_t index = 0; index < RayCount; ++index)
			{
				const auto& hit = results[index];
				const bool expectedHit = index < 4;
				const bool matches = hit.m_Hit == (expectedHit ? 1u : 0u) &&
					(!expectedHit || (hit.m_Instance == 17 + index * 12 && hit.m_Primitive == 0 &&
						std::abs(hit.m_Barycentrics[0] - 0.25f) <= 0.00005f &&
						std::abs(hit.m_Barycentrics[1] - 0.5f) <= 0.00005f &&
						std::abs(hit.m_Distance - distances[index]) <= 0.00005f));
				m_Failed = m_Failed || !matches;
				if (m_VerifiedFrames == 0 || !matches)
				{
					GGLAB_LOG_GRAPHICS_INFO_ALWAYS("Ray-query hit: ray={}, hit={}, instance={}, primitive={}, bary=({},{}), t={}, match={}",
						index, hit.m_Hit, hit.m_Instance, hit.m_Primitive, hit.m_Barycentrics[0],
						hit.m_Barycentrics[1], hit.m_Distance, matches);
				}
			}
			slot.m_Receipt.m_Fence = {};
			if (!m_Failed)
			{
				if (m_VerifiedFrames++ == 0)
				{
					GGLAB_LOG_GRAPHICS_INFO_ALWAYS("Ray-query GPU verification passed: 8 rays, 4 instances, 4 masked/directional/spatial misses.");
				}
			}
		}
	};

	namespace
	{
		struct Resources
		{
			std::array<RGBufferId, 2> m_Geometry;
			std::array<RGBufferId, 2> m_Uploads;
			std::array<RGBufferId, 2> m_Scratch;
			RGAccelerationStructureId m_Bottom;
			RGAccelerationStructureId m_Top;
			RGBufferId m_Results;
			RGBufferId m_Readback;
			RGTextureId m_Image;
			RGTextureId m_BackBuffer;
		};
		constexpr const char* ResourcesName = "RayQuery.Resources";
		struct EmptyData
		{
		};
		struct QueryData
		{
			RGAccelerationStructureId m_Top;
			RGBufferId m_Results;
			RGTextureViewId m_Image;
		};
		struct PreviewData
		{
			RGTextureViewId m_Image;
			RGTextureViewId m_Target;
		};

		class RayQueryLabPipeline final : public RenderPipelineBase
		{
		public:
			explicit RayQueryLabPipeline(std::shared_ptr<RayQueryLabState> state) noexcept : m_State(std::move(state)) {}
			std::string_view GetName() const noexcept override { return "Ray-query validation"; }
			RenderFrameValidationResult ValidateRenderFrame(const RenderFrameContext& frame,
				const RenderServices& services) noexcept override
			{
				auto& context = *services.m_Presentation->GetRHIContext();
				if (!context.GetDevice().GetRayQueryCapabilities().m_Enabled)
				{
					m_State->m_Unavailable = true;
					return RenderFrameValidationResult::Skip("Ray-query device capability is unavailable");
				}
				if (!m_Initialized)
				{
					m_Initialized = true;
					m_State->m_Failed = !m_State->Initialize(context) || !InitializeLayouts(context, services);
				}
				if (!m_State->m_Failed)
				{
					m_State->CheckReadback(frame.m_FrameSlotIndex);
				}
				return m_State->m_Failed ? RenderFrameValidationResult::ContractFailure(
					"Ray-query setup or GPU hit verification failed") : RenderFrameValidationResult::Ready();
			}

			void BuildRenderGraph(RenderGraph& rg, const RenderFrameContext& frame,
				const RenderServices& services) noexcept override
			{
				rg.GetBlackboard().GetOrCreate<NonFiniteWatchDefaultRegistry>().Declare({});
				auto* swap = services.m_Presentation->GetSwapChain();
				const auto view = frame.GetDisplayViewId();
				const auto width = swap->GetBufferWidth();
				const auto height = swap->GetBufferHeight();
				const auto slotIndex = frame.m_FrameSlotIndex;
				rg.AddPass<EmptyData>("Lab.RayQuery.Setup",
					[&, view, width, height, slotIndex, swap](RenderGraph::RGBuilder& builder, EmptyData&)
					{
						auto& resources = builder.GetBlackboard().Create<Resources>(ResourcesName);
						for (size_t index = 0; index < 2; ++index)
						{
							resources.m_Geometry[index] = builder.ImportBuffer(index == 0 ? "RayQuery.Vertices" : "RayQuery.Indices",
								m_State->m_Geometry[index].Get(), m_State->m_GeometryDescs[index],
								RGBufferAccess::AccelerationStructureInput, RGContentValidity::Defined);
							RHIBufferDesc uploadDesc{ .m_SizeInBytes = m_State->m_GeometryDescs[index].m_SizeInBytes,
								.m_Usage = RHIBufferUsage::CopySource, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu };
							resources.m_Uploads[index] = builder.ImportBuffer(index == 0 ? "RayQuery.VertexUpload" : "RayQuery.IndexUpload",
								m_State->m_Uploads[index].Get(), uploadDesc, RGBufferAccess::CopySource, RGContentValidity::Defined);
							resources.m_Scratch[index] = builder.ImportBuffer(index == 0 ? "RayQuery.BLASScratch" : "RayQuery.TLASScratch",
								m_State->m_Scratch[index].Get(), m_State->m_ScratchDescs[index],
								RGBufferAccess::AccelerationStructureScratch, RGContentValidity::Undefined);
						}
						resources.m_Bottom = builder.Import<RGAccelerationStructureResource>("RayQuery.BLAS", m_State->m_Bottom,
							{ .m_SizeInBytes = m_State->m_Sizes[0].m_StorageSizeInBytes }, RGAccelerationStructureAccess::QueryRead,
							RGContentValidity::Undefined);
						resources.m_Top = builder.Import<RGAccelerationStructureResource>("RayQuery.TLAS", m_State->m_Top,
							{ .m_Type = RHIAccelerationStructureType::TopLevel, .m_SizeInBytes = m_State->m_Sizes[1].m_StorageSizeInBytes },
							RGAccelerationStructureAccess::QueryRead, RGContentValidity::Undefined);
						resources.m_Results = builder.ImportBuffer("RayQuery.Hits", m_State->m_Results.Get(),
							{ .m_SizeInBytes = ResultBytes, .m_StrideInBytes = sizeof(HitResult),
								.m_Usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySource },
							RGBufferAccess::CopySource, RGContentValidity::Undefined);
						resources.m_Readback = builder.ImportBuffer("RayQuery.Readback", m_State->m_Readbacks[slotIndex].m_Buffer.Get(),
							{ .m_SizeInBytes = ResultBytes, .m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu },
							RGBufferAccess::CopyDest, RGContentValidity::Undefined);
						resources.m_Image = builder.CreateTexture("RayQuery.Barycentrics", {
							.m_Format = RHIFormat::R16G16B16A16Float, .m_Extent = { ImageSize, ImageSize, 1 } });
						resources.m_BackBuffer = builder.ImportTexture("RayQuery.BackBuffer", swap->GetBackBufferHandle(frame.m_BackBufferIndex),
							{ .m_Format = swap->GetFormat(), .m_Extent = { width, height, 1 } },
							swap->GetBackBufferInitialState(frame.m_BackBufferIndex), RGContentValidity::Undefined);
						auto& targets = builder.GetBlackboard().GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(view);
						targets.m_BackBuffer = resources.m_BackBuffer;
						targets.m_DisplayWidth = width;
						targets.m_DisplayHeight = height;
					});
				rg.AddPass<EmptyData>("Lab.RayQuery.Upload", RGPassEncoderType::Copy,
					[](RenderGraph::RGBuilder& builder, EmptyData&)
					{
						auto& resources = builder.GetBlackboard().Get<Resources>(ResourcesName);
						for (size_t index = 0; index < 2; ++index)
						{
							GGLAB_UNUSED(builder.Read(resources.m_Uploads[index], RGBufferAccess::CopySource));
							builder.WriteInPlace(resources.m_Geometry[index], RGBufferAccess::CopyDest);
						}
					},
					[this](RGExecuteContext& context, EmptyData&)
					{
						for (size_t index = 0; index < 2; ++index)
						{
							context.GetCopyCommandContext()->CopyBuffer(m_State->m_Geometry[index].Get(), 0,
								m_State->m_Uploads[index].Get(), 0, m_State->m_GeometryDescs[index].m_SizeInBytes);
						}
					});
				for (uint32_t index = 0; index < 2; ++index)
				{
					rg.AddPass<EmptyData>(index == 0 ? "Lab.RayQuery.BuildBLAS" : "Lab.RayQuery.BuildTLAS", RGPassEncoderType::Compute,
						[index](RenderGraph::RGBuilder& builder, EmptyData&)
						{
							auto& resources = builder.GetBlackboard().Get<Resources>(ResourcesName);
							if (index == 0)
							{
								GGLAB_UNUSED(builder.Read(resources.m_Geometry[0], RGBufferAccess::AccelerationStructureInput));
								GGLAB_UNUSED(builder.Read(resources.m_Geometry[1], RGBufferAccess::AccelerationStructureInput));
								// Exercise All for build writes alongside explicit AS read/query stages.
								builder.WriteInPlace(resources.m_Bottom, RGAccelerationStructureAccess::BuildWrite, RHIStage::All);
							}
							else
							{
								GGLAB_UNUSED(builder.Read(resources.m_Bottom, RGAccelerationStructureAccess::BuildRead));
								builder.WriteInPlace(resources.m_Top, RGAccelerationStructureAccess::BuildWrite);
							}
							// Exercise scratch semantics with universal and mixed stage scopes.
							const auto scratchStages = index == 0 ?
								RHIStage::All :
								RHIStage::AccelerationStructureBuild | RHIStage::ComputeShader;
							builder.WriteInPlace(resources.m_Scratch[index],
								RGBufferAccess::AccelerationStructureScratch, scratchStages);
						},
						[this, index](RGExecuteContext& context, EmptyData&)
						{
							const RHIASBuildInputs inputs = index == 0 ? m_State->BottomInputs() :
								RHIASBuildInputs{ .m_Input = std::span<const RHIASInstanceDesc>(m_State->m_Instances) };
							GGLAB_UNUSED(context.GetDirectComputeCommandContext()->BuildAccelerationStructure(
								index == 0 ? m_State->m_Bottom : m_State->m_Top, inputs,
								{ m_State->m_Scratch[index].Get(), 0, m_State->m_ScratchDescs[index].m_SizeInBytes }));
						});
				}
				rg.AddPass<QueryData>(QueryInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
					[](RenderGraph::RGBuilder& builder, QueryData& data)
					{
						auto& resources = builder.GetBlackboard().Get<Resources>(ResourcesName);
						GGLAB_UNUSED(builder.Read(resources.m_Bottom, RGAccelerationStructureAccess::QueryRead));
						data.m_Top = builder.Read(resources.m_Top, RGAccelerationStructureAccess::QueryRead);
						builder.WriteInPlace(resources.m_Results, RGBufferAccess::StorageWrite, RHIStage::ComputeShader);
						data.m_Results = resources.m_Results;
						builder.WriteInPlace(resources.m_Image, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
						data.m_Image = builder.CreateView<RHITextureViewType::UnorderedAccess>(resources.m_Image);
					},
					[this, &services](RGExecuteContext& context, QueryData& data)
					{
						auto* commands = context.GetDirectComputeCommandContext();
						const auto pipeline = m_QueryLayout.Resolve(*services.m_PipelineResolver, m_QuerySlot, m_QueryRecipe, QueryInfo);
						commands->SetPipeline(pipeline.GetHandle(m_QueryLayout, true));
						if (!commands->SetAccelerationStructure(m_SceneParameter, context.GetAccelerationStructureHandle(data.m_Top)))
						{
							return;
						}
						commands->SetReadWriteBuffer(m_ResultsParameter, context.GetBufferHandle(data.m_Results));
						commands->SetInlineConstants(m_QueryParameters, Parameters{ context.GetViewDescriptor(data.m_Image).m_Index });
						commands->Dispatch(ImageSize / 8, ImageSize / 8, 1);
					});
				rg.AddPass<EmptyData>("Lab.RayQuery.Readback", RGPassEncoderType::Copy,
					[](RenderGraph::RGBuilder& builder, EmptyData&)
					{
						builder.SideEffect();
						auto& resources = builder.GetBlackboard().Get<Resources>(ResourcesName);
						GGLAB_UNUSED(builder.Read(resources.m_Results, RGBufferAccess::CopySource));
						builder.WriteInPlace(resources.m_Readback, RGBufferAccess::CopyDest);
					},
					[this, slotIndex](RGExecuteContext& context, EmptyData&)
					{
						context.GetCopyCommandContext()->CopyBuffer(m_State->m_Readbacks[slotIndex].m_Buffer.Get(), 0,
							m_State->m_Results.Get(), 0, ResultBytes);
						// CPU receipt only. The frame slot gates reuse even if settlement discards this receipt.
						auto* receipt = &m_State->m_Readbacks[slotIndex].m_Receipt;
						context.DeferCommit([receipt](const RGCommitContext& committed) noexcept
							{
								receipt->m_Fence = committed.m_Fence;
							});
					});
				rg.AddPass<PreviewData>(PreviewInfo.m_TypeName.c_str(),
					[view](RenderGraph::RGBuilder& builder, PreviewData& data)
					{
						auto& resources = builder.GetBlackboard().Get<Resources>(ResourcesName);
						data.m_Image = builder.CreateView<RHITextureViewType::ShaderResource>(
							builder.Read(resources.m_Image, RGTextureAccess::Sample, RHIStage::PixelShader));
						auto& target = builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(view);
						builder.WriteInPlace(target.m_BackBuffer, RGTextureAccess::RenderTarget);
						data.m_Target = builder.CreateView<RHITextureViewType::RenderTarget>(target.m_BackBuffer);
					},
					[this, &services, width, height](RGExecuteContext& context, PreviewData& data)
					{
						auto* commands = context.GetGraphicsCommandContext();
						const RHIRenderingAttachment attachment{ .m_View = context.GetViewHandle(data.m_Target),
							.m_LoadOp = RHIContentLoadOp::DontCare };
						commands->BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
						const auto pipeline = m_PreviewLayout.Resolve(*services.m_PipelineResolver, m_PreviewSlot, m_PreviewRecipe, PreviewInfo);
						commands->SetPipeline(pipeline.GetHandle(m_PreviewLayout, false));
						commands->SetInlineConstants(m_PreviewParameters, Parameters{ context.GetViewDescriptor(data.m_Image).m_Index });
						commands->SetViewport({ 0, 0, static_cast<float>(width), static_cast<float>(height) });
						commands->SetScissorRect({ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) });
						commands->DrawFullscreenTriangle();
						commands->EndRendering();
					});
				AddFrameCaptureTapPass(rg, frame, services, FrameCaptureSource::Scene);
				if (services.m_OverlayExtension)
				{
					services.m_OverlayExtension->AddOverlayPasses(rg, frame, services);
				}
				AddFrameCaptureTapPass(rg, frame, services, FrameCaptureSource::Composited);
				rg.AddPass<EmptyData>("Lab.RayQuery.Present", [view](RenderGraph::RGBuilder& builder, EmptyData&)
					{
						builder.SideEffect();
						builder.Export(builder.GetBlackboard().Get<RGViewTargetsTable>(ViewTargetsTableName)
							.GetViewTargets(view).m_BackBuffer, RGTextureAccess::Present, RHIStage::Present);
					});
			}

		private:
			bool InitializeLayouts(RHIContext& context, const RenderServices& services) noexcept
			{
				const LayoutBindingRequirement parameters{ .m_Key = ParametersKey,
					.m_Slot = { .m_Type = RHIBindingType::InlineConstants, .m_Visibility = RHIShaderStage::All,
						.m_Count = 1, .m_SizeInBytes = sizeof(Parameters) }, .m_ParameterOrder = 2 };
				const LayoutBindingRequirement heap{ .m_Key = HeapKey,
					.m_Slot = { .m_Type = RHIBindingType::BindlessResourceTable, .m_Visibility = RHIShaderStage::All,
						.m_Space = 1, .m_Count = 0 }, .m_ParameterOrder = 3 };
				const std::array queryBindings{
					LayoutBindingRequirement{ .m_Key = SceneKey, .m_Slot = { .m_Type = RHIBindingType::AccelerationStructure,
						.m_Visibility = RHIShaderStage::Compute }, .m_ParameterOrder = 0 },
					LayoutBindingRequirement{ .m_Key = ResultsKey, .m_Slot = { .m_Type = RHIBindingType::ReadWriteStorageBuffer,
						.m_Visibility = RHIShaderStage::Compute }, .m_ParameterOrder = 1 }, parameters, heap };
				const std::array previewBindings{ parameters, heap };
				const std::array queryFragments{ LayoutFragment{ "RayQuery", queryBindings } };
				const std::array previewFragments{ LayoutFragment{ "RayQuery.Preview", previewBindings } };
				const auto query = ComposeLayoutFragments(queryFragments);
				const auto preview = ComposeLayoutFragments(previewFragments);
				if (!query.IsSuccess() || !preview.IsSuccess())
				{
					return false;
				}
				m_QueryLayout = ResolvedRenderLayout::Create(context.GetPipelineSystem(), *query.m_Layout, "RayQuery.Query");
				m_PreviewLayout = ResolvedRenderLayout::Create(context.GetPipelineSystem(), *preview.m_Layout, "RayQuery.Preview");
				// Exercise logical AS/SRV identity even when DX12 deduplicates the native signature.
				auto bufferBindings = queryBindings;
				bufferBindings[0].m_Slot.m_Type = RHIBindingType::ReadOnlyStorageBuffer;
				const std::array bufferFragments{ LayoutFragment{ "RayQuery.BufferAlias", bufferBindings } };
				const auto bufferContract = ComposeLayoutFragments(bufferFragments);
				if (!bufferContract.IsSuccess())
				{
					return false;
				}
				const auto bufferLayout = ResolvedRenderLayout::Create(context.GetPipelineSystem(), *bufferContract.m_Layout, "RayQuery.BufferAlias");
				RHIBindingLayoutDesc retained{};
				if (!bufferLayout.IsValid() || bufferLayout.GetNativeLayout() == m_QueryLayout.GetNativeLayout() ||
					!context.GetPipelineSystem().GetBindingLayoutDesc(m_QueryLayout.GetNativeLayout(), retained) ||
					retained.m_Slots[0].m_Type != RHIBindingType::AccelerationStructure)
				{
					return false;
				}
				m_SceneParameter = *query.m_Layout->FindParameterIndex(SceneKey);
				m_ResultsParameter = *query.m_Layout->FindParameterIndex(ResultsKey);
				m_QueryParameters = *query.m_Layout->FindParameterIndex(ParametersKey);
				m_PreviewParameters = *preview.m_Layout->FindParameterIndex(ParametersKey);
				m_QueryRecipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::RayQueryCompute);
				m_QueryRecipe.m_BindingLayout = m_QueryLayout.GetNativeLayout();
				// The clear shader uses u0 and leaves t0 unused, so both typed layouts
				// are legal even on backends whose native descriptor types differ.
				const auto identityShader = services.m_ShaderPrograms->LoadProgram(shader_programs::NonFiniteScanClearCompute);
				for (const bool bufferFirst : { false, true })
				{
					const auto asLayout = ResolvedRenderLayout::Create(
						context.GetPipelineSystem(), *query.m_Layout, "RayQuery.Identity.AS");
					const auto srvLayout = ResolvedRenderLayout::Create(
						context.GetPipelineSystem(), *bufferContract.m_Layout, "RayQuery.Identity.Buffer");
					const auto& firstLayout = bufferFirst ? srvLayout : asLayout;
					const auto& secondLayout = bufferFirst ? asLayout : srvLayout;
					ComputePipelineSlot firstSlot;
					ComputePipelineSlot secondSlot;
					ComputePipelineSlot repeatSlot;
					ComputePipelineRecipe identityRecipe;
					identityRecipe.m_CSId = identityShader;
					identityRecipe.m_BindingLayout = firstLayout.GetNativeLayout();
					const auto first = firstLayout.Resolve(*services.m_PipelineResolver,
						firstSlot, identityRecipe, QueryInfo).GetHandle(firstLayout, true);
					const auto repeated = firstLayout.Resolve(*services.m_PipelineResolver,
						repeatSlot, identityRecipe, QueryInfo).GetHandle(firstLayout, true);
					identityRecipe.m_BindingLayout = secondLayout.GetNativeLayout();
					const auto second = secondLayout.Resolve(*services.m_PipelineResolver,
						secondSlot, identityRecipe, QueryInfo).GetHandle(secondLayout, true);
					if (!first.IsValid() || !second.IsValid() || first == second || repeated != first)
					{
						return false;
					}
				}
				m_PreviewRecipe.m_VSId = services.m_ShaderPrograms->LoadProgram(shader_programs::RayQueryVertex);
				m_PreviewRecipe.m_PSId = services.m_ShaderPrograms->LoadProgram(shader_programs::RayQueryPixel);
				m_PreviewRecipe.m_BindingLayout = m_PreviewLayout.GetNativeLayout();
				m_PreviewRecipe.m_InputLayoutId = InputLayoutID::None;
				m_PreviewRecipe.m_TopologyType = RHIPrimitiveTopologyType::Triangle;
				m_PreviewRecipe.m_PrimitiveTopology = RHIPrimitiveTopology::TriangleList;
				m_PreviewRecipe.m_Formats.m_RenderTargetFormats[0] = context.GetSwapChain().GetFormat();
				m_PreviewRecipe.m_Formats.m_RenderTargetCount = 1;
				m_PreviewRecipe.m_Formats.m_SampleCount = 1;
				m_PreviewRecipe.m_RasterizerPreset = RasterizerPreset::Default;
				m_PreviewRecipe.m_BlendPreset = BlendPreset::Default;
				m_PreviewRecipe.m_DepthPreset = DepthPreset::DepthDisabled;
				return m_QueryLayout.IsValid() && m_PreviewLayout.IsValid();
			}
			std::shared_ptr<RayQueryLabState> m_State;
			ResolvedRenderLayout m_QueryLayout;
			ResolvedRenderLayout m_PreviewLayout;
			ComputePipelineRecipe m_QueryRecipe;
			GraphicsPhysicalPipelineKey m_PreviewRecipe;
			ComputePipelineSlot m_QuerySlot;
			GraphicsPipelineSlot m_PreviewSlot;
			uint32_t m_SceneParameter = 0;
			uint32_t m_ResultsParameter = 0;
			uint32_t m_QueryParameters = 0;
			uint32_t m_PreviewParameters = 0;
			bool m_Initialized = false;
		};
	}

	RayQueryLabSession::RayQueryLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, nullptr), m_State(std::make_shared<RayQueryLabState>())
	{
		SetRenderPipeline(std::make_unique<RayQueryLabPipeline>(m_State));
	}
	void RayQueryLabSession::Update(float deltaTime) noexcept { GGLAB_UNUSED(deltaTime); }
	void RayQueryLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Ray-query triangle and instance validation";
		diagnostics.m_Metrics.push_back({ "Verified frames", std::to_string(m_State->m_VerifiedFrames) });
		diagnostics.m_Checks.push_back({ "GPU hits and misses", m_State->m_Failed || m_State->m_Unavailable ?
			LabDiagnosticCheckStatus::Failed : m_State->m_VerifiedFrames ? LabDiagnosticCheckStatus::Passed :
			LabDiagnosticCheckStatus::Pending, m_State->m_Unavailable ? "Ray queries are unavailable on this device." :
			"Checks four transformed instances, IDs, primitive IDs, barycentrics, distances and four misses." });
	}
	LabId RayQueryLabSession::GetId() noexcept { return LabId("gglab.lab.ray_query"); }
	LabDescriptor RayQueryLabSession::GetDescriptor() noexcept
	{
		return { .m_Id = GetId(), .m_DisplayName = "Ray Query", .m_Category = "Rendering",
			.m_Description = "Validates native BLAS/TLAS builds and inline ray queries through RenderGraph.",
			.m_Kind = LabKind::Pipeline };
	}
	std::unique_ptr<LabSessionBase> RayQueryLabSession::Create(const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<RayQueryLabSession>(createInfo);
	}
}
