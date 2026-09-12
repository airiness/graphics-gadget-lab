#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/IBLCacheControlBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewControlBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewViewBase.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	class CameraRig;
	class ShaderManager;
	class TaskSystem;
	class World;

	// Move-only handle for one active render host frame. The handle aborts the
	// frame when it is destroyed before the host has ended it, so every
	// early-return path retires the frame's owned resources. Non-ready begin
	// results carry no active frame and are safe to ignore.
	class RenderFrame
	{
	public:
		RenderFrame() noexcept = default;
		// Non-ready begin results carry no active frame; only a host-begun ready
		// frame owns the abort obligation.
		explicit RenderFrame(RHIFrameBeginStatus beginStatus) noexcept :
			m_BeginStatus(beginStatus)
		{
			GGLAB_ASSERT(beginStatus != RHIFrameBeginStatus::Ready);
		}
		GGLAB_DELETE_COPYABLE(RenderFrame)
		RenderFrame(RenderFrame&& other) noexcept;
		RenderFrame& operator=(RenderFrame&& other) noexcept;
		~RenderFrame() noexcept;

		[[nodiscard]] bool IsValid() const noexcept { return m_Host != nullptr; }
		[[nodiscard]] RHIFrameBeginStatus GetBeginStatus() const noexcept
		{
			return m_BeginStatus;
		}
		[[nodiscard]] bool IsReady() const noexcept
		{
			return m_BeginStatus == RHIFrameBeginStatus::Ready;
		}
		[[nodiscard]] bool IsUnavailable() const noexcept
		{
			return m_BeginStatus == RHIFrameBeginStatus::Unavailable;
		}
		[[nodiscard]] bool IsFatal() const noexcept
		{
			return m_BeginStatus == RHIFrameBeginStatus::Fatal;
		}
		[[nodiscard]] uint64_t GetSerial() const noexcept { return m_FrameSerial; }
		[[nodiscard]] uint32_t GetFrameSlotIndex() const noexcept { return m_FrameSlotIndex; }
		[[nodiscard]] uint32_t GetBackBufferIndex() const noexcept { return m_BackBufferIndex; }

	private:
		friend class RenderHost;

		RenderFrame(RenderHost* host, uint64_t frameSerial, uint32_t frameSlotIndex,
			uint32_t backBufferIndex) noexcept :
			m_Host(host), m_FrameSerial(frameSerial), m_FrameSlotIndex(frameSlotIndex),
			m_BackBufferIndex(backBufferIndex), m_BeginStatus(RHIFrameBeginStatus::Ready)
		{
			GGLAB_ASSERT_NOT_NULL(host);
		}

		void Reset() noexcept;

		RenderHost* m_Host = nullptr;
		uint64_t m_FrameSerial = 0;
		uint32_t m_FrameSlotIndex = std::numeric_limits<uint32_t>::max();
		uint32_t m_BackBufferIndex = std::numeric_limits<uint32_t>::max();
		RHIFrameBeginStatus m_BeginStatus = RHIFrameBeginStatus::Fatal;
	};

	// Owned frame values published by a host frame build. The GPU allocation and
	// upload-fence handoff stays inside the host; the result carries only the
	// Public CPU frame values a caller needs for validation, graph authoring and
	// tooling.
	struct RenderFrameBuildResult
	{
		std::vector<RenderView> m_RenderViews;
		std::array<ResolvedViewRenderSettings, utils::ToIndex(RenderViewID::Count)>
			m_ViewRenderSettings{};
		ResolvedTemporalFramePlan m_TemporalFramePlan{};
		TemporalFrameTransaction* m_TemporalFrameTransaction = nullptr;
		RenderScene m_RenderScene{};
		std::array<RenderQueue, utils::ToIndex(RenderViewID::Count)> m_RenderQueues{};
		DebugDrawFrameView m_DebugDrawFrame{};
		DebugDrawCullContext m_DebugDrawCullContext{};
		RenderSceneBuildStatus m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed;
		RenderViewID m_DisplayViewId = RenderViewID::Main;
		DirectionalShadowSettings m_DirectionalShadowSettings =
			DisabledDirectionalShadowSettings();
		const ShadowVisualizationSettings* m_ShadowVisualizationSettings = nullptr;
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		uint64_t m_FrameSerial = 0;

		[[nodiscard]] RenderFrameContext MakeRenderFrameContext() noexcept
		{
			return RenderFrameContext{
				.m_RenderViews = std::span<RenderView>(m_RenderViews),
				.m_ViewRenderSettings =
					std::span<const ResolvedViewRenderSettings>(m_ViewRenderSettings),
				.m_TemporalFramePlan = m_TemporalFramePlan,
				.m_TemporalFrameTransaction = m_TemporalFrameTransaction,
				.m_DisplayViewId = m_DisplayViewId,
				.m_RenderScene = m_RenderScene,
				.m_RenderQueues = std::span<const RenderQueue>(m_RenderQueues),
				.m_DebugDrawFrame = m_DebugDrawFrame,
				.m_DirectionalShadowSettings = m_DirectionalShadowSettings,
				.m_ShadowVisualizationSettings = m_ShadowVisualizationSettings,
				.m_FrameSlotIndex = m_FrameSlotIndex,
				.m_BackBufferIndex = m_BackBufferIndex,
				.m_FrameSerial = m_FrameSerial,
				.m_RenderSceneStatus = m_RenderSceneStatus,
			};
		}
	};

	// Borrowed frame-build inputs. The host owns the renderer, asset manager and
	// GPU resources; the caller supplies only session content and frame identity.
	struct RenderFrameBuildRequest
	{
		World& m_World;
		CameraRig& m_CameraRig;
		const ViewRenderProfile& m_ViewRenderProfile;
		ShadowVisualizationSettings& m_ShadowVisualizationSettings;
		ResolvedTemporalFramePlan m_TemporalFramePlan{};
		TemporalFrameTransaction& m_TemporalFrameTransaction;
		RenderViewID m_DisplayViewId = RenderViewID::Main;
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		uint64_t m_FrameSerial = 0;
	};

	// Narrow Runtime render host contract used by the application runtime to
	// drive lifecycle and frame orchestration. It intentionally exposes no
	// concrete renderer accessor and no pass or content service locator; the
	// transitional concrete access path lives in the legacy bridge.
	class RenderHost
	{
	public:
		RenderHost() noexcept = default;
		GGLAB_DELETE_COPYABLE(RenderHost)
		virtual ~RenderHost() = default;

		[[nodiscard]] virtual bool IsInitialized() const noexcept = 0;
		virtual void Finalize() noexcept = 0;
		virtual void OnResize(uint32_t width, uint32_t height) noexcept = 0;
		virtual void OnSuspend() noexcept = 0;
		virtual void OnResume() noexcept = 0;
		[[nodiscard]] virtual bool IsSuspended() const noexcept = 0;

		[[nodiscard]] virtual RHIContext* GetRHIContext() const noexcept = 0;

		[[nodiscard]] virtual RenderFrame BeginFrame() noexcept = 0;
		[[nodiscard]] virtual RenderFrameBuildResult BuildFrame(
			const RenderFrameBuildRequest& request) noexcept = 0;
		[[nodiscard]] virtual TemporalFrameTransaction& BeginTemporalFrame(RenderFrame& frame,
			const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept = 0;
		virtual void InvalidateTemporalFrameAfterLateContractFailure(RenderFrame& frame) noexcept = 0;
		virtual void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept = 0;
		[[nodiscard]] virtual RenderGraph::CreateInfo CreateRenderGraphCreateInfo()
			const noexcept = 0;
		[[nodiscard]] virtual const TemporalAACapabilityStatus& GetTemporalAACapabilityStatus()
			const noexcept = 0;
		virtual void Render(RenderFrame& frame, RenderGraph& rg,
			const RenderFrameContext& renderContext) noexcept = 0;
		[[nodiscard]] virtual RHIFrameEndResult EndFrame(RenderFrame& frame) noexcept = 0;

		[[nodiscard]] virtual EnvironmentLightingViewBase* GetEnvironmentLightingView()
			const noexcept = 0;
		[[nodiscard]] virtual EnvironmentLightingControlBase* GetEnvironmentLightingControl()
			const noexcept = 0;
		[[nodiscard]] virtual IBLCacheControlBase* GetIBLCacheControl() const noexcept = 0;
		[[nodiscard]] virtual IBLPreviewViewBase* GetIBLPreviewView() const noexcept = 0;
		[[nodiscard]] virtual IBLPreviewControlBase* GetIBLPreviewControl() const noexcept = 0;
		[[nodiscard]] virtual PostProcessPreviewViewBase* GetPostProcessPreviewView()
			const noexcept = 0;
		[[nodiscard]] virtual PostProcessPreviewControlBase* GetPostProcessPreviewControl()
			const noexcept = 0;
		[[nodiscard]] virtual ShadowPreviewViewBase* GetShadowPreviewView() const noexcept = 0;
		[[nodiscard]] virtual GpuProfilingViewBase* GetGpuProfilingView() const noexcept = 0;
		[[nodiscard]] virtual GpuProfilingControlBase* GetGpuProfilingControl() const noexcept = 0;

	protected:
		[[nodiscard]] static RenderFrame MakeReadyFrame(RenderHost* host, uint64_t frameSerial,
			uint32_t frameSlotIndex, uint32_t backBufferIndex) noexcept;

	private:
		friend class RenderFrame;
		virtual void AbortFrame(uint64_t frameSerial) noexcept = 0;
	};

	// Host-supplied composition inputs for the Runtime-owned render host.
	struct RenderHostCreateInfo
	{
		const RHIContextFactoryBase* m_RHIContextFactory = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		std::filesystem::path m_IblDerivedDataCacheDirectory;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::optional<std::string> m_AdapterSelector;
		bool m_EnableDebugValidation = false;

		[[nodiscard]] bool HasRequiredRuntimePaths() const noexcept
		{
			return !m_IblDerivedDataCacheDirectory.empty();
		}
	};

	// Runtime render host plus the stable explicit service bundle. The host is
	// null when the host inputs are missing or initialization fails.
	struct RenderHostInstance
	{
		std::unique_ptr<RenderHost> m_Host;
		RenderServices m_Services;
	};

	// Creates and initializes the Runtime render host and its explicit service
	// bundle. The host is null when the host-supplied context factory or runtime
	// paths are missing, or when host initialization fails.
	[[nodiscard]] RenderHostInstance CreateRenderHost(
		const RenderHostCreateInfo& createInfo) noexcept;

	inline RenderFrame RenderHost::MakeReadyFrame(RenderHost* host, uint64_t frameSerial,
		uint32_t frameSlotIndex, uint32_t backBufferIndex) noexcept
	{
		return RenderFrame(host, frameSerial, frameSlotIndex, backBufferIndex);
	}

	inline RenderFrame::RenderFrame(RenderFrame&& other) noexcept :
		m_Host(std::exchange(other.m_Host, nullptr)),
		m_FrameSerial(other.m_FrameSerial),
		m_FrameSlotIndex(other.m_FrameSlotIndex),
		m_BackBufferIndex(other.m_BackBufferIndex),
		m_BeginStatus(other.m_BeginStatus)
	{
	}

	inline RenderFrame& RenderFrame::operator=(RenderFrame&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Host = std::exchange(other.m_Host, nullptr);
			m_FrameSerial = other.m_FrameSerial;
			m_FrameSlotIndex = other.m_FrameSlotIndex;
			m_BackBufferIndex = other.m_BackBufferIndex;
			m_BeginStatus = other.m_BeginStatus;
		}
		return *this;
	}

	inline void RenderFrame::Reset() noexcept
	{
		if (m_Host && m_BeginStatus == RHIFrameBeginStatus::Ready)
		{
			m_Host->AbortFrame(m_FrameSerial);
		}
		m_Host = nullptr;
		m_FrameSerial = 0;
		m_FrameSlotIndex = std::numeric_limits<uint32_t>::max();
		m_BackBufferIndex = std::numeric_limits<uint32_t>::max();
		m_BeginStatus = RHIFrameBeginStatus::Fatal;
	}

	inline RenderFrame::~RenderFrame() noexcept
	{
		Reset();
	}
}
