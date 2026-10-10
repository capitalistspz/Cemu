#pragma once
#include "IOSU/iosu_types_common.h"

namespace iosu::usb_btrm {
	enum class StateFlags : uint32
	{
		Initialized = 1 << 0,
		FlagActive = 1 << 1,
		GotStatus = 1 << 2,
		WudInitialized = 1 << 4,
		MadeVisible = 1 << 5,
		GotDevInfo = 1 << 6
	};

	enum class Cmd : uint8
	{
		Status = 1,
		Visible = 2,
		WUD = 3,
		BTE = 4,
		QueueEvt = 5
	};

	enum class InternalMsg
	{
		Unknown0x17 = 0x17,
		ReceiveFromSmdPpc = 0x1e,
		Shutdown = 0x1f,
		Disconnect = 0x20,
	};

	constexpr inline uint32 operator+(InternalMsg msg)
	{
		return static_cast<uint32>(msg);
	}

	enum class IoctlvId
	{
		ExecuteFunc = 0x00
	};

	enum class IoctlId
	{
		InitSmd = 0x01,
		CloseSmd = 0x02,
		RemoveFromQueue = 0x03,
		IsolateChannels = 0x04,
		SysUpdate = 0x05,
		InitOHCI = 0x06,
		ReportProcData = 0x07,
		DevInfo = 0x08,
		Disconnect = 0x09,
		SetBtCfg = 0x0A
	};

	enum class AsyncEvt
	{
		Dequeued = -1,
		HID = 0,
		RegisterDevNum = 1,
		WUDBusy = 2,
		Connectable = 3,
		Discoverable = 4,
		ClearDevice = 5,
		SyncDevice = 6,
		RegisteredWbc = 7,
		LinkNums = 8,
		RSSI = 9,
		SerialFlashTestUpdate = 11,
		SerialFlashTestMode = 12,
		SerialFlashTestWrite = 13,
		SerialFlashTestRead = 14,
		ReportProcData = 15,
		ScArray = 16
	};

	enum class WudFunc
	{
		StartSync = 0x04,
		StartSyncAlt = 0x05,
		PurgeDevInfo = 0x06,
		SetSyncLoop = 0x07,
		CancelSync = 0x08,
		DisableChannel = 0x09,
		StartClearDevices = 0x0A,
		SetVisibility = 0x0B,
		SetSniffMode = 0x0C,
		RegisterNewDev = 0x0D,
		EnableClearDeviceResponse = 0x0E,
		EnableSyncResponse = 0x0F,
		EnableWBC = 0x10,
		SerialFlashTestUpdate = 0x11,
		SerialFlashTestMode = 0x12,
		SerialFlashTestWrite = 0x13,
		SerialFlashTestRead = 0x14,
		ReportProcData = 0x16,
		StartSyncEx = 0x17,
		StoreScArray = 0x19,
		ClearScArray = 0x20,
		DisbaleSyncResponse = 0x1B,
		Unk0x1C = 0x1C,
		Noop = 0x1D,
		DisableWBC = 0x1E,
	};

	enum class BTEFunc
	{
		Disconnect = 0x00,
		HidWrite = 0x01,
		CloseACL = 0x02,
		Unk0x03 = 0x03
	};

	struct Request
	{
		uint8 requestData[0x1000];
		Cmd cmd;
		uint8 func;
		uint8 unknown0x1002[2];
		uint32 unknown0x1004;
	};
	static_assert(offsetof(Request, unknown0x1002) == 0x1002);
	static_assert(offsetof(Request, unknown0x1004) == 0x1004);

	struct Response
	{
		uint8 resultData[0x1000];
		sint8 responseId;
		sint8 unknown0x1001[3];
		sint32be unknown0x1004;
		sint32be unknown0x1008;
	};
	static_assert(offsetof(Response, unknown0x1001) == 0x1001);
	static_assert(offsetof(Response, unknown0x1004) == 0x1004);
	static_assert(offsetof(Response, unknown0x1008) == 0x1008);

	using BTAddr = std::array<uint8, 6>;

	struct BtCfg
	{
		uint32be wbcEnabled;
		uint32be urccEnabled;
		uint32be wiimoteEnabled;
		uint8 maxControllers;
		uint8 padding[3];
	};
	static_assert(sizeof(BtCfg) == 0x10);

	struct ScElem
	{
		BTAddr addr;
		char name[59];
		// These bytes are set after a long sequence of 0s
		uint8 unk[5];
	};
	static_assert(sizeof(ScElem) == 0x46);

	struct ScArray
	{
		uint8 num;
		ScElem elems[0x10];
	};
	static_assert(sizeof(ScArray) == 0x461);


#pragma pack(push, 1)
	struct BtrmStatus
	{
		StateFlags flags;
		BTAddr maybeHostBdaddr;
		uint8_t unk0;
		uint16_t wudFwVersion;
		uint16_t wudSerialFlashVersion;
		uint8_t blcRegistered;
	};
#pragma pack(pop)

	IOSUModule* GetModule();
}

ENABLE_BITMASK_OPERATORS(iosu::usb_btrm::StateFlags);