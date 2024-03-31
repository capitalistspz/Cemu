#pragma once

enum WiimoteButtons
{
	kWiimoteButton_Left = 0,
	kWiimoteButton_Right = 1,
	kWiimoteButton_Down = 2,
	kWiimoteButton_Up = 3,
	kWiimoteButton_Plus = 4,

	kWiimoteButton_Two = 8,
	kWiimoteButton_One = 9,
	kWiimoteButton_B = 10,
	kWiimoteButton_A = 11,
	kWiimoteButton_Minus = 12,

	kWiimoteButton_Home = 15,

	// self defined
	kWiimoteButton_C = 16,
	kWiimoteButton_Z = 17,

	kHighestWiimote = 20,
};

enum ClassicButtons
{
	kClassicButton_R = 1,
	kClassicButton_Plus = 2,
	kClassicButton_Home = 3,
	kClassicButton_Minus = 4,
	kClassicButton_L = 5,
	kClassicButton_Down = 6,
	kClassicButton_Right = 7,
	kClassicButton_Up = 8,
	kClassicButton_Left = 9,
	kClassicButton_ZR = 10,
	kClassicButton_X = 11,
	kClassicButton_A = 12,
	kClassicButton_Y = 13,
	kClassicButton_B = 14,
	kClassicButton_ZL = 15,
};

enum IRMode : uint8
{
	kIRDisabled,
	kBasicIR = 1,
	kExtendedIR = 3,
	kFullIR = 5,
};


struct IRDot
{
	bool visible = false;
	glm::vec2 pos;
	glm::vec<2, uint16> raw;
	uint32 size;
};

struct IRCamera
{
	IRMode mode = IRMode::kIRDisabled;
	std::array<IRDot, 4> dots{}, prev_dots{};

	glm::vec2 position, m_prev_position;
	glm::vec2 middle;
	float distance;
	std::pair<sint32, sint32> indices{ 0,1 };
};

struct Calibration
{
	glm::vec<3, uint16> zero{ 0x200, 0x200, 0x200 };
	glm::vec<3, uint16> gravity{ 0x240, 0x240, 0x240 };
};


struct NunchukCalibration : Calibration
{
	glm::vec<2, uint8> min{};
	glm::vec<2, uint8> center{ 0x7f, 0x7f };
	glm::vec<2, uint8> max{ 0xff, 0xff };
};

struct MotionPlusData
{
	Calibration calibration{};

	glm::vec3 orientation; // yaw, roll, pitch
	bool slow_roll = false;
	bool slow_pitch = false;
	bool slow_yaw = false;
	bool extension_connected = false;
};

struct NunchukData
{
	glm::vec3 acceleration{}, prev_acceleration{};
	NunchukCalibration calibration{};

	bool c = false;
	bool z = false;

	glm::vec2 axis{};
	glm::vec<2, uint8> raw_axis{};

	MotionSample motion_sample{};
};

struct ClassicData
{
	glm::vec2 left_axis{};
	glm::vec<2, uint8> left_raw_axis{};

	glm::vec2 right_axis{};
	glm::vec<2, uint8> right_raw_axis{};

	glm::vec2 trigger{};
	glm::vec<2, uint8> raw_trigger{};
	uint16 buttons = 0;
};

struct WiimoteState
{
	uint16 buttons = 0;
	uint8 flags = 0;
	uint8 battery_level = 0;

	glm::vec3 m_acceleration{}, m_prev_acceleration{};
	float m_roll = 0;

	std::chrono::high_resolution_clock::time_point m_last_motion_timestamp{};
	MotionSample motion_sample{};
	WiiUMotionHandler motion_handler{};

	bool m_calibrated = false;
	Calibration m_calib_acceleration{};

	IRCamera ir_camera{};

	std::optional<MotionPlusData> m_motion_plus;
	std::variant<std::monostate, NunchukData, ClassicData> m_extension{};
};