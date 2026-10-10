#include "iosu_smd.h"

#include "IOSU/kernel/iosu_kernel.h"

namespace iosu::smd
{
	static std::array<SmdIop, 8> s_smdIopArray = {};


	SmdError smdIopInit(uint32 index, SmdCtrlTable& table, const char* name)
	{
		if (index > 7)
			return SmdError::InvalidSmd;
		auto& smd = s_smdIopArray[index];
		smd.ctrlTable = &table;
		if (std::strncmp(smd.ctrlTable->name.c_str(), name, smd.ctrlTable->name.Size()) != 0)
			return SmdError::InvalidArgument;
		smd.iopBuf = reinterpret_cast<SmdElement*>(memory_getPointerFromPhysicalOffset(table.iopToPpcInterface.bufPhysAddr));
		smd.ppcBuf = reinterpret_cast<SmdElement*>(memory_getPointerFromPhysicalOffset(table.ppcToIopInterface.bufPhysAddr));
		return SmdError::Ok;
	}

	SmdError smdIopOpen(uint32 index)
	{
		if (index > 7)
			return SmdError::InvalidSmd;
		auto& smd = s_smdIopArray[index];
		if (!match_any_of(smd.state, SmdIopState::Initialized, SmdIopState::Opened, SmdIopState::Closed))
			return SmdError::IopInvalidState;
		smd.state = SmdIopState::Opened;
		smd.ctrlTable->iopToPpcInterface.state = SmdInterfaceState::Opened;
		return SmdError::Ok;
	}

	SmdError smdIopClose(uint32 index)
	{
		if (index > 7)
			return SmdError::InvalidSmd;
		auto& smdIop = s_smdIopArray[index];
		if (!match_any_of(smdIop.state, SmdIopState::Initialized, SmdIopState::Opened, SmdIopState::Closed))
			return SmdError::IopInvalidState;
		smdIop.state = SmdIopState::Closed;
		smdIop.ctrlTable->iopToPpcInterface.state = SmdInterfaceState::Closed;
		return SmdError::Ok;
	}

	SmdError smdIopReceive(uint32 index, SmdElement& elem)
	{
		if (index > 7)
			return SmdError::InvalidSmd;
		auto& smdIop = s_smdIopArray[index];
		auto& iface = smdIop.ctrlTable->ppcToIopInterface;
		cemu_assert_debug(smdIop.state == SmdIopState::Opened);
		cemu_assert_debug(iface.state == SmdInterfaceState::Opened);

		return coreinit::smd::smdInterfaceReceive(iface, smdIop.iopBuf, elem);
	}

	SmdError smdIopWriteMessage(uint32 index, std::span<const uint8> msg)
	{
		if (index > 7)
			return SmdError::InvalidSmd;
		if (msg.size() > 0x80)
			return SmdError::MsgTooLarge;

		auto& smdIop = s_smdIopArray[index];
		auto& iface = smdIop.ctrlTable->iopToPpcInterface;
		cemu_assert_debug(smdIop.state == SmdIopState::Opened);
		cemu_assert_debug(iface.state == SmdInterfaceState::Opened);

		SmdElement elem{};
		elem.type = SmdElementType::Bytes;
		elem.size = msg.size();
		std::memcpy(elem.u.data, msg.data(), msg.size());

		return coreinit::smd::smdInterfaceSend(iface, smdIop.ppcBuf, elem);
	}
} // namespace iosu::smd
