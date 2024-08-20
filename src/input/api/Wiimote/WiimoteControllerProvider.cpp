#include "input/api/Wiimote/WiimoteControllerProvider.h"
#include "input/api/Wiimote/NativeWiimoteController.h"
#include "input/api/Wiimote/WiimoteMessages.h"

#include "input/api/Wiimote/hidapi/HidapiWiimote.h"

#include <numbers>
#include <queue>

WiimoteControllerProvider::WiimoteControllerProvider()
{
	m_reader_thread = std::jthread(&WiimoteControllerProvider::reader_thread, this);
	m_writer_thread = std::jthread(&WiimoteControllerProvider::writer_thread, this);
}

WiimoteControllerProvider::~WiimoteControllerProvider()
{
	m_writer_thread.request_stop();
	m_reader_thread.request_stop();
}

std::vector<std::shared_ptr<ControllerBase>> WiimoteControllerProvider::get_controllers()
{
	std::scoped_lock lock(m_device_mutex);

	std::queue<uint32> disconnected_wiimote_indices;
	for (auto i{0u}; i < m_wiimotes.size(); ++i)
	{
		if (!(m_wiimotes[i].connected = m_wiimotes[i].device->write_data({kStatusRequest, 0x00})))
		{
			disconnected_wiimote_indices.push(i);
		}
	}

	const auto valid_new_device = [&](std::shared_ptr<WiimoteDevice>& device) {
		const auto writeable = device->write_data({kStatusRequest, 0x00});
		const auto not_already_connected =
			std::none_of(m_wiimotes.cbegin(), m_wiimotes.cend(),
						 [device](const auto& it) {
							 return (*it.device == *device) && it.connected;
						 });
		return writeable && not_already_connected;
	};

	for (auto& device : WiimoteDevice_t::get_devices())
	{
		if (!valid_new_device(device))
			continue;
		// Replace disconnected wiimotes
		if (!disconnected_wiimote_indices.empty())
		{
			const auto idx = disconnected_wiimote_indices.front();
			disconnected_wiimote_indices.pop();

			m_wiimotes.replace(idx, std::make_unique<Wiimote>(device));
		}
		// Otherwise add them
		else
		{
			m_wiimotes.push_back(std::make_unique<Wiimote>(device));
		}
	}

	std::vector<std::shared_ptr<ControllerBase>> result;
	for (size_t i = 0; i < m_wiimotes.size(); ++i)
	{
		result.emplace_back(std::make_shared<NativeWiimoteController>(i));
		m_handlers.emplace_back(i, this);
	}

	return result;
}

bool WiimoteControllerProvider::is_connected(size_t index)
{
	std::shared_lock lock(m_device_mutex);
	return index < m_wiimotes.size() && m_wiimotes[index].connected;
}

bool WiimoteControllerProvider::is_registered_device(size_t index)
{
	std::shared_lock lock(m_device_mutex);
	return index < m_wiimotes.size();
}

void WiimoteControllerProvider::set_rumble(size_t index, bool state)
{
	std::shared_lock lock(m_device_mutex);
	if (index >= m_wiimotes.size())
		return;

	m_handlers[index].EnableRumble(state);
	lock.unlock();
}

void WiimoteControllerProvider::set_led(size_t index, size_t player_index)
{
	std::shared_lock lock(m_device_mutex);
	if (index >= m_wiimotes.size())
		return;
	const auto a = player_index / 4;
	const auto b = player_index % 4;
	m_handlers[index].SetLED(1 << b | a * (1 << 4));
}
uint32 WiimoteControllerProvider::get_packet_delay(size_t index)
{
	std::shared_lock lock(m_device_mutex);
	if (index < m_wiimotes.size())
	{
		return m_wiimotes[index].data_delay;
	}

	return kDefaultPacketDelay;
}

void WiimoteControllerProvider::set_packet_delay(size_t index, uint32 delay)
{
	std::shared_lock lock(m_device_mutex);
	if (index < m_wiimotes.size())
	{
		m_wiimotes[index].data_delay = delay;
	}
}

WiimoteControllerProvider::WiimoteState WiimoteControllerProvider::get_state(size_t index)
{
	std::shared_lock lock(m_device_mutex);
	if (index < m_wiimotes.size())
	{
		std::shared_lock data_lock(m_wiimotes[index].mutex);
		return m_wiimotes[index].state;
	}

	return {};
}

