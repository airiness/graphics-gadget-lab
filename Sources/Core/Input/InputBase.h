#pragma once

namespace gglab
{
	class InputBase
	{
	public:
		explicit InputBase(GameInputKind inputKind) noexcept;
		virtual ~InputBase() noexcept;

		virtual void Update() noexcept = 0;
		bool IsConnected() const noexcept;

	protected:
		static void CALLBACK OnGameInputDevice(
			GameInputCallbackToken callbackToken,
			void* context,
			IGameInputDevice* gameInputDevice,
			uint64_t timestamp,
			GameInputDeviceStatus currentStatus,
			GameInputDeviceStatus previousStatus) noexcept;

	protected:
		ComPtr<IGameInput> m_GameInput;
		GameInputCallbackToken m_CallbackToken = 0;
		uint32_t m_Connected = 0;
	};
}