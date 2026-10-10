#include "iosu_usb_btrm.h"

#include "IOSU/iosu_types_common.h"
#include "IOSU/kernel/iosu_kernel.h"
#include "IOSU/smd/iosu_smd.h"
#include "OS/libs/coreinit/coreinit_Smd.h"
#include "util/helpers/helpers.h"

#define BTRM_LOG(fmt, ...) cemuLog_logDebug(LogType::Force, "[BTRM] " fmt __VA_OPT__(, ) __VA_ARGS__)

namespace iosu::usb_btrm
{

	constexpr uint32 EXIT_MSG = 0xf00dd00d;
	constexpr char BTRM_PATH[] = "/dev/usb/btrm";
	constexpr char EARLY_BTRM_PATH[] = "/dev/usb/early_btrm";
	constexpr uint32 SMD_HANDLE = 0;

	namespace
	{
		std::thread s_serviceThread;
		IOSMsgQueueId s_btrmRmMsgQueueId;
		IOSMsgQueueId s_btrmTransMsgQueueId;
		SysAllocator<kernel::IOSMessage, 128> s_msgBuffer;
		SysAllocator<kernel::IOSMessage, 128> s_transMsgBuffer;

		struct btrm
		{
			StateFlags stateFlags;
			sint32 disconnectTimer;
			sint32 receiveTimer;
			bool smdInitialized;
		} s_btrm;

		IOS_ERROR HandleIoctl(IPCCommandBody& cmdBody);
		IOS_ERROR HandleWUD(const Request& req, Response& resp);
		IOS_ERROR HandleBTE(const Request& req, Response& resp);

		void RemoveAllFromQueue()
		{
			while (true)
			{
				kernel::IOSMessage msg;
				IOS_ERROR r = kernel::IOS_ReceiveMessage(s_btrmTransMsgQueueId, &msg, 1);
				if (IOS_ResultIsError(r))
					break;
				auto req = MEMPTR<IPCCommandBody>(msg);
				auto fnRes = MEMPTR<Response>(req->args.ioctlv.vecs[1].baseVirt);
				memset(&fnRes->resultData, 0, sizeof(fnRes->resultData));
				fnRes->responseId = -1;
				fnRes->unknown0x1001[0] = -1;
				fnRes->unknown0x1001[1] = -1;
				fnRes->unknown0x1001[2] = 0;
				fnRes->unknown0x1004 = -1;
				fnRes->unknown0x1008 = -1;
				kernel::IOS_ResourceReply(req, IOS_ERROR_OK);
			}
		}

