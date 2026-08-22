#pragma once

#include "AppRuntimeConfig.h"
#include "AppRuntimeHostServices.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "RuntimePaths.h"

#include <cstdint>

namespace gglab
{
	enum class AppRuntimeLifecycleState : uint8_t
	{
		Uninitialized,
		Initializing,
		Running,
		Suspended,
		ExitRequested,
		Failed,
		ShuttingDown,
		Stopped,
	};

	enum class AppRuntimeInitializeResult : uint8_t
	{
		Succeeded,
		AlreadyInitialized,
		InvalidState,
		InvalidConfig,
		InvalidRuntimePaths,
	};

	enum class AppRuntimeTickResult : uint8_t
	{
		Continue,
		Suspended,
		Exit,
	};

	enum class AppHostEventType : uint8_t
	{
		Suspended,
		Resumed,
		ExitRequested,
	};

	struct GGLabAppRuntimeCreateInfo
	{
		AppRuntimeConfig m_Config{};
		RuntimePaths m_Paths{};
		AppRuntimeHostServices m_HostServices{};
	};

	class GGLabAppRuntime final
	{
	public:
		GGLabAppRuntime() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(GGLabAppRuntime);
		~GGLabAppRuntime() noexcept;

		[[nodiscard]] AppRuntimeInitializeResult Initialize(
			const GGLabAppRuntimeCreateInfo& createInfo) noexcept;
		[[nodiscard]] AppRuntimeTickResult Tick() const noexcept;
		void HandleHostEvent(AppHostEventType eventType) noexcept;
		void Shutdown() noexcept;

		[[nodiscard]] AppRuntimeLifecycleState GetLifecycleState() const noexcept
		{
			return m_LifecycleState;
		}

	private:
		AppRuntimeLifecycleState m_LifecycleState = AppRuntimeLifecycleState::Uninitialized;
		bool m_ShutdownComplete = false;
	};
}
