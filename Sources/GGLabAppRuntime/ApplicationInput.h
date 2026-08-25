#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	enum class AppInputKey : uint8_t
	{
		A,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,
		D0,
		D1,
		D2,
		D3,
		D4,
		D5,
		D6,
		D7,
		D8,
		D9,
		Escape,
		Space,
		LeftShift,
		RightShift,
		Count,
	};

	enum class AppPointerButton : uint8_t
	{
		Left,
		Middle,
		Right,
		X1,
		X2,
		Count,
	};

	enum class AppPointerMode : uint8_t
	{
		Absolute,
		Relative,
	};

	struct AppInputVector2
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
	};

	inline constexpr size_t AppInputKeyCount = static_cast<size_t>(AppInputKey::Count);
	inline constexpr size_t AppPointerButtonCount =
		static_cast<size_t>(AppPointerButton::Count);

	struct ApplicationInputSnapshot
	{
		std::array<bool, AppInputKeyCount> m_KeysHeld{};
		std::array<bool, AppPointerButtonCount> m_PointerButtonsHeld{};
		AppInputVector2 m_PointerPosition{};
		AppInputVector2 m_PointerDelta{};
		int64_t m_ScrollDeltaY = 0;
		bool m_IsAvailable = false;
	};

	class ApplicationInput final
	{
	public:
		void Publish(const ApplicationInputSnapshot& snapshot) noexcept;
		void Reset() noexcept;

		[[nodiscard]] bool IsKeyPressed(AppInputKey key) const noexcept;
		[[nodiscard]] bool IsKeyReleased(AppInputKey key) const noexcept;
		[[nodiscard]] bool IsKeyHeld(AppInputKey key) const noexcept;

		[[nodiscard]] bool IsPointerButtonPressed(AppPointerButton button) const noexcept;
		[[nodiscard]] bool IsPointerButtonReleased(AppPointerButton button) const noexcept;
		[[nodiscard]] bool IsPointerButtonHeld(AppPointerButton button) const noexcept;

		[[nodiscard]] AppInputVector2 GetPointerPosition() const noexcept;
		[[nodiscard]] AppInputVector2 GetPointerDelta() const noexcept;
		[[nodiscard]] int64_t GetScrollDeltaY() const noexcept;

		void SetPointerMode(AppPointerMode mode) noexcept;
		[[nodiscard]] AppPointerMode GetPointerMode() const noexcept;

		void SetUICaptureState(bool keyboardCaptured, bool pointerCaptured) noexcept;
		[[nodiscard]] bool IsKeyboardCapturedByUI() const noexcept;
		[[nodiscard]] bool IsPointerCapturedByUI() const noexcept;
		[[nodiscard]] bool IsAvailable() const noexcept;

	private:
		ApplicationInputSnapshot m_Previous{};
		ApplicationInputSnapshot m_Current{};
		AppPointerMode m_PointerMode = AppPointerMode::Relative;
		bool m_KeyboardCapturedByUI = false;
		bool m_PointerCapturedByUI = false;
	};
}
