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

	struct IRBasic
	{
		uint8 x1;
		uint8 y1;
		uint8 bits;
		uint8 x2;
		uint8 y2;
	};
	static_assert(sizeof(IRBasic) == 5);

	struct IRExtended
	{
		uint8 x;
		uint8 y;
		uint8 bits;
	};
	static_assert(sizeof(IRExtended) == 3);

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
	EnableIR(true);
}
void WiimoteHandler::EnableIR(bool enable)
{
	constexpr static uint8 irSensBlock1[] = {0x02, 0x00, 0x00, 0x71, 0x01, 0x00, 0xaa, 0x00, 0x64};
	constexpr static uint8 irSensBlock2[] = {0x63, 0x03};

	m_state.irEnabled = enable;
	uint8 ir1[2] = {};
	ir1[0] = static_cast<uint8>(RequestReportId::IR1);
	ir1[1] = enable * 0x4;
	Send(ir1);

	uint8 ir2[2] = {};
	ir2[0] = static_cast<uint8>(RequestReportId::IR2);
	ir2[1] = enable * 0x4;
	Send(ir2);

	if (!enable)
		return;

	RequestWriteByte(ADDR_REG_IR_ENABLE, 0x01);
	RequestWrite(ADDR_REG_IR_SENS_BLOCK_1, irSensBlock1);
	RequestWrite(ADDR_REG_IR_SENS_BLOCK_2, irSensBlock2);
	if (m_state.extensionConnected)
	{
		RequestWriteByte(ADDR_REG_IR_MODE, 0x01);
		RequestWriteByte(ADDR_REG_IR_ENABLE, 0x08);
		SetReportMode(ResponseReportId::DataCoreAccIR10Ext6, true);
	}
	else
	{
		RequestWriteByte(ADDR_REG_IR_MODE, 0x03);
		RequestWriteByte(ADDR_REG_IR_ENABLE, 0x08);
		SetReportMode(ResponseReportId::DataCoreAccIR12, true);
	}
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
		.flags = static_cast<uint8>(ledMask << 4)};
	Send(req);
}

