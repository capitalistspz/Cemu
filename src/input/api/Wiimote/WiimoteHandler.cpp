//
// Created by pz on 31/07/24.
//

#include "WiimoteHandler.h"
#include "WiimoteHIDMessages.h"

enum Addresses : uint32
{
	ADDR_REG_IR_SENS_BLOCK_1 = 0x04b0'0000,
	ADDR_REG_IR_SENS_BLOCK_2 = 0x04b00'001a,
	ADDR_REG_IR_ENABLE = 0x04b0'0030,
	ADDR_REG_IR_MODE = 0x04b0'0033,

	ADDR_REG_EXT_ID_4B = 0x04a400fc
};

#pragma pack(push, 1)
namespace Messages
{
	template<typename T>
	concept HasAcc = requires(T t) {
		requires std::same_as<decltype(t.core), uint16>;
		requires std::same_as<decltype(t.acc), glm::u8vec3>;
	};

	struct Status
	{
		uint16 core;
		WiimoteMsg::StatusFlags flags;
		uint16 zeroes;
		uint8 batteryLevel;
	};
	static_assert(sizeof(Status) == 6);

	struct Read
	{
		uint16 core;
		uint8 sizeAndError;
		uint16be address;
		uint8 data[16];
	};
	static_assert(sizeof(Read) == 21);

	struct CoreAcc
	{
		uint16 core;
		glm::u8vec3 acc;
	};
	static_assert(sizeof(CoreAcc) == 5);
	static_assert(HasAcc<CoreAcc>);

	struct CoreExt8
	{
		uint16 core;
		uint8_t extData[8];
	};
	static_assert(sizeof(CoreExt8) == 10);

	struct CoreAccIR12
	{
		uint16 core;
		glm::u8vec3 acc;
		uint8 irData[12];
	};
	static_assert(sizeof(CoreAccIR12) == 17);
	static_assert(HasAcc<CoreAccIR12>);

	struct CoreExt19
	{
		uint16 core;
		uint8 extData[19];
	};
	static_assert(sizeof(CoreExt19) == 21);

	struct CoreAccExt16
	{
		uint16 core;
		glm::u8vec3 acc;
		uint8 extData[16];
	};
	static_assert(sizeof(CoreAccExt16) == 21);
	static_assert(HasAcc<CoreAccExt16>);

	struct CoreIR10Ext9
	{
		uint16 core;
		uint8 irData[10];
		uint8 extData[9];
	};
	static_assert(sizeof(CoreIR10Ext9) == 21);

	struct CoreAccIR10Ext6
	{
		uint16 core;
		glm::u8vec3 acc;
		uint8 irData[10];
		uint8 extData[6];
	};
	static_assert(sizeof(CoreAccIR10Ext6) == 21);
	static_assert(HasAcc<CoreAccIR10Ext6>);

	static_assert(sizeof(CoreAcc::acc) == 3);

} // namespace Messages

struct Response
{
	ResponseReportId type;
	union U
	{
		Messages::Status status;
		Messages::Read read;
		uint16 core;
		Messages::CoreAcc coreAcc;
		Messages::CoreExt8 coreExt8;
		Messages::CoreAccIR12 coreAccIr12;
		Messages::CoreExt19 coreExt19;
		Messages::CoreAccExt16 coreAccExt16;
		Messages::CoreIR10Ext9 coreIr10Ext9;
		Messages::CoreAccIR10Ext6 coreAccIr10Ext6;
	} u;
};
static_assert(offsetof(Response, u) == 1);

#pragma pack(pop)

template<typename... T>
static void WiimoteHandlerLog(fmt::format_string<T...> format_string, T&&... t)
{
	cemuLog_log(LogType::Force, std::string("WiimoteHandler: ") + fmt::format(format_string, std::forward<T>(t)...));
}

