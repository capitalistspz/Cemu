#pragma once

#include "input/motion/MotionHandler.h"
#include "WiimoteDevice.h"
#include "WiimoteDataTypes.h"
#include "WiimoteComponents.h"

#include "input/api/ControllerProvider.h"

#include <list>
#include <variant>
#include <boost/ptr_container/ptr_vector.hpp>

#ifndef HAS_WIIMOTE
#define HAS_WIIMOTE 1
#endif

#define WIIMOTE_DEBUG 1

class WiimoteControllerProvider : public ControllerProviderBase
{
	friend class WiimoteController;
public:
	constexpr static uint32 kDefaultPacketDelay = 25;

	WiimoteControllerProvider();
	~WiimoteControllerProvider();

	inline static InputAPI::Type kAPIType = InputAPI::Wiimote;
	InputAPI::Type api() const override { return kAPIType; }

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;

	bool is_connected(size_t index);
	bool is_registered_device(size_t index);
	void set_rumble(size_t index, bool state);
	void request_status(size_t index);
	void set_led(size_t index, size_t player_index);

	uint32 get_packet_delay(size_t index);
	void set_packet_delay(size_t index, uint32 delay);


	WiimoteState get_state(size_t index);
	

private:
	std::atomic_bool m_running = false;
	std::thread m_reader_thread, m_writer_thread;

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

	std::list<std::pair<size_t,std::vector<uint8>>> m_write_queue;
	std::mutex m_writer_mutex;
	std::condition_variable m_writer_cond;

	void reader_thread();
	void writer_thread();

	void calibrate(size_t index);
	IRMode set_ir_camera(size_t index, bool state);

	void send_packet(size_t index, std::vector<uint8> data);
	void send_read_packet(size_t index, MemoryType type, RegisterAddress address, uint16 size);
	void send_write_packet(size_t index, MemoryType type, RegisterAddress address, const std::vector<uint8>& data);

	void parse_acceleration(WiimoteState& wiimote_state, const uint8*& data);

	void rotate_ir(WiimoteState& wiimote_state);
	void calculate_ir_position(WiimoteState& wiimote_state);
	int parse_ir(WiimoteState& wiimote_state, const uint8* data);

	void request_extension(size_t index);
	void detect_motion_plus(size_t index);
	void set_motion_plus(size_t index, bool state);

	void update_report_type(size_t index);
};



