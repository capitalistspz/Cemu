#include "coreinit_Smd.h"

#include "coreinit_Memory.h"
#include "coreinit_Thread.h"
#include <OS/common/OSUtil.h>

#include <Common/CafeString.h>

namespace coreinit::smd
{

	struct SmdPpc
	{
		MEMPTR<SmdPpc> self;
		MEMPTR<SmdCtrlTable> ctrlTable;
		SmdLockType lockType;
		union
		{
			uint32 interrupts;
			OSMutex mutex;
		} lock;
		uint32be messageCount;
		MEMPTR<SmdElement> writeBuf;
		MEMPTR<SmdElement> readBuf;
		SmdPpcState state;
		uint8 padding[0x38];
	};
	static_assert(sizeof(SmdPpc) == 0x80);


	class SmdLockGuard
	{
		SmdPpc* m_smd;
	public:
		explicit SmdLockGuard(SmdPpc* smd)
			: m_smd(smd)
		{
			if (smd->lockType == SmdLockType::Mutex)
				OSLockMutex(&smd->lock.mutex);
			else if (smd->lockType == SmdLockType::DisableInterrupts)
				smd->lock.interrupts = OSDisableInterrupts();
		}
		~SmdLockGuard()
		{
			if (m_smd->lockType == SmdLockType::Mutex)
				OSUnlockMutex(&m_smd->lock.mutex);
			else if (m_smd->lockType == SmdLockType::DisableInterrupts)
				OSRestoreInterrupts(m_smd->lock.interrupts);
		}
	};

	namespace internal
	{
		static uint32 smdAvailableWrites(SmdInterface& iface);
		static uint32 smdAvailableReads(SmdInterface& iface);
	}


