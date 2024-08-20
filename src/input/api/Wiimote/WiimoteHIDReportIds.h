#pragma once


enum class RequestReportId : uint8
{
	Rumble = 0x10,
	LED = 0x11,
	ReportMode = 0x12,
	IR1 = 0x13,
	SpeakerEnable = 0x14,
	Status = 0x15,
	Write = 0x16,
	Read = 0x17,
	SpeakerData = 0x18,
	SpeakerMute = 0x19,
	IR2 = 0x1A
};

enum class ResponseReportId : uint8
{
	Status = 0x20,
	Read = 0x21,
	Acknowledge = 0x22,

	DataCore = 0x30,
	DataCoreAcc = 0x31,
	DataCoreExt8 = 0x32,
	DataCoreAccIR12 = 0x33,
	DataCoreExt19 = 0x34,
	DataCoreAccExt16 = 0x35,
	DataCoreIR10Ext9 = 0x36,
	DataCoreAccIR10Ext6 = 0x37,
	DataExt21 = 0x3d
};