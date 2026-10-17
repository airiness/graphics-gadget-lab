#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace gglab
{
	void FrameCaptureService::ConfigureNonFiniteScans(RenderShaderProgramAccess& shaders,
		RenderPipelineResolver& resolver, RHIPipelineSystem& pipelines) noexcept
	{
		m_ScanShaders = &shaders;
		m_ScanResolver = &resolver;
		m_ScanPipelineSystem = &pipelines;
	}

	RHIPipelineHandle FrameCaptureService::ResolveScanPipeline(uint32_t variant) noexcept
	{
		if (!m_ScanShaders || !m_ScanResolver || !m_ScanPipelineSystem)
		{
			return {};
		}
		if (!m_ScanPipelineSystem->IsAlive(m_ScanBindingLayout))
		{
			RHIBindingLayoutDesc desc;
			desc.m_Slots[0] = { .m_Type = RHIBindingType::PushConstants, .m_Visibility = RHIShaderStage::Compute,
				.m_Binding = 2, .m_SizeInBytes = sizeof(NonFiniteScanParameters) };
			desc.m_Slots[1] = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
			desc.m_Slots[2] = { .m_Type = RHIBindingType::ReadOnlyStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 1 };
			desc.m_Slots[3] = { .m_Type = RHIBindingType::ReadWriteStorageBuffer, .m_Visibility = RHIShaderStage::Compute, .m_Binding = 0 };
			desc.m_Slots[4] = { .m_Type = RHIBindingType::BindlessResourceTable, .m_Visibility = RHIShaderStage::Compute, .m_Count = 0 };
			desc.m_SlotCount = 5;
			desc.m_DebugName = "NonFiniteScan.BindingLayout";
			m_ScanBindingLayout = m_ScanPipelineSystem->CreateBindingLayout(desc);
			if (!m_ScanBindingLayout.IsValid())
			{
				return {};
			}
		}
		const std::array refs{ shader_programs::NonFiniteScanClearCompute,
			shader_programs::NonFiniteScanTextureCompute, shader_programs::NonFiniteScanBufferCompute };
		auto& recipe = m_ScanRecipes[variant];
		if (!recipe.m_CSId.IsValid())
		{
			recipe.m_CSId = m_ScanShaders->LoadProgram(refs[variant]);
		}
		if (!recipe.m_CSId.IsValid())
		{
			return {};
		}
		recipe.m_BindingLayout = m_ScanBindingLayout;
		return m_ScanResolver->Resolve(m_ScanSlots[variant], recipe, {
			.m_TypeName = "NonFiniteScan", .m_DisplayName = "Non-finite boundary scan",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute });
	}

	uint64_t FrameCaptureService::RequestNonFiniteScan(std::vector<NonFiniteScanTarget> targets,
		uint64_t configurationVersion, uint64_t frameSerial, bool includeGraph, uint64_t groupId, bool deferFrameOnBusy) noexcept
	{
		if (GetRequestAvailability() == FrameCaptureAvailability::Busy)
		{
			return 0;
		}
		const uint64_t id = m_NextRequestId++;
		if (m_IsShutdown || targets.empty() || targets.size() > MaxScanTargetsPerGroup)
		{
			m_Results.push_back({ .m_RequestId = id, .m_Source = FrameCaptureSource::NonFiniteScan,
				.m_ResourceFailure = targets.size() > MaxScanTargetsPerGroup ? RGObservationFailure::TooLarge : RGObservationFailure::UnsupportedResource,
				.m_Failure = m_IsShutdown ? "The render host has been finalized." : "The scan group is empty or exceeds the scan target limit." });
			return id;
		}
		m_Queued.push_back({ .m_Id = id, .m_Source = FrameCaptureSource::NonFiniteScan, .m_FrameSerial = frameSerial,
			.m_GroupId = groupId, .m_ScanTargets = std::move(targets), .m_ConfigurationVersion = configurationVersion,
			.m_DeferFrameOnBusy = deferFrameOnBusy, .m_IncludeGraph = includeGraph });
		return id;
	}

	RGObservationFailure FrameCaptureService::PrepareScanTarget(RenderGraph& graph,
		const NonFiniteScanTarget& request, ScanReadback& readback) noexcept
	{
		auto& boundary = readback.m_Boundary;
		boundary.m_Target = graph.ResolveObservation(request.m_Address, RGObservationKind::NonFiniteScan);
		const auto& target = boundary.m_Target;
		if (target.m_Failure != RGObservationFailure::None)
		{
			return target.m_Failure;
		}
		if (!m_Device->GetShaderWaveCapabilities().IsValid())
		{
			return RGObservationFailure::UnsupportedResource;
		}
		auto& parameters = boundary.m_Parameters;
		if (target.m_Type == RGResourceType::RGBuffer)
		{
			const auto& layout = request.m_BufferLayout;
			if (layout.m_Failure != NonFiniteScanLayoutFailure::None || layout.m_Stride == 0 ||
				(target.m_BufferDesc.m_StrideInBytes != 0 && target.m_BufferDesc.m_StrideInBytes != layout.m_Stride))
			{
				return RGObservationFailure::UnsupportedResource;
			}
			// Revalidate the owned interpretation against this frame's actual buffer.
			std::vector<GpuBufferFieldLayout> fields;
			for (const auto& component : layout.m_Components)
			{
				if (component.m_Size != 2 && component.m_Size != 4)
				{
					return RGObservationFailure::UnsupportedResource;
				}
				fields.push_back({ "component", component.m_Offset,
					component.m_Size == 2 ? GpuBufferScalar::Float16 : GpuBufferScalar::Float32 });
			}
			boundary.m_BufferLayout = BuildNonFiniteBufferScanLayout({ layout.m_Stride, fields },
				target.m_BufferDesc.m_SizeInBytes, layout.m_ValidElementCount);
			if (boundary.m_BufferLayout.m_Failure != NonFiniteScanLayoutFailure::None)
			{
				return boundary.m_BufferLayout.m_Failure == NonFiniteScanLayoutFailure::TooLarge ?
					RGObservationFailure::TooLarge :
					RGObservationFailure::UnsupportedResource;
			}
			parameters.m_BufferStride = layout.m_Stride;
			parameters.m_ComponentCount = static_cast<uint32_t>(boundary.m_BufferLayout.m_Components.size());
			parameters.m_ScalarCount = boundary.m_BufferLayout.m_ScalarCount;
			readback.m_Scan.m_ComponentBytes = boundary.m_BufferLayout.m_Components.size() * sizeof(NonFiniteBufferComponent);
		}
		else
		{
			if (target.m_RawTypeless || target.m_Subresources.m_Aspects == RHITextureAspect::Stencil)
			{
				return RGObservationFailure::UnsupportedFormat;
			}
			switch (target.m_ViewFormat)
			{
			case RHIFormat::R16Float:
			case RHIFormat::R32Float:
			case RHIFormat::D32Float:
				parameters.m_Channels = 1;
				break;
			case RHIFormat::R16G16Float:
			case RHIFormat::R32G32Float:
				parameters.m_Channels = 2;
				break;
			case RHIFormat::R32G32B32Float:
				parameters.m_Channels = 3;
				break;
			case RHIFormat::R16G16B16A16Float:
			case RHIFormat::R32G32B32A32Float:
				parameters.m_Channels = 4;
				break;
			default:
				return RGObservationFailure::UnsupportedFormat;
			}
			parameters.m_Width = std::max(1u, target.m_TextureDesc.m_Extent.m_Width >> target.m_Subresources.m_BaseMip);
			parameters.m_Height = std::max(1u, target.m_TextureDesc.m_Extent.m_Height >> target.m_Subresources.m_BaseMip);
			const auto count = GetNonFiniteTextureScalarCount(parameters.m_Width, parameters.m_Height,
				target.m_Subresources.m_ArraySliceCount, parameters.m_Channels);
			if (!count)
			{
				return RGObservationFailure::TooLarge;
			}
			parameters.m_ScalarCount = *count;
		}
		readback.m_Scan.m_Target = target;
		readback.m_Scan.m_RecordBytes = sizeof(NonFiniteScanRecord);
		return RGObservationFailure::None;
	}

	bool FrameCaptureService::AllocateScanReadback(ScanReadback& readback) noexcept
	{
		const auto clear = ResolveScanPipeline(0);
		const bool buffer = readback.m_Boundary.m_Target.m_Type == RGResourceType::RGBuffer;
		const auto scan = ResolveScanPipeline(buffer ? 2 : 1);
		if (!clear.IsValid() || !scan.IsValid())
		{
			return false;
		}
		readback.m_Buffer = RHIBufferOwner(m_Device, m_Device->CreateBuffer({ .m_SizeInBytes = sizeof(NonFiniteScanRecord),
			.m_Usage = RHIBufferUsage::CopyDest, .m_MemoryUsage = RHIMemoryUsage::GpuToCpu, .m_DebugName = "NonFiniteScan.Readback" }));
		if (!readback.m_Buffer)
		{
			return false;
		}
		if (buffer)
		{
			readback.m_Components = RHIBufferOwner(m_Device, m_Device->CreateBuffer({ .m_SizeInBytes = readback.m_Scan.m_ComponentBytes,
				.m_StrideInBytes = sizeof(NonFiniteBufferComponent), .m_Usage = RHIBufferUsage::Structured,
				.m_MemoryUsage = RHIMemoryUsage::CpuToGpu, .m_DebugName = "NonFiniteScan.Components" }));
			if (!readback.m_Components)
			{
				return false;
			}
			void* mapped = m_Device->MapBuffer(readback.m_Components.Get(), {});
			if (!mapped)
			{
				return false;
			}
			std::memcpy(mapped, readback.m_Boundary.m_BufferLayout.m_Components.data(), static_cast<size_t>(readback.m_Scan.m_ComponentBytes));
			m_Device->UnmapBuffer(readback.m_Components.Get(), { 0, readback.m_Scan.m_ComponentBytes });
		}
		readback.m_Scan.m_Readback = readback.m_Buffer.Get();
		readback.m_Scan.m_Components = readback.m_Components.Get();
		readback.m_Scan.m_Clear = [clear](RGExecuteContext& context, RGBufferId record)
			{
				auto* commands = context.GetDirectComputeCommandContext();
				commands->SetPipeline(clear);
				commands->SetReadWriteBuffer(3, context.GetBufferHandle(record));
				commands->Dispatch(1, 1, 1);
			};
		const auto target = readback.m_Boundary.m_Target;
		const auto components = readback.m_Components.Get();
		readback.m_Scan.m_Scan =
			[scan, target, components, parameters = readback.m_Boundary.m_Parameters]
			(RGExecuteContext& context, RGBufferId record, RGTextureViewId view) mutable
			{
				if (parameters.m_ScalarCount == 0)
				{
					return; // The clear record is complete evidence for an empty valid range.
				}
				auto* commands = context.GetDirectComputeCommandContext();
				commands->SetPipeline(scan);
				commands->SetReadWriteBuffer(3, context.GetBufferHandle(record));
				if (target.m_Type == RGResourceType::RGBuffer)
				{
					commands->SetReadOnlyBuffer(1, context.GetBufferHandle(RGBufferId{ *target.m_Resource }));
					commands->SetReadOnlyBuffer(2, components);
				}
				else
				{
					const auto descriptor = context.GetViewDescriptor(view);
					if (!descriptor.IsValid())
					{
						return; // Zero checked components becomes Incomplete, never Passed.
					}
					parameters.m_TextureIndex = descriptor.m_Index;
				}
				const auto dispatch = GetNonFiniteScanDispatch(parameters.m_ScalarCount);
				parameters.m_DispatchGroupsX = dispatch.m_X;
				commands->SetPushConstants(0, parameters);
				commands->Dispatch(dispatch.m_X, dispatch.m_Y, 1);
			};
		return true;
	}

	void FrameCaptureService::PublishScanTap(Tap& tap) noexcept
	{
		if (tap.m_Graph && !tap.m_Graph->m_Execution.m_Recorded)
		{
			FailTap(tap, tap.m_FrameSerial, "The scan's paired graph did not execute.");
			return;
		}
		auto payload = std::make_shared<NonFiniteScanPayload>();
		payload->m_ConfigurationVersion = tap.m_Requests[0].m_ConfigurationVersion;
		for (auto& readback : tap.m_ScanReadbacks)
		{
			const void* mapped = m_Device->MapBuffer(readback.m_Buffer.Get(), { 0, sizeof(NonFiniteScanRecord) });
			if (!mapped)
			{
				FailTap(tap, tap.m_FrameSerial, "Non-finite scan readback mapping failed.");
				return;
			}
			std::memcpy(&readback.m_Boundary.m_Record, mapped, sizeof(NonFiniteScanRecord));
			m_Device->UnmapBuffer(readback.m_Buffer.Get(), {});
			readback.m_Boundary.m_Result = EvaluateNonFiniteBoundary(readback.m_Boundary.m_Record,
				readback.m_Boundary.m_Parameters.m_ScalarCount);
			payload->m_Boundaries.push_back(std::move(readback.m_Boundary));
		}
		uint64_t retainedBytes = 0;
		for (const auto& boundary : payload->m_Boundaries)
		{
			retainedBytes += sizeof(NonFiniteScanRecord) + boundary.m_BufferLayout.m_Components.size() * sizeof(NonFiniteBufferComponent);
		}
		m_RetainedScans.push_back({ payload, retainedBytes, 1u });
		for (const auto& request : tap.m_Requests)
		{
			m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = FrameCaptureSource::NonFiniteScan,
				.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = tap.m_FrameSerial,
				.m_Graph = tap.m_Graph, .m_NonFiniteScans = payload,
				.m_GraphExportActive = tap.m_GraphExportActive, .m_ResourceExportActive = tap.m_ResourceExportActive,
				.m_NonFiniteScanActive = true, .m_SubmissionFenceIndex = tap.m_Fence.m_Fence.Index(),
				.m_SubmissionFenceGeneration = tap.m_Fence.m_Fence.Generation(), .m_SubmissionFenceValue = tap.m_Fence.m_Value,
				.m_AdapterCompatibilityIdentity = tap.m_AdapterCompatibilityIdentity });
		}
		tap.m_Requests.clear();
	}
}
