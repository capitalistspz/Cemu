#pragma once
#include "WiimoteHIDReportIds.h"

#pragma pack(push, 1)

namespace WiimoteMsg
{
	enum StatusFlags : uint8
	{
		STATUS_FLAG_LOW_BATTERY = 1 << 0,
		STATUS_FLAG_EXTENSION = 1 << 1,
		STATUS_FLAG_SPEAKER = 1 << 2,
		STATUS_FLAG_IR = 1 << 3,
		STATUS_FLAGS_LED = 0xF0
	};

	struct RequestRead
	{
		const RequestReportId rep = RequestReportId::Read;
		uint32be address{};
		uint16be size{};
	};
	static_assert(sizeof(RequestRead) == 7);

	struct RequestWrite
	{
		const RequestReportId rep = RequestReportId::Write;
		uint32be address{};
		uint8 size{};
		uint8 data[16];
	};
	static_assert(sizeof(RequestWrite) == 22);

	struct RequestLED
	{
		const RequestReportId rep = RequestReportId::LED;
		// Upper 4 bits hold led data
		uint8 flags{};
	};
	static_assert(sizeof(RequestLED) == 2);

	struct RequestStatus
	{
		const RequestReportId req = RequestReportId::Status;
		const uint8 unused{};
	};
	static_assert(sizeof(RequestStatus) == 2);

	struct RequestReportMode
	{
		const RequestReportId req = RequestReportId::ReportMode;
		uint8 flags{};
		ResponseReportId report;
	};
	static_assert(sizeof(RequestReportMode) == 3);

}

#pragma pack(pop)