		void ServiceThread()
		{
			using namespace kernel;
			SetThreadName("IOSU-USB-BTRM");
			s_btrmTransMsgQueueId = IOS_CreateMessageQueue(s_transMsgBuffer.GetPtr(), s_transMsgBuffer.GetCount());

			while (true)
			{
				IOSMessage msg;
				IOS_ERROR r = IOS_ReceiveMessage(s_btrmRmMsgQueueId, &msg, 0);
				cemu_assert(!IOS_ResultIsError(r));
				if (msg == EXIT_MSG)
				{
					break;
				}
				switch (static_cast<InternalMsg>(msg))
				{
				case InternalMsg::ReceiveFromSmdPpc:
				{
					continue;
				}
				case InternalMsg::Shutdown:
				{
					BTRM_LOG("TIMER SHUTDOWN");
					continue;
				}
				case InternalMsg::Disconnect:
				{
					BTRM_LOG("TIMER DISCONNECT");
					continue;
				}
				default:
					break;
				}
				IPCCommandBody* cmdBody = MEMPTR<IPCCommandBody>(msg).GetPtr();
				switch (cmdBody->cmdId)
				{
				case IPCCommandId::IOS_OPEN:
				{
					auto openPath = cmdBody->args.open.path.GetPtr();
					sint32 res;
					if (strcmp(BTRM_PATH, openPath) == 0)
						res = 1;
					else if (strcmp(EARLY_BTRM_PATH, openPath) == 0)
						res = 2;
					else
						res = 3;
					BTRM_LOG("OPEN {} -> {}", cmdBody->args.open.path.GetPtr(), res);
					IOS_ResourceReply(cmdBody, static_cast<IOS_ERROR>(res));
					continue;
				}
				case IPCCommandId::IOS_CLOSE:
				{
					BTRM_LOG("CLOSE {}", cmdBody->devHandle);
					IOS_ResourceReply(cmdBody, static_cast<IOS_ERROR>(0));
					s_btrm.smdInitialized = false;
					smd::smdIopClose(SMD_HANDLE);
					IOS_DestroyTimer(s_btrm.receiveTimer);
					//TODO: The rest of the close behaviour
					continue;
				}
				case IPCCommandId::IOS_IOCTL:
				{
					auto res = HandleIoctl(*cmdBody);
					IOS_ResourceReply(cmdBody, static_cast<IOS_ERROR>(res));
					continue;
				}
				case IPCCommandId::IOS_IOCTLV:
				{
					auto& args = cmdBody->args.ioctlv;
					IPCIoctlVector* vecIn = args.vecs;
					IPCIoctlVector* vecOut = args.vecs + 1;
					if (static_cast<IoctlvId>(cmdBody->args.ioctl.requestId.value()) != IoctlvId::ExecuteFunc)
					{
						BTRM_LOG("{}: Received unsupported Ioctlv cmd", BTRM_PATH);
						IOS_ResourceReply(cmdBody, IOS_ERROR_INVALID);
						continue;
					}
					if (args.numIn != 1 || args.numOut != 1 || vecIn->size != sizeof(Request) || vecOut->size != sizeof(Response))
					{
						BTRM_LOG("Invalid IOCTLV vector In: {}, Out: {}, Len0: {}, Len1: {}", args.numIn, args.numOut, vecIn->size, vecOut->size);
						IOS_ResourceReply(cmdBody, IOS_ERROR_INVALID);
						continue;
					}
					auto req = MEMPTR<const Request>(vecIn->baseVirt).GetPtr();
					auto resp = MEMPTR<Response>(vecIn->baseVirt).GetPtr();
					switch (req->cmd)
					{
					case Cmd::Status:
					{
						BTRM_LOG("IOCTLV STATUS");
						const auto btrmStatus = BtrmStatus{s_btrm.stateFlags};
						std::memcpy(resp->resultData, &btrmStatus, sizeof(BtrmStatus));
						s_btrm.stateFlags |= StateFlags::GotStatus;
						IOS_ResourceReply(cmdBody, IOS_ERROR_OK);
						continue;
					}
					case Cmd::Visible:
					{
						BTRM_LOG("IOCTLV VISIBLE");
						IOS_ResourceReply(cmdBody, IOS_ERROR_OK);
						continue;
					}
					case Cmd::WUD:
					{
						IOS_ResourceReply(cmdBody, HandleWUD(*req, *resp));
						continue;
					}
					case Cmd::BTE:
					{
						IOS_ResourceReply(cmdBody, HandleBTE(*req, *resp));
						continue;
					}
					case Cmd::QueueEvt:
					{
						BTRM_LOG("IOCTLV QUEUE EVT");
						IOS_SendMessage(s_btrmTransMsgQueueId, MEMPTR(cmdBody).GetMPTR(), 1);
						continue;
					}
					default:
					{
						BTRM_LOG("IOCTLV INVALID FUNC {:#02x}", req->func);
						IOS_ResourceReply(cmdBody, IOS_ERROR_INVALID);
						continue;
					}
					}
				}
				default:
				{
					BTRM_LOG("Unsupported cmdId: {}", (uint32)cmdBody->cmdId.value());
					cemu_assert_unimplemented();
					IOS_ResourceReply(cmdBody, IOS_ERROR_INVALID);
				}
				}
			}
		}