WiimoteHandler::WiimoteHandler(unsigned index, Queuer* queuer)
	: m_index(index), m_queueOwner(queuer), m_state()
{
	SetReportMode(ResponseReportId::DataCoreAcc, true);
}
void WiimoteHandler::EnableIR(bool enable)
{
	constexpr static uint8 irSensBlock1[] = {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64};
	constexpr static uint8 irSensBlock2[] = {0x63, 0x03};

	m_state.ir = enable;
	uint8 ir1[2] = {};
	ir1[0] = static_cast<uint8>(RequestReportId::IR1);
	ir1[1] = enable * 0x4;
	Send(ir1);

	uint8 ir2[2] = {};
	ir1[0] = static_cast<uint8>(RequestReportId::IR2);
	ir1[1] = enable * 0x4;
	Send(ir2);

	if (!enable)
		return;

	RequestWriteByte(ADDR_REG_IR_ENABLE, 0x01);
	RequestWrite(ADDR_REG_IR_SENS_BLOCK_1, irSensBlock1);
	RequestWrite(ADDR_REG_IR_SENS_BLOCK_2, irSensBlock2);
	if (m_state.extensionConnected)
		RequestWriteByte(ADDR_REG_IR_MODE, 0x01);
	else
		RequestWriteByte(ADDR_REG_IR_MODE, 0x03);
	RequestWriteByte(ADDR_REG_IR_ENABLE, 0x08);
}

void WiimoteHandler::EnableRumble(bool enable)
{
	m_state.rumble = enable;
	// Any request will do
	RequestStatus();
}
void WiimoteHandler::SetLED(uint8 ledMask)
{
	WiimoteMsg::RequestLED req{
		.flags = req.flags = ledMask << 4};
	Send(req);
}

