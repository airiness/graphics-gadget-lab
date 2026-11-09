#pragma once
#include "GGLabFoundation/Platform/Win/ComTypes.h"

#include <Windows.h>
#include <GameInput.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace gglab
{
	class InputBase
	{
	public:
		InputBase(IGameInput* gameInput, GameInputKind inputKind) noexcept;
		virtual ~InputBase() noexcept;

		virtual void Update() noexcept = 0;
		bool IsAvailable() const noexcept;
		bool IsConnected() const noexcept;

	protected:
		void CopyConnectedDevices(std::vector<ComPtr<IGameInputDevice>>& devices) const noexcept;

		static void CALLBACK OnGameInputDevice(GameInputCallbackToken callbackToken, void* context,
			IGameInputDevice* gameInputDevice, uint64_t timestamp,
			GameInputDeviceStatus currentStatus, GameInputDeviceStatus previousStatus) noexcept;

	protected:
		ComPtr<IGameInput> m_GameInput;
		GameInputCallbackToken m_CallbackToken = 0;

	private:
		mutable std::mutex m_DeviceMutex;
		std::vector<ComPtr<IGameInputDevice>> m_ConnectedDevices;
	};
}