		IOS_ERROR HandleIoctl(IPCCommandBody& cmdBody)
		{
			auto& args = cmdBody.args.ioctl;
			const auto reqId = args.requestId.value();
			switch (static_cast<IoctlId>(reqId))
			{
			case IoctlId::InitSmd:
			{
				BTRM_LOG("IOCTL SMD INIT");
				if (s_btrm.disconnectTimer > 0)
				{
					kernel::IOS_DestroyTimer(s_btrm.disconnectTimer);
					s_btrm.disconnectTimer = IOS_ERROR_INVALID;
				}
				if (s_btrm.smdInitialized)
				{
					return IOS_ERROR_INVALID;
				}
				auto ctrlTable = MEMPTR<smd::SmdCtrlTable>(args.ptrIn);
				if (smd::smdIopInit(SMD_HANDLE, *ctrlTable, "btSmd") != smd::SmdError::Ok)
					return IOS_ERROR_INVALID;

				smd::smdIopOpen(SMD_HANDLE);
				//TODO: Handle QueueSizes

				s_btrm.receiveTimer = kernel::IOS_CreateTimer(5000, 5000, s_btrmRmMsgQueueId, +InternalMsg::ReceiveFromSmdPpc);
				return IOS_ERROR_OK;
			}
			case IoctlId::CloseSmd:
			{
				BTRM_LOG("IOCTL SMD CLOSE");
				s_btrm.disconnectTimer = kernel::IOS_CreateTimer(300'000'000, 0, s_btrmRmMsgQueueId, +InternalMsg::Disconnect);
				if (s_btrm.disconnectTimer <= 0)
					BTRM_LOG("Unable to create disconnect timer: {}", s_btrm.disconnectTimer);
				if (s_btrm.smdInitialized)
				{
					smd::smdIopClose(SMD_HANDLE);
					return IOS_ERROR_OK;
				}
				return IOS_ERROR_INVALID;
			}
			case IoctlId::RemoveFromQueue:
			{
				BTRM_LOG("IOCTL REMOVE FROM QUEUE");
				RemoveAllFromQueue();
				return IOS_ERROR_OK;
			}
			case IoctlId::IsolateChannels:
			case IoctlId::SysUpdate:
			case IoctlId::InitOHCI:
			{
				// padscore doesn't use these
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case IoctlId::ReportProcData:
			{
				BTRM_LOG("IOCTL REPORT PROC DATA");
				return IOS_ERROR_OK;
			}
			case IoctlId::DevInfo:
			{
				BTRM_LOG("IOCTL SET DEVICE INFO");
				return IOS_ERROR_OK;
			}
			case IoctlId::Disconnect:
			{
				if (!args.ptrOut || args.sizeOut != sizeof(BTAddr))
				{
					BTRM_LOG("Invalid BT disconnect: outPtr={} outLen={:d}", args.ptrOut, args.sizeOut);
					return IOS_ERROR_INVALID;
				}

				auto bdaddr = static_cast<BTAddr*>(args.ptrOut.GetPtr());
				BTRM_LOG("Requested to disconnect {:02x}", fmt::join(*bdaddr, ":"));
				return IOS_ERROR_OK;
			}
			case IoctlId::SetBtCfg:
			{
				if (!args.ptrOut || args.sizeOut != 16)
				{
					BTRM_LOG("Invalid BT set cfg: outPtr={} outLen={}", args.ptrOut, args.sizeOut);
					return IOS_ERROR_INVALID;
				}
				auto cfg = static_cast<BtCfg*>(args.ptrOut.GetPtr());
				BTRM_LOG("IOCTL SET CFG Max controllers={} Enabled Devs: (Wiimote={}, Pro={}, WBC={})", cfg->maxControllers, cfg->wiimoteEnabled, cfg->urccEnabled, cfg->wbcEnabled);
				return IOS_ERROR_OK;
			}
			default:
			{
				BTRM_LOG("IOCTL INVALID {:#04x}", reqId);
				return IOS_ERROR_INVALID;
			}
			}
		}

		IOS_ERROR HandleBTE(const Request& req, Response& resp)
		{
			//TODO
			switch (static_cast<BTEFunc>(req.func))
			{
			case BTEFunc::Disconnect:
			{
				BTRM_LOG("IOCTLV BTE DISCONNECT");
				return IOS_ERROR_OK;
			}
			// HID Writes are sent over SMD instead of IOCTLV calls
			case BTEFunc::HidWrite:
			{
				BTRM_LOG("IOCTLV BTE HID_WRITE (unexpected)");
				cemu_assert_suspicious();
				return IOS_ERROR_OK;
			}
			case BTEFunc::CloseACL:
			{
				BTRM_LOG("IOCTLV BTE ACL CLOSE");
				return IOS_ERROR_OK;
			}
			case BTEFunc::Unk0x03:
			{
				BTRM_LOG("IOCTLV BTE UNK 0x03");
				return IOS_ERROR_OK;
			}
			default:
			{
				BTRM_LOG("IOCTLV BTE INVALID ({:#02x})", req.func);
				return IOS_ERROR_INVALID;
			}
			}
		}
		IOS_ERROR HandleWUD(const Request& req, Response& resp)
		{
			//TODO: Implement all of this
			switch (static_cast<WudFunc>(req.func))
			{
			case WudFunc::StartSync:
			{
				BTRM_LOG("IOCTLV WUD START SYNC");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::StartSyncAlt:
			{
				BTRM_LOG("IOCTLV WUD START SYNC ALT");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::PurgeDevInfo:
			{
				BTRM_LOG("IOCTLV WUD PURGE DEVICE INFO");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SetSyncLoop:
			{
				BTRM_LOG("IOCTLV WUD SET SYNC LOOP");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::CancelSync:
			{
				BTRM_LOG("IOCTLV WUD CANCEL SYNC");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::DisableChannel:
			{
				BTRM_LOG("IOCTLV WUD DISABLE CHANNEL");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::StartClearDevices:
			{
				BTRM_LOG("IOCTLV WUD START CLEAR DEV");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SetVisibility:
			{
				BTRM_LOG("IOCTLV WUD SET VISIBILITY");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SetSniffMode:
			{
				BTRM_LOG("IOCTLV WUD SET SNIFF MODE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::RegisterNewDev:
			{
				BTRM_LOG("IOCTLV WUD REGISTER NEW DEVICE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::EnableClearDeviceResponse:
			{
				BTRM_LOG("IOCTLV WUD ENABLE CLEAR DEV RESPONSE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::EnableSyncResponse:
			{
				BTRM_LOG("IOCTLV WUD ENABLE SYNC DEV RESPONSE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::EnableWBC:
			{
				BTRM_LOG("IOCTLV WUD ENABLE WBC");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SerialFlashTestUpdate:
			{
				BTRM_LOG("IOCTLV WUD SERIAL FLASH TEST UPDATE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SerialFlashTestMode:
			{
				BTRM_LOG("IOCTLV WUD SERIAL FLASH TEST MODE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SerialFlashTestWrite:
			{
				BTRM_LOG("IOCTLV WUD SERIAL FLASH TEST WRITE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::SerialFlashTestRead:
			{
				BTRM_LOG("IOCTLV WUD SERIAL FLASH TEST READ");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::ReportProcData:
			{
				BTRM_LOG("IOCTLV WUD REPORT PROC DATA");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::StartSyncEx:
			{
				BTRM_LOG("IOCTLV WUD START SYNC EX");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::StoreScArray:
			{
				BTRM_LOG("IOCTLV WUD STORE SC ARRAY");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::ClearScArray:
			{
				BTRM_LOG("IOCTLV WUD CLEAR SC ARRAY");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::DisbaleSyncResponse:
			{
				BTRM_LOG("IOCTLV WUD DISABLE SYNC RESPONSE");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			case WudFunc::Unk0x1C:
			{
				// Haven't found where this is supposed to be handled
				kernel::IOS_SendMessage(s_btrmRmMsgQueueId, +InternalMsg::Unknown0x17, 1);
				break;
			}
			case WudFunc::Noop:
				break;
			case WudFunc::DisableWBC:
				BTRM_LOG("IOCTLV WUD DISABLE WBC");
				cemu_assert_unimplemented();
				return IOS_ERROR_OK;
			}
			return IOS_ERROR_OK;
		}

	} // namespace
	class : public ::IOSUModule
	{
	  public:
		void SystemLaunch() override
		{
			s_btrmRmMsgQueueId = kernel::IOS_CreateMessageQueue(s_msgBuffer.GetPtr(), s_msgBuffer.GetCount());
			cemu_assert(!IOS_ResultIsError(static_cast<IOS_ERROR>(s_btrmRmMsgQueueId)));

			IOS_ERROR error = kernel::IOS_RegisterResourceManager(BTRM_PATH, s_btrmRmMsgQueueId);
			cemu_assert(!IOS_ResultIsError(error));

			error = kernel::IOS_RegisterResourceManager(EARLY_BTRM_PATH, s_btrmRmMsgQueueId);
			cemu_assert(!IOS_ResultIsError(error));

			s_serviceThread = std::thread(ServiceThread);
		}

		void SystemExit() override
		{
			kernel::IOS_SendMessage(s_btrmRmMsgQueueId, EXIT_MSG, 0);
			s_serviceThread.join();

			kernel::IOS_DestroyMessageQueue(s_btrmRmMsgQueueId);
			kernel::IOS_DestroyMessageQueue(s_btrmTransMsgQueueId);
			s_btrmRmMsgQueueId = -1;
		}
	} sIOSUModuleUSBBTRM;

	IOSUModule* GetModule()
	{
		return &sIOSUModuleUSBBTRM;
	}
} // namespace iosu::btrm