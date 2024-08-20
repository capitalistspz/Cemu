#pragma once

#include "WiimoteHandler.h"
#include "input/motion/MotionHandler.h"
#include "input/api/Wiimote/WiimoteDevice.h"
#include "input/api/Wiimote/WiimoteMessages.h"

#include "input/api/ControllerProvider.h"
#include "input/api/ControllerState.h"

#include <variant>
#include <boost/ptr_container/ptr_vector.hpp>
#include <list>

#ifndef HAS_WIIMOTE
#define HAS_WIIMOTE 1
#endif

#define WIIMOTE_DEBUG 1

class WiimoteControllerProvider : public ControllerProviderBase, public WiimoteHandler::Queuer {
	friend class WiimoteController;

  public:
	constexpr static uint32 kDefaultPacketDelay = 25;

	WiimoteControllerProvider();
	~WiimoteControllerProvider() override;

	inline static InputAPI::Type kAPIType = InputAPI::Wiimote;
	InputAPI::Type api() const override
	{
		return kAPIType;
	}

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;

	bool is_connected(size_t index);
	bool is_registered_device(size_t index);
	void set_rumble(size_t index, bool state);
	void set_led(size_t index, size_t player_index);

	uint32 get_packet_delay(size_t index);
	void set_packet_delay(size_t index, uint32 delay);

	struct WiimoteState
	{
		uint16 buttons = 0;
		uint8 flags = 0;
		uint8 battery_level = 0;

		float m_roll = 0;

		MotionSample motion_sample{};
		WiiUMotionHandler motion_handler{};

		bool m_calibrated = false;
		Calibration m_calib_acceleration{};

		struct IRCamera
		{
			IRMode mode = kIRDisabled;
			std::array<IRDot, 4> dots{}, prev_dots{};

			glm::vec2 position{}, m_prev_position{};
			PositionVisibility m_positionVisibility;
			glm::vec2 middle{};
			float distance = 0;
			std::pair<sint32, sint32> indices{0, 1};
		} ir_camera{};

		std::optional<MotionPlusData> m_motion_plus;
		std::variant<std::monostate, NunchuckData, ClassicData> m_extension{};
	};
	WiimoteState get_state(size_t index);

  private:
	std::jthread m_reader_thread, m_writer_thread;

	std::shared_mutex m_device_mutex;

	struct Wiimote
	{
		Wiimote(WiimoteDevicePtr device)
			: device(std::move(device)) {}

		WiimoteDevicePtr device;
		std::atomic_bool connected = true;
		std::atomic_bool rumble = false;

		std::shared_mutex mutex;
		WiimoteState state{};

		std::atomic_uint32_t data_delay = kDefaultPacketDelay;
		std::chrono::high_resolution_clock::time_point data_ts{};
	};

	boost::ptr_vector<Wiimote> m_wiimotes;
	std::list<std::pair<size_t, std::vector<uint8>>> m_write_queue;
	std::mutex m_writer_mutex;
	std::condition_variable m_writer_cond;

	void reader_thread(std::stop_token);
	void writer_thread(std::stop_token);

	IRMode set_ir_camera(size_t index, bool state);

	void Enqueue(unsigned index, std::span<const uint8> data) override;

	std::vector<WiimoteHandler> m_handlers;
};
