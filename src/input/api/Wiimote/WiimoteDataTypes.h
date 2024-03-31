#pragma once

// https://wiibrew.org/wiki/Wiimote

enum OutputReportId : uint8
{
	kLED = 0x11,
	kType = 0x12,
	kIR = 0x13,
	kSpeakerState = 0x14,
	kStatusRequest = 0x15,
	kWriteMemory = 0x16,
	kReadMemory = 0x17,
	kSpeakerData = 0x18,
	kSpeakerMute = 0x19,
	kIR2 = 0x1A,
};

enum InputReportId : uint8
{
	kNone = 0,

	kStatus = 0x20,
	kRead = 0x21,
	kAcknowledge = 0x22,

	kDataCore = 0x30,
	kDataCoreAcc = 0x31,
	kDataCoreExt8 = 0x32,
	kDataCoreAccIR = 0x33,
	kDataCoreExt19 = 0x34,
	kDataCoreAccExt = 0x35,
	kDataCoreIRExt = 0x36,
	kDataCoreAccIRExt = 0x37,
	kDataExt = 0x3d,
};

enum RegisterAddress : uint32
{
	kRegisterCalibration = 0x16,
	kRegisterCalibration2 = 0x20, // backup calibration data

	kRegisterIR = 0x4b00030,
	kRegisterIRSensitivity1 = 0x4b00000,
	kRegisterIRSensitivity2 = 0x4b0001a,
	kRegisterIRMode = 0x4b00033,

	kRegisterExtensionEncrypted = 0x4a40040,

	kRegisterExtension1 = 0x4a400f0,
	kRegisterExtension2 = 0x4a400fb,
	kRegisterExtensionType = 0x4a400fa,
	kRegisterExtensionCalibration = 0x4a40020,

	kRegisterMotionPlusDetect = 0x4a600fa,
	kRegisterMotionPlusInit = 0x4a600f0,
	kRegisterMotionPlusEnable = 0x4a600fe,
};


enum MemoryType : uint8
{
	kEEPROMMemory = 0,
	kRegisterMemory = 0x4,
};

enum StatusBitmask : uint8
{
	kBatteryEmpty = 0x1,
	kExtensionConnected = 0x2,
	kSpeakerEnabled = 0x4,
	kIREnabled = 0x8,
	kLed1 = 0x10,
	kLed2 = 0x20,
	kLed3 = 0x40,
	kLed4 = 0x80
};

struct BasicIR
{
	uint8 x1;
	uint8 y1;

	struct
	{
		uint8 x2 : 2;
		uint8 y2 : 2;
		uint8 x1 : 2;
		uint8 y1 : 2;
	} bits;
	static_assert(sizeof(bits) == 1);

	uint8 x2;
	uint8 y2;
};
struct ExtendedIR
{
	uint8 x;
	uint8 y;
	struct
	{
		uint8 size : 4;
		uint8 x : 2;
		uint8 y : 2;
	} bits;
	static_assert(sizeof(bits) == 1);
};

static_assert(sizeof(BasicIR) == 5);

static_assert(sizeof(ExtendedIR) == 3);

