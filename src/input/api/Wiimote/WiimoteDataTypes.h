#pragma once
// https://wiibrew.org/wiki/Wiimote


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

enum class InputReportId : uint8
{
	None = 0,
	Status = 0x20,
	ReadMemory = 0x21,
	Acknowledge = 0x22,

	Core = 0x30,
	CoreAcc = 0x31,
	CoreExt8 = 0x32,
	CoreAccIR12 = 0x33,
	CoreExt19 = 0x34,
	CoreAccExt16 = 0x35,
	CoreIR10Ext9 = 0x36,
	CoreAccIR10Ext6 = 0x37,
	Ext22 = 0x3d
};
enum class OutputReportId : uint8
{
	Rumble = 0x10,
	LED = 0x11,
	Report = 0x12,
	IRPixelClockEnable = 0x13,
	SpeakerEnable = 0x14,
	Status = 0x15,
	WriteMemory = 0x16,
	ReadMemory = 0x17,
	SpeakerData = 0x18,
	SpeakerMute = 0x19,
	IREnable = 0x1a
};

#pragma pack(push, 1)

namespace Messages
{
	struct Acc
	{
	  private:
		uint8 unused0 : 5 = 0;

	  public:
		uint8 xLow : 2;

	  private:
		uint8 unused1 : 6;

	  public:
		uint8 yLow : 1;
		uint8 zLow : 1;
		uint8 x;
		uint8 y;
		uint8 z;
	};

	static_assert(sizeof(Acc) == 5);

	struct Status
	{
		bool batteryLow : 1;
		bool extensionConnected : 1;
		bool speakerEnabled : 1;
		bool irEnabled : 1;
		uint8 ledState : 4;
		uint16 _zero;
		uint8 batteryLevel;
	};
	static_assert(sizeof(Status) == 4);

	struct MemoryRead
	{
		uint8 error : 4;
		uint8 size : 4;
		uint16 addressBe;
		uint8 data[16];
	};
	static_assert(sizeof(MemoryRead) == 19);

	struct Acknowledgement
	{
		OutputReportId report;
		uint8 result;
	};
	static_assert(sizeof(Acknowledgement) == 2);

	struct Nunchuk
	{
		uint8 stickX;
		uint8 stickY;
		uint8 accX;
		uint8 accY;
		uint8 accZ;
		struct
		{
			bool z : 1;
			bool c : 1;
			uint8 accX : 2;
			uint8 accY : 2;
			uint8 accZ : 2;
		} bits;
	};
	static_assert(sizeof(Nunchuk) == 6);

	struct Classic9B
	{
		uint8 lx;
		uint8 rx;
		uint8 ly;
		uint8 ry;
		struct
		{
			uint8 lx : 2;
			uint8 rx : 2;
			uint8 ly : 2;
			uint8 ry : 2;
		} bits;
		uint8 lt;
		uint8 rt;
		uint16 buttons;
	};
	static_assert(sizeof(Classic9B) == 9);

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
		uint8 x2;
		uint8 y2;
	};
	static_assert(sizeof(BasicIR) == 5);

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
	};
	static_assert(sizeof(ExtendedIR) == 3);

	struct Mpls
	{
		uint8 yawLow;
		uint8 rollLow;
		uint8 pitchLow;
		bool pitchSlowMode : 1;
		bool yawSlowMode : 1;
		uint8 yawHigh : 6;
		bool extensionConnected : 1;
		bool rollSlowMode : 1;
		uint8 rollHigh : 6;
		uint8 _zero : 1;
		bool containsMpls : 1;
		uint8 pitchHigh : 6;
	};
	static_assert(sizeof(Mpls) == 6);

	struct MplsNunchuk
	{
		uint8 stickX;
		uint8 stickY;
		uint8 accX;
		uint8 accY;
		bool extensionConnected : 1;
		uint8 accZ : 7;
		struct
		{
			uint8 _zero : 1;
			bool isMpls : 1;
			bool z : 1;
			bool c : 1;
			uint8 accX : 1;
			uint8 accY : 1;
			uint8 accZ : 2;
		} bits;
	};
	static_assert(sizeof(MplsNunchuk) == 6);
	struct MplsClassic
	{
		bool dpadUp : 1;
		uint8 lx : 5;
		uint8 rxHigh : 2;
		bool dpadLeft : 1;
		uint8 ly : 5;
		uint8 rxMid : 2;
		uint8 ry : 5;
		uint8 ltHigh : 2;
		uint8 rxLow : 1;
		uint8 rt : 5;
		uint8 lt : 3;
		uint16 buttons; // Also contains some other data
	};

}
namespace Requests
{
	class ReqBase {};
	template<OutputReportId rep>
	class Req : public ReqBase {
	  private:
		OutputReportId const report = rep;
	};