	SmdPpc* smdPpcInit(MEMPTR<uint8> buffer, uint32 bufferSize, uint32 msgCount, const char* name, SmdLockType lockType)
	{
		if (!buffer || msgCount == 0 || !name)
			return nullptr;
		const auto actualMsgCount = msgCount + 1;

		const auto alignedBuffer = MEMPTR<uint8>((buffer + 3).GetMPTR() & 0xFFFF'FFFC);
		auto ctrlTable = MEMPTR<SmdCtrlTable>((alignedBuffer.GetMPTR() + 0x87) & 0xFFFF'FFC0);
		const auto smdPpc = reinterpret_cast<SmdPpc*>(alignedBuffer.GetPtr());
		const auto msgArea = MEMPTR<SmdElement>(ctrlTable + 1);
		const auto readBuf = msgArea + actualMsgCount;

		if ((readBuf.GetMPTR() + actualMsgCount) > (buffer + static_cast<sint32>(bufferSize)).GetMPTR())
			return nullptr;

		if (smdPpc->self == smdPpc)
			ctrlTable->initCount = 1;
		else
			ctrlTable->initCount += 1;

		smdPpc->messageCount = actualMsgCount;
		smdPpc->readBuf = readBuf;
		smdPpc->state = SmdPpcState::Initialized;
		smdPpc->ctrlTable = ctrlTable;
		smdPpc->writeBuf = msgArea;
		smdPpc->self = smdPpc;
		smdPpc->lockType = lockType;

		if (smdPpc->lockType == SmdLockType::Mutex)
			new (&smdPpc->lock.mutex) OSMutex;
		else
			new (&smdPpc->lock.interrupts) uint32;

		ctrlTable->name.assign(name);

		SmdInterface& iopIface = ctrlTable->ppcToIopInterface;
		iopIface.readIndex = 0;
		iopIface.writeIndex = 0;
		iopIface.state = SmdInterfaceState::Initialized;
		iopIface.capacity = actualMsgCount;
		iopIface.bufPhysAddr = OSEffectiveToPhysical(smdPpc->writeBuf.GetMPTR());

		SmdInterface& ppcIface = ctrlTable->iopToPpcInterface;
		ppcIface.readIndex = 0;
		ppcIface.writeIndex = 0;
		ppcIface.state = SmdInterfaceState::Initialized;
		ppcIface.capacity = actualMsgCount;
		ppcIface.bufPhysAddr = OSEffectiveToPhysical(smdPpc->readBuf.GetMPTR());

		return smdPpc;
	}

	SmdError smdPpcOpen(SmdPpc* smd)
	{
		if (smd->self != smd)
			return SmdError::InvalidSmd;
		auto l = SmdLockGuard(smd);
		if (smd->state < SmdPpcState::Invalid)
			return SmdError::Uninitialized;
		smd->ctrlTable->iopToPpcInterface.state = SmdInterfaceState::Opened;
		std::atomic_thread_fence(std::memory_order::release);

		smd->state = SmdPpcState::Opened;
		return SmdError::Ok;
	}

	SmdError smdPpcClose(SmdPpc* smd)
	{
		if (smd->self != smd)
			return SmdError::InvalidSmd;
		auto l = SmdLockGuard(smd);
		if (smd->state != SmdPpcState::Opened)
			return SmdError::InvalidState;
		stdx::atomic_ref(smd->ctrlTable->iopToPpcInterface.state).store(SmdInterfaceState::Closed, std::memory_order::release);
		std::atomic_thread_fence(std::memory_order::release);

		smd->state = SmdPpcState::Closed;
		return SmdError::Ok;
	}

	SmdError smdPpcSendMessage(SmdPpc* smd, const uint8* buffer, uint32 bufferSize)
	{
		if (smd->self != smd)
			return SmdError::InvalidSmd;
		auto l = SmdLockGuard(smd);
		if (smd->state != SmdPpcState::Opened)
			return SmdError::InvalidState;
		if (bufferSize > 0x80)
			return SmdError::MsgTooLarge;
		auto elem = SmdElement{};
		elem.type = SmdElementType::Bytes;
		elem.size = bufferSize;
		std::memcpy(&elem.u.data, buffer, bufferSize);
		return smdInterfaceSend(smd->ctrlTable->ppcToIopInterface, smd->writeBuf, elem);
	}

	uint32 internal::smdAvailableWrites(SmdInterface& iface)
	{
		const auto writeIndex = stdx::atomic_ref(iface.writeIndex).load(std::memory_order::acquire);
		const auto readIndex = stdx::atomic_ref(iface.readIndex).load(std::memory_order::acquire);
		const auto diff = readIndex - writeIndex;
		if (diff >= 0)
			return diff;
		if (readIndex < 0)
			return iface.capacity - writeIndex;
		return iface.capacity - writeIndex + readIndex;
	}

	uint32 internal::smdAvailableReads(SmdInterface& iface)
	{
		const auto readIndex = stdx::atomic_ref(iface.readIndex).load(std::memory_order::acquire);
		const auto writeIndex = stdx::atomic_ref(iface.writeIndex).load(std::memory_order::acquire);
		const auto diff = writeIndex - readIndex;
		if (diff >= 0)
			return diff;
		if (writeIndex < 0)
			return iface.capacity - readIndex;
		return iface.capacity - readIndex + writeIndex;
	}

	SmdError smdInterfaceReceive(SmdInterface& iface, const SmdElement* buffer, SmdElement& received)
	{
		if (internal::smdAvailableReads(iface) < 1)
			return SmdError::QueueEmpty;

		const auto readIndexRef = stdx::atomic_ref(iface.readIndex);

		const auto currentIndex = readIndexRef.load(std::memory_order::acquire);
		const auto currentElem = buffer + currentIndex;

		const auto newIndex = (currentIndex + 1) % static_cast<sint32>(iface.capacity);

		switch (currentElem->type)
		{
		case SmdElementType::Bytes:
		{
			received = *currentElem;
			break;
		}
		case SmdElementType::VectorSpec:
		case SmdElementType::Vector:
		{
			cemuLog_log(LogType::UnsupportedAPI, "smdPpcReceive: Element types are not yet handled");
			cemu_assert_unimplemented();
			break;
		}
		default:
			return SmdError::InvalidElemType;
		}

		readIndexRef.store(newIndex, std::memory_order::release);
		return SmdError::Ok;
	}

	SmdError smdInterfaceSend(SmdInterface& iface, SmdElement* buffer, const SmdElement& element)
	{
		if (internal::smdAvailableWrites(iface) < 2)
			return SmdError::QueueFull;
		const auto writeIndexRef = stdx::atomic_ref(iface.writeIndex);

		const auto currentIndex = writeIndexRef.load(std::memory_order::acquire);
		SmdElement* newElem = buffer + currentIndex;
		*newElem = element;

		const auto newIndex = (currentIndex + 1) % static_cast<sint32>(iface.capacity);
		writeIndexRef.store(newIndex, std::memory_order::release);
		return SmdError::Ok;
	}

	SmdError smdPpcReceive(SmdPpc* smdPpc, SmdElement* received)
	{
		if (!smdPpc || smdPpc->self != smdPpc)
			return SmdError::InvalidSmd;
		auto l = SmdLockGuard(smdPpc);
		if (smdPpc->state != SmdPpcState::Opened)
			return SmdError::InvalidState;
		SmdInterface& iface = smdPpc->ctrlTable->iopToPpcInterface;
		return smdInterfaceReceive(iface, smdPpc->readBuf, *received);
	}

	SmdError smdPpcGetCtrlTableVectors(SmdPpc *smd, SmdPpcCtrlTableVectors *ctrlTableVecs)
	{
		if (smd->self != smd)
			return SmdError::InvalidSmd;
		if (!match_any_of(smd->state, SmdPpcState::Initialized, SmdPpcState::Opened, SmdPpcState::Closed))
			return SmdError::InvalidState;
		ctrlTableVecs->ctrlTable = smd->ctrlTable;
		ctrlTableVecs->ctrlTableSize = sizeof(SmdCtrlTable);

		ctrlTableVecs->readBuf = smd->readBuf;
		ctrlTableVecs->readBufSize = smd->messageCount * sizeof(SmdElement);
		ctrlTableVecs->writeBuf = smd->writeBuf;
		ctrlTableVecs->writeBufSize = smd->messageCount * sizeof(SmdElement);
		return SmdError::Ok;
	}

	void Initialize()
	{
		cafeExportRegister("coreinit", smdPpcInit, LogType::Force);
		cafeExportRegister("coreinit", smdPpcOpen, LogType::Force);
		cafeExportRegister("coreinit", smdPpcClose, LogType::Force);
		cafeExportRegister("coreinit", smdPpcSendMessage, LogType::Force);
		cafeExportRegister("coreinit", smdPpcReceive, LogType::Force);
		cafeExportRegister("coreinit", smdPpcGetCtrlTableVectors, LogType::Force);
	}
}