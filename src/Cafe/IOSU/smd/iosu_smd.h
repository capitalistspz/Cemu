#pragma once
#include "Cafe/OS/libs/coreinit/coreinit_Smd.h"

namespace iosu::smd
{
	using coreinit::smd::SmdError;
	using coreinit::smd::SmdElement;
	using coreinit::smd::SmdElementType;
	using coreinit::smd::SmdCtrlTable;
	using coreinit::smd::SmdInterfaceState;

	enum class SmdIopState
	{
		Initialized = 2,
		Closed = 3,
		Opened = 4,
	};

	struct SmdIop
	{
		std::mutex mutex;
		SmdCtrlTable* ctrlTable;
		SmdElement* iopBuf;
		SmdElement* ppcBuf;
		SmdIopState state;
	};

	SmdError smdIopInit(uint32 index, SmdCtrlTable& table, const char* name);
	SmdError smdIopOpen(uint32 index);
	SmdError smdIopClose(uint32 index);
	SmdError smdIopReceive(uint32 index, SmdElement& element);
	SmdError smdIopWriteMessage(uint32 index, std::span<const uint8> msg);
}