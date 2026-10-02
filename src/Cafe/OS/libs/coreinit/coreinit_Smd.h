#pragma once
#include "Common/CafeString.h"

namespace coreinit::smd
{
	enum class SmdError : sint32
	{
		Ok = 0,
		Uninitialized = -786447,
		InvalidState = -786446,
		IopInvalidState = -786445,
		InvalidArgument = -786439,
		QueueEmpty = -786437,
		QueueFull = -786436,
		InvalidSmd = -786435,
		InvalidVectors = -786434,
		MsgTooLarge = -786433,
		InvalidElemType = -786432,
	};

	enum class SmdLockType : uint32
	{
		Mutex = 0,
		DisableInterrupts = 1,
		None = 2
	};

	enum class SmdPpcState : uint32 {
		Invalid = 0,
		Initialized = 1,
		Closed = 2,
		Opened = 3
	};

	enum class SmdInterfaceState : uint32 {
		Initialized = 0x1111,
		Opened = 0x2222,
		Closed = 0x3333
	};

	enum class SmdElementType : uint32
	{
		Bytes = 0,
		VectorSpec = 1,
		Vector = 2
	};

	struct SmdVectorSpec
	{
		MEMPTR<uint8[]> ptr;
		uint32be size;
	};
	static_assert(sizeof(SmdVectorSpec) == 8);

	struct SmdVector
	{
		uint32be command;
		sint32be count;
		SmdVectorSpec vecs[4];
	};
	static_assert(sizeof(SmdVector) == 0x28);


	struct SmdElement
	{
		betype<SmdElementType> type;
		uint32be size;
		union
		{
			uint8 data[0x80];
			SmdVector spec;
			uint32be physAddr;
		} u;
		uint8 padding[120];
	};
	static_assert(sizeof(SmdElement) == 0x100);


	struct SmdInterface
	{
		betype<SmdInterfaceState> state;
		uint8 padding0[0x7C];
		uint32be capacity;
		uint8 padding1[0x7C];
		sint32be readIndex;
		uint8 padding2[0x7c];
		sint32be writeIndex;
		uint8 padding3[0x7c];
		uint32be bufPhysAddr;
		uint8 padding4[0x7c];
	};
	static_assert(sizeof(SmdInterface) == 0x280);

	struct SmdCtrlTable
	{
		CafeString<0x10> name;
		uint32be initCount;
		uint8 padding0[0x6C];
		SmdInterface ppcToIopInterface;
		uint8 padding1[0x40];
		SmdInterface iopToPpcInterface;
		uint8 padding2[0x40];
	};
	static_assert(sizeof(SmdCtrlTable) == 0x600);

	struct SmdPpcCtrlTableVectors
	{
		MEMPTR<SmdCtrlTable> ctrlTable;
		uint32be ctrlTableSize;
		MEMPTR<SmdElement> writeBuf;
		uint32be writeBufSize;
		MEMPTR<SmdElement> readBuf;
		uint32be readBufSize;
	};
	static_assert(sizeof(SmdPpcCtrlTableVectors) == 0x18);

	SmdError smdInterfaceReceive(SmdInterface& interface, const SmdElement* elementBuffer, SmdElement& received);
	SmdError smdInterfaceSend(SmdInterface& iface, SmdElement* buffer, const SmdElement& element);

	void Initialize();
};