	struct LED : public Req<OutputReportId::LED>
	{
	  private:
		uint8 unused : 4 = 0;

	  public:
		uint8 led : 4;
	};
	static_assert(sizeof(LED) == 2);

	struct Report : public Req<OutputReportId::Report>
	{
	  private:
		uint8 unused0 : 2 = 0;

	  public:
		bool continuous : 1 = 0;

	  private:
		uint8 unused1 : 2 = 0;

	  public:
		InputReportId report;
	};
	static_assert(sizeof(Report) == 3);

	struct Status : public Req<OutputReportId::Status>
	{
	  private:
		uint8 unused = 0;
	};
	static_assert(sizeof(Status) == 2);

	struct ReadMemory : public Req<OutputReportId::ReadMemory>
	{
		uint32be address = 0;
		uint16be size = 0;
	};
	static_assert(sizeof(ReadMemory) == 7);

	struct WriteMemory : public Req<OutputReportId::WriteMemory>
	{
		uint32be address = 0;
		uint8 size = 0;
		uint8 data[16] = {0};
	};
	static_assert(sizeof(WriteMemory) == 22);

	struct IRPixelClock : public Req<OutputReportId::IRPixelClockEnable>
	{
	  private:
		uint8 unused0 : 2 = 0;

	  public:
		bool enable : 1 = 0;

	  private:
		uint8 unused1 : 5 = 0;
	};
	static_assert(sizeof(IRPixelClock) == 2);

	struct IREnable : public Req<OutputReportId::IREnable>
	{
	  private:
		uint8 unused0 : 2 = 0;

	  public:
		bool enable : 1 = 0;

	  private:
		uint8 unused1 : 5 = 0;
	};
	static_assert(sizeof(IREnable) == 2);
	struct Rumble : public Req<OutputReportId::Rumble>
	{
		bool enable;
	};
	static_assert(sizeof(Rumble) == 2);
}
namespace Memory
{
	struct AccCalibBlock
	{
		uint8 x;
		uint8 y;
		uint8 z;
		struct
		{
			uint8 z : 2;
			uint8 y : 2;
			uint8 x : 2;

		  private:
			uint8 unused : 2;
		} bits;
	};
	static_assert(sizeof(AccCalibBlock) == 4);

	struct WiimoteCalibration
	{
		AccCalibBlock zero;
		AccCalibBlock scale;
		uint8 volume : 7;
		bool motor : 1;
		uint8 checksum;
	};
	static_assert(sizeof(WiimoteCalibration) == 10);

	struct NunchukCalibration
	{
		struct Bounds
		{
			uint8 max;
			uint8 min;
			uint8 center;
		};
		AccCalibBlock zero;
		AccCalibBlock scale;
		Bounds x;
		Bounds y;
	};
	static_assert(sizeof(NunchukCalibration) == 14);

	struct MplsCalibration
	{
		uint16be yawZero;
		uint16be rollZero;
		uint16be pitchZero;
		uint16be yawScale;
		uint16be rollScale;
		uint16be pitchScale;
		uint8 degreesDiv6;
		uint8 uid;
		uint16be crc;
	};
	static_assert(sizeof(MplsCalibration) == 16);
}
#pragma pack(pop)