namespace
{
	inline void HandleAcc(WiimoteHandler::State& state, const Messages::HasAcc auto& msg)
	{
		constexpr static float piHalf = std::numbers::pi / 2;

		state.accelerationPrev = state.acceleration;
		const glm::u16vec3 acc{
			(msg.acc.x << 2) | ((msg.core & 0b0110'0000'0000'0000) >> 13),
			(msg.acc.y << 2) | ((msg.core & 0b0000'0000'0010'0000) >> 5),
			(msg.acc.z << 2) | ((msg.core & 0b0000'0000'0100'0000) >> 6)};

		const auto& [zero, gravity] = state.calibration;
		state.acceleration = (glm::vec3(acc) - zero) / (gravity - zero);
		state.roll = std::atan2(state.acceleration.z, state.acceleration.x) - piHalf;
	}

	inline void HandleButtons(WiimoteHandler::State& state, uint16 buttons)
	{
		constexpr static uint16 buttonMask = 0b1001'1111'0001'1111;
		state.buttons = buttons & buttonMask;
	}
	constexpr float DegToRad(float angle)
	{
		return angle * (std::numbers::pi / 180);
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

void RotateIR(WiimoteHandler::IR ir, float angle)
{
	const float sin = std::sin(angle);
	const float cos = std::cos(angle);
	for (auto& dot : ir.dots)
	{
		if (!dot.visible)
			continue;
		dot.pos -= 0.5f;
		dot.pos.x = (dot.pos.x * cos) + (dot.pos.y * -sin);
		dot.pos.y = (dot.pos.x * sin) + (dot.pos.y * cos);
		dot.pos += 0.5f;
	}
}



void CalculateIRPos(WiimoteHandler::IR ir)
{
	auto indices = ir.indices;
	if (ir.middle.x != 0)
	{
		const float last_angle = std::atan(ir.middle.y / ir.middle.x);
		float best_distance = std::numeric_limits<float>::max();
		for (size_t i = 0; i < std::size(ir.dots); ++i)
		{
			if (!ir.dots[i].visible)
				continue;

			for (size_t j = i + 1; j < std::size(ir.dots); ++j)
			{
				if (!ir.dots[j].visible)
					continue;

				const auto mid = (ir.dots[i].pos + ir.dots[j].pos) / 2.0f;
				if (mid.x == 0)
					continue;

				// check if angle is close enough to the last known one
				float angle = std::atan(mid.y / mid.x);
				if (std::abs(last_angle - angle) > DegToRad(10.0f))
					continue;

				// check if distance between points is similar to last known distance
				const float distance = std::abs(ir.distance - glm::length(ir.dots[i].pos - ir.dots[j].pos));
				if (distance > 0.1f && distance > best_distance)
					continue;

				// found a new pair
				best_distance = distance;
				indices = {i, j};
			}
		}
	}

	if (ir.dots[indices.first].visible && ir.dots[indices.second].visible)
	{
		ir.dotsPrev[indices.first] = ir.dots[indices.first];
		ir.dotsPrev[indices.second] = ir.dots[indices.second];
		ir.position = (ir.dots[indices.first].pos + ir.dots[indices.second].pos) / 2.0f;

		ir.middle = ir.position;
		ir.distance = glm::length(ir.dots[indices.first].pos - ir.dots[indices.second].pos);
		ir.indices = indices;
		ir.positionVisibility = PositionVisibility::FULL;
	}
	else if (ir.dots[indices.first].visible)
	{
		ir.position = ir.middle + (ir.dots[indices.first].pos - ir.dotsPrev[indices.first].pos);
		ir.positionVisibility = PositionVisibility::PARTIAL;
	}
	else if (ir.dots[indices.second].visible)
	{
		ir.position = ir.middle + (ir.dots[indices.second].pos - ir.dotsPrev[indices.second].pos);
		ir.positionVisibility = PositionVisibility::PARTIAL;
	}
	else {
		ir.positionVisibility = PositionVisibility::NONE;
	}
}

void WiimoteHandler::ParseIRData(std::span<uint8_t> irData)
{
	constexpr static glm::u16vec2 invalidDot = {0x3ffu, 0x3ffu};
	const auto size = irData.size();
	cemu_assert_debug(size == 12 || size == 10);

	if (size == 10)
	{
		Messages::IRBasic irMsg[2];
		std::memcpy(irMsg, irData.data(), sizeof(irMsg));

		for (auto i = 0u; i < 2; ++i)
		{
			const auto data = irMsg[i];
			glm::u16vec2 dot1{data.x1 | ((data.bits & 0b00110000) << 4),
							  data.y1 | ((data.bits & 0b11000000) << 2)};
			glm::u16vec2 dot2{data.x2 | ((data.bits & 0b00000011) << 8),
							  data.y2 | ((data.bits & 0b00001100) << 6)};

			auto& outDot1 = m_state.ir.dots[i * 2];
			auto& outDot2 = m_state.ir.dots[i * 2 + 1];

			outDot1.visible = dot1 != invalidDot;
			outDot1.size = 0;
			if (outDot1.visible)
				outDot1.pos = glm::vec2(1.0f - (dot1.x / 1023.0f), dot1.y / 767.0f);
			else
				outDot1.pos = {};
			m_state.ir.anyVisible |= outDot1.visible;

			outDot2.visible = dot2 != invalidDot;
			outDot2.size = 0;
			if (outDot2.visible)
				outDot2.pos = glm::vec2(1.0f - (dot2.x / 1023.0f), dot2.y / 767.0f);
			else
				outDot2.pos = {};
			m_state.ir.anyVisible |= outDot2.visible;

		}
	}
	else if (size == 12)
	{
		Messages::IRExtended irMsg[4];
		std::memcpy(irMsg, irData.data(), sizeof(irMsg));
		for (auto i = 0u; i < 4; ++i)
		{
			const auto data = irMsg[i];
			auto& outDot = m_state.ir.dots[i];

			glm::u16vec2 rawDot{
				data.x | (data.bits & 0b00110000) << 4,
				data.y | (data.bits & 0b11000000) << 2,
			};
			outDot.visible = rawDot != invalidDot;
			m_state.ir.anyVisible |= outDot.visible;

			outDot.size = data.bits & 0b00001111;
		}
	}
	RotateIR(m_state.ir, m_state.roll);
	CalculateIRPos(m_state.ir);

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