namespace
{
	inline void HandleAcc(WiimoteHandler::State& state, const Messages::HasAcc auto& msg)
	{
		state.accelerationPrev = state.acceleration;
		const glm::u16vec3 acc{
			msg.acc.x << 2 | msg.core & 0b0110'0000'0000'0000 >> 13,
			msg.acc.y << 2 | msg.core & 0b0000'0000'0010'0000 >> 5,
			msg.acc.z << 2 | msg.core & 0b0000'0000'0100'0000 >> 6};

		const auto& [zero, gravity] = state.calibration;
		state.acceleration = (glm::vec3(acc) - zero) / (gravity - zero);
	}

	inline void HandleButtons(WiimoteHandler::State& state, uint16 buttons)
	{
		constexpr static uint16 buttonMask = 0b1001'1111'0001'1111;
		state.buttons = buttons & buttonMask;
	}
} // namespace

bool WiimoteHandler::Parse(std::span<const uint8> data)
{
	const auto size = data.size();
	if (size < 2)
		return false;
	if (size > 22)
		cemu_assert_suspicious();
	auto it = data.begin();
	const auto reportId = static_cast<ResponseReportId>(*it);
	if (reportId < ResponseReportId::Status || reportId > ResponseReportId::DataExt21)
		return false;

	Response response;
	std::memcpy(&response, data.data(), size);
	WiimoteHandlerLog("Report {:#02x} received", reportId);
	switch (reportId)
	{
		using enum ResponseReportId;
	case Status:
	{
		const auto& status = response.u.status;
		HandleButtons(m_state, status.core);
		bool extCon = HAS_FLAG(status.flags, WiimoteMsg::STATUS_FLAG_EXTENSION);
		if (extCon != m_state.extensionConnected)
		{
			RequestExtension();
			m_state.extensionConnected = extCon;
		}
		m_state.battery = status.batteryLevel;
		break;
	}
	case Read:
	{
		const auto& [buttons, sizeAndError, address, data] = response.u.read;
		HandleButtons(m_state, buttons);
		WiimoteHandlerLog("Read status {}, size: {}, addr: {:#02x} data: [{:02x}]",
						  sizeAndError & 0xF, sizeAndError >> 8, address, fmt::join(data, " "));
		break;
	}
	case Acknowledge:
	{
		break;
	}
	case DataCore:
	{
		HandleButtons(m_state, response.u.core);
		break;
	}
	case DataCoreAcc:
	{
		auto& resp = response.u.coreAcc;
		HandleButtons(m_state, resp.core);
		HandleAcc(m_state, resp);
		break;
	}
	case DataCoreExt8:
	{
		auto& resp = response.u.coreExt8;
		HandleButtons(m_state, resp.core);
		ParseExtensionData(resp.extData);
		break;
	}
	case DataCoreAccIR12:
	{
		auto& resp = response.u.coreAccIr12;
		HandleButtons(m_state, resp.core);
		HandleAcc(m_state, resp);
		ParseIRData(resp.irData);
		break;
	}
	case DataCoreExt19:
	{
		auto& resp = response.u.coreExt19;
		HandleButtons(m_state, resp.core);
		ParseExtensionData(resp.extData);
		break;
	}
	case DataCoreAccExt16:
	{
		auto& resp = response.u.coreAccExt16;
		HandleButtons(m_state, resp.core);
		HandleAcc(m_state, resp);
		ParseExtensionData(resp.extData);
		break;
	}
	case DataCoreIR10Ext9:
	{
		auto& resp = response.u.coreIr10Ext9;
		HandleButtons(m_state, resp.core);
		ParseIRData(resp.irData);
		ParseExtensionData(resp.extData);
		break;
	}
	case DataCoreAccIR10Ext6:
	{
		auto& resp = response.u.coreAccIr10Ext6;
		HandleButtons(m_state, resp.core);
		HandleAcc(m_state, resp);
		ParseIRData(resp.irData);
		ParseExtensionData(resp.extData);
		break;
	}
	default:
		WiimoteHandlerLog("Report {:#02x} not yet handled", reportId);
		cemu_assert_unimplemented();
		return false;
	}
	return true;
}

WiimoteHandler::State WiimoteHandler::GetState() const
{
	return m_state;
}
WiimoteHandler::Calibration WiimoteHandler::GetCalibration() const
{
	return m_calibration;
}
void WiimoteHandler::ParseExtensionData(std::span<uint8_t> extensionData)
{
	const auto invalid = std::ranges::all_of(extensionData, [](uint8 val) { return val == 0xFF; });
	if (invalid)
	{
		WiimoteHandlerLog("Extension data was invalid");
		return;
	}
	// TODO: Extension data parsing
}
void WiimoteHandler::ParseIRData(std::span<uint8_t> irData)
{
	const auto size = irData.size();
	cemu_assert_debug(size == 12 || size == 10);
	// TODO: IR Data parsing
}
void WiimoteHandler::RequestStatus()
{
	Send(WiimoteMsg::RequestStatus{});
}

void WiimoteHandler::RequestExtension()
{
	WiimoteHandlerLog("Requesting extension!");
}
void WiimoteHandler::RequestWriteByte(uint32 address, uint8 byte)
{
	WiimoteMsg::RequestWrite req{
		.address = address,
		.size = 1,
		.data = {byte}};
	Send(req);
}

void WiimoteHandler::RequestWrite(uint32 address, std::span<const uint8> data)
{
	const auto size = data.size();
	cemu_assert_debug(size <= 16);
	WiimoteMsg::RequestWrite req{
		.address = address,
		.size = static_cast<uint8>(size),
		.data = {}};
	std::ranges::copy(data, req.data);
	Send(req);
}
void WiimoteHandler::RequestRead(uint32 address, uint16 size)
{
	const auto addrSpace = address >> 24;
	cemu_assert_debug(addrSpace == 0x0 || addrSpace == 0x4);
	WiimoteMsg::RequestRead req{
		.address = address,
		.size = size};
	Send(req);
}
void WiimoteHandler::SetReportMode(ResponseReportId report, bool continuous)
{
	WiimoteMsg::RequestReportMode req;
	req.flags = continuous * 0x4;
	req.report = report;
	Send(req);
}

void WiimoteHandler::Send(const MessageConcept auto& val)
{
	std::vector<uint8> out(sizeof(val));
	std::memcpy(out.data(), &val, sizeof(val));
	// Rumble bit must be set for each output report
	out[1] |= static_cast<uint8>(m_state.rumble);
	m_queueOwner->Enqueue(m_index, out);
}
