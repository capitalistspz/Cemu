#pragma once

#include "WiimoteHIDReportIds.h"

template<typename T>
concept MessageConcept = std::is_trivially_copyable_v<T>;

class WiimoteHandler {
  public:
	class Queuer {
	  public:
		virtual ~Queuer() = default;
		virtual void Enqueue(unsigned index, std::span<const uint8>) = 0;
	};

	struct Calibration
	{
		glm::vec3 zero{512, 512, 512};
		glm::vec3 gravity{576, 576, 576};
	};

	struct State
	{
		bool extensionConnected;
		bool rumble;
		bool ir;
		uint8 battery;
		uint16 buttons;
		glm::vec3 acceleration;
		glm::vec3 accelerationPrev;
		Calibration calibration;
	};

	explicit WiimoteHandler(unsigned index, Queuer* queuer);

	void EnableIR(bool enable);
	void EnableRumble(bool enable);
	void SetLED(uint8 ledMask);

	bool Parse(std::span<const uint8> data);
	[[nodiscard]] State GetState() const;
	[[nodiscard]] Calibration GetCalibration() const;

  private:
	void ParseExtensionData(std::span<uint8_t> extensionData);
	void ParseIRData(std::span<uint8_t> irData);
	void RequestStatus();
	void RequestExtension();
	void RequestWriteByte(uint32 address, uint8 byte);
	void RequestWrite(uint32 address, std::span<const uint8> data);
	void RequestRead(uint32 address, uint16 size);
	void Send(const MessageConcept auto& val);
	void SetReportMode(ResponseReportId report, bool continuous);

  private:
	unsigned m_index;
	Queuer* m_queueOwner;
	State m_state;
	Calibration m_calibration;
	ResponseReportId m_dataReport;
};