void WiimoteControllerProvider::reader_thread(std::stop_token stopToken)
{
	SetThreadName("Wiimote-reader");
	std::chrono::steady_clock::time_point lastCheck = {};
	while (!stopToken.stop_requested())
	{
		const auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck) > std::chrono::seconds(2))
		{
			// check for new connected wiimotes
			get_controllers();
			lastCheck = std::chrono::steady_clock::now();
		}

		bool receivedAnyPacket = false;
		std::shared_lock lock(m_device_mutex);
		for (size_t index = 0; index < m_wiimotes.size(); ++index)
		{
			auto& wiimote = m_wiimotes[index];
			if (!wiimote.connected)
				continue;

			const auto read_data = wiimote.device->read_data();
			if (!read_data || read_data->empty())
				continue;
			receivedAnyPacket = true;

			{
				wiimote.mutex.lock_shared();
				WiimoteState new_state = wiimote.state;
				wiimote.mutex.unlock_shared();

				auto& handler = m_handlers[index];
				handler.Parse(*read_data);
				const auto& handlerState = handler.GetState();

				new_state.buttons = handlerState.buttons;
				new_state.battery_level = handlerState.battery;

				const auto handlerAcc = handlerState.acceleration;

				constexpr static float piHalf = std::numbers::pi / 2;

				new_state.m_roll = std::atan2(handlerAcc.z, handlerAcc.x) - piHalf;

				float acc[3]{-handlerAcc.x, -handlerAcc.z, handlerAcc.y};
				const auto accDiff = length(handlerAcc - handlerState.accelerationPrev);
				float zero3[3]{};
				float zero4[4]{};

				new_state.motion_sample = MotionSample(acc, accDiff, zero3, zero3, zero4);

				wiimote.mutex.lock();
				wiimote.state = new_state;
				wiimote.mutex.unlock();
			}
		}

		lock.unlock();
		if (!receivedAnyPacket)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void WiimoteControllerProvider::writer_thread(std::stop_token stopToken)
{
	SetThreadName("Wiimote-writer");
	while (!stopToken.stop_requested())
	{
		std::unique_lock writer_lock(m_writer_mutex);
		while (m_write_queue.empty())
		{
			if (m_writer_cond.wait_for(writer_lock, std::chrono::milliseconds(250)) == std::cv_status::timeout)
			{
				if (stopToken.stop_requested())
					return;
			}
		}

		auto index = (size_t)-1;
		std::vector<uint8> data;
		std::shared_lock device_lock(m_device_mutex);

		// get first packet of device which is ready to be sent
		const auto now = std::chrono::high_resolution_clock::now();
		for (auto it = m_write_queue.begin(); it != m_write_queue.end(); ++it)
		{
			if (it->first >= m_wiimotes.size())
				continue;

			const auto delay = m_wiimotes[it->first].data_delay.load(std::memory_order_relaxed);
			if (now >= m_wiimotes[it->first].data_ts + std::chrono::milliseconds(delay))
			{
				index = it->first;
				data = std::move(it->second);
				m_write_queue.erase(it);
				break;
			}
		}
		writer_lock.unlock();

		if (index != (size_t)-1 && !data.empty())
		{
			m_wiimotes[index].connected = m_wiimotes[index].device->write_data(data);
			if (m_wiimotes[index].connected)
			{
				m_wiimotes[index].data_ts = std::chrono::high_resolution_clock::now();
			}
		}
		device_lock.unlock();

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		std::this_thread::yield();
	}
}

// Called by handler which is only while m_device_mutex is already locked
void WiimoteControllerProvider::Enqueue(unsigned index, std::span<const uint8> data)
{
	std::unique_lock lock(m_writer_mutex);
	m_write_queue.emplace_back(std::piecewise_construct, std::forward_as_tuple(index), std::forward_as_tuple(data.begin(), data.end()));
	m_writer_cond.notify_one();
}

IRMode WiimoteControllerProvider::set_ir_camera(size_t index, bool state)
{
	std::shared_lock read_lock(m_wiimotes[index].mutex);
	return IRMode::kBasicIR;
}
