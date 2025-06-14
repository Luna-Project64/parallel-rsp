#ifdef DEBUG_JIT
#include "debug_rsp.hpp"
#else
#include "rsp_jit.hpp"
#endif
#include <stdint.h>

#include "Zilmar_Rsp.h"

#define RSP_PARALLEL_VERSION 0x0101
#define RSP_PLUGIN_API_VERSION 0x020000

#ifdef PARALLEL_INTEGRATION_EX
uint32_t m64p_rsp_yielded_on_semaphore;
#endif

namespace RSP
{
RSP_INFO rsp;
#ifdef DEBUG_JIT
RSP::CPU* cpu;
#else
RSP::JIT::CPU* cpu;
#endif
short MFC0_count[32];
short semaphore_count[32];
int SP_STATUS_TIMEOUT;
int SP_SEMAPHORE_TIMEOUT;
bool graphics_hle = false;

namespace Zilmar
{
	short Set_GraphicsHle = 0;
	enum SettingLocation
	{
		SettingType_ConstString = 0,
		SettingType_ConstValue = 1,
		SettingType_CfgFile = 2,
		SettingType_Registry = 3,
		SettingType_RelativePath = 4,
		TemporarySetting = 5,
		SettingType_RomDatabase = 6,
		SettingType_CheatSetting = 7,
		SettingType_GameSetting = 8,
		SettingType_BoolVariable = 9,
		SettingType_NumberVariable = 10,
		SettingType_StringVariable = 11,
		SettingType_SelectedDirectory = 12,
		SettingType_RdbSetting = 13,
	};

	enum SettingDataType
	{
		Data_DWORD = 0, Data_String = 1, Data_CPUTYPE = 2, Data_SelfMod = 3, Data_OnOff = 4, Data_YesNo = 5, Data_SaveChip = 6
	};

	typedef struct
	{
		uint32_t dwSize;
		int DefaultStartRange;
		int SettingStartRange;
		int MaximumSettings;
		int NoDefault;
		int DefaultLocation;
		void * handle;

		unsigned int(CALL *GetSetting)      (void * handle, int ID);
		const char * (CALL *GetSettingSz)    (void * handle, int ID, char * Buffer, int BufferLen);
		void(CALL *SetSetting)      (void * handle, int ID, unsigned int Value);
		void(CALL *SetSettingSz)    (void * handle, int ID, const char * Value);
		void(CALL *RegisterSetting) (void * handle, int ID, int DefaultID, SettingDataType Type,
			SettingLocation Location, const char * Category, const char * DefaultStr, uint32_t Value);
		void(CALL *UseUnregisteredSetting) (int ID);
	} PLUGIN_SETTINGS;

	typedef struct
	{
		unsigned int(CALL *FindSystemSettingId) (void * handle, const char * Name);
	} PLUGIN_SETTINGS2;

	static PLUGIN_SETTINGS  g_PluginSettings;
	static PLUGIN_SETTINGS2 g_PluginSettings2;
	static inline unsigned int GetSystemSetting(short SettingID)
	{
		return g_PluginSettings.GetSetting(g_PluginSettings.handle, SettingID);
	}
	
	static inline short FindSystemSettingId(const char * Name)
	{
		if (g_PluginSettings2.FindSystemSettingId && g_PluginSettings.handle)
		{
			return (short)g_PluginSettings2.FindSystemSettingId(g_PluginSettings.handle, Name);
		}
		return 0;
	}
}
} // namespace RSP

extern "C"
{
	// Hack entry point to use when loading savestates when we're tracing.
	void rsp_clear_registers()
	{
		memset(RSP::cpu->get_state().sr, 0, sizeof(uint32_t) * 32);
		memset(&RSP::cpu->get_state().cp2, 0, sizeof(RSP::cpu->get_state().cp2));
	}

#ifdef INTENSE_DEBUG
	// Need super-fast hash here.
	static uint64_t hash_imem(const uint8_t *data, size_t size)
	{
		uint64_t h = 0xcbf29ce484222325ull;
		size_t i;
		for (i = 0; i < size; i++)
			h = (h * 0x100000001b3ull) ^ data[i];
		return h;
	}

	void log_rsp_mem_parallel(void)
	{
		fprintf(stderr, "IMEM HASH: 0x%016llx\n", hash_imem(RSP::rsp.IMEM, 0x1000));
		fprintf(stderr, "DMEM HASH: 0x%016llx\n", hash_imem(RSP::rsp.DMEM, 0x1000));
	}
#endif

	EXPORT unsigned int CALL DoRspCycles(unsigned int cycles)
	{
#ifdef PARALLEL_INTEGRATION_EX
		m64p_rsp_yielded_on_semaphore = 0;
#endif

		uint32_t TaskType = *(uint32_t*)(RSP::rsp.DMEM + 0xFC0);
		if (TaskType == 1 && RSP::graphics_hle && *(uint32_t*)(RSP::rsp.DMEM + 0x0ff0) != 0)
		{
			if (RSP::rsp.ProcessDlist)
			{
				RSP::rsp.ProcessDlist();
			}
			*RSP::rsp.SP_STATUS_REG |= (0x0203 );
			if ((*RSP::rsp.SP_STATUS_REG & SP_STATUS_INTR_BREAK) != 0 )
			{
				*RSP::rsp.MI_INTR_REG |= 1;
				RSP::rsp.CheckInterrupts();
			}

			*RSP::rsp.DPC_STATUS_REG &= ~0x0002;
			return cycles;
		}

		if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT)
			return 0;

		// We don't know if Mupen from the outside invalidated our IMEM.
		RSP::cpu->invalidate_imem();

		// Run CPU until we either break or we need to fire an IRQ.
		RSP::cpu->get_state().pc = *RSP::rsp.SP_PC_REG & 0xfff;

#ifdef INTENSE_DEBUG
		fprintf(stderr, "RUN TASK: %u\n", RSP::cpu.get_state().pc);
		log_rsp_mem_parallel();
#endif

		for (auto &count : RSP::MFC0_count)
			count = 0;

		for (auto &count : RSP::semaphore_count)
			count = 0;

		while (!(*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT))
		{
			auto mode = RSP::cpu->run();
			if (mode == RSP::MODE_CHECK_FLAGS && (*RSP::cpu->get_state().cp0.irq & 1))
				break;
#ifdef PARALLEL_INTEGRATION_EX
			if (m64p_rsp_yielded_on_semaphore)
				break;
#endif
		}

		*RSP::rsp.SP_PC_REG = 0x04001000 | (RSP::cpu->get_state().pc & 0xffc);
#ifdef PARALLEL_INTEGRATION_EX
		if (m64p_rsp_yielded_on_semaphore)
			return cycles;
#endif

		// From CXD4.
		if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_BROKE)
			return cycles;
		else if (*RSP::cpu->get_state().cp0.irq & 1)
			RSP::rsp.CheckInterrupts();
		else if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT)
			return cycles;
		else
			RSP::SP_STATUS_TIMEOUT = 16; // From now on, wait 16 times, not 0x7fff

		// CPU restarts with the correct SIGs.
		*RSP::rsp.SP_STATUS_REG &= ~SP_STATUS_HALT;

		return cycles;
	}

	EXPORT void CALL GetDllInfo(PLUGIN_INFO * PluginInfo)
	{
		PluginInfo->Version = 0x0101;
		PluginInfo->Type = PLUGIN_TYPE_RSP;
		strcpy(PluginInfo->Name, "ParaLLel Launcher RSP Plugin v0.6");
		PluginInfo->NormalMemory = 1;
		PluginInfo->MemoryBswaped = 1;
	}

	EXPORT void CALL RomClosed(void)
	{
		*RSP::rsp.SP_PC_REG = 0x00000000;
		delete RSP::cpu;
		RSP::cpu = nullptr;
	}
	
	EXPORT void CALL SetSettingInfo(RSP::Zilmar::PLUGIN_SETTINGS * info)
	{
		RSP::Zilmar::g_PluginSettings = *info;
	}

	EXPORT void CALL SetSettingInfo2(RSP::Zilmar::PLUGIN_SETTINGS2 * info)
	{
		RSP::Zilmar::g_PluginSettings2 = *info;
	}
	
	EXPORT void CALL PluginLoaded(void)
	{
		RSP::Zilmar::Set_GraphicsHle = RSP::Zilmar::FindSystemSettingId("HLE GFX");
	}

	EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
	{
		RSP::graphics_hle = RSP::Zilmar::GetSystemSetting(RSP::Zilmar::Set_GraphicsHle);

		if (CycleCount)
			*CycleCount = 0;

		if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
			return; /* DMA is not executed just because plugin initiates. */

#ifdef DEBUG_JIT
		RSP::cpu = new (std::align_val_t(64)) RSP::CPU();
#else
		RSP::cpu = new (std::align_val_t(64)) RSP::JIT::CPU();
#endif

		RSP::rsp = Rsp_Info;
		*RSP::rsp.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */

		auto **cr = RSP::cpu->get_state().cp0.cr;
		cr[0x0] = RSP::rsp.SP_MEM_ADDR_REG;
		cr[0x1] = RSP::rsp.SP_DRAM_ADDR_REG;
		cr[0x2] = RSP::rsp.SP_RD_LEN_REG;
		cr[0x3] = RSP::rsp.SP_WR_LEN_REG;
		cr[0x4] = RSP::rsp.SP_STATUS_REG;
		cr[0x5] = RSP::rsp.SP_DMA_FULL_REG;
		cr[0x6] = RSP::rsp.SP_DMA_BUSY_REG;
		cr[0x7] = RSP::rsp.SP_SEMAPHORE_REG;
		cr[0x8] = RSP::rsp.DPC_START_REG;
		cr[0x9] = RSP::rsp.DPC_END_REG;
		cr[0xA] = RSP::rsp.DPC_CURRENT_REG;
		cr[0xB] = RSP::rsp.DPC_STATUS_REG;
		cr[0xC] = RSP::rsp.DPC_CLOCK_REG;
		cr[0xD] = RSP::rsp.DPC_BUFBUSY_REG;
		cr[0xE] = RSP::rsp.DPC_PIPEBUSY_REG;
		cr[0xF] = RSP::rsp.DPC_TMEM_REG;

		*cr[RSP::CP0_REGISTER_SP_STATUS] = SP_STATUS_HALT;
		RSP::cpu->get_state().cp0.irq = RSP::rsp.MI_INTR_REG;

		// From CXD4.
		RSP::SP_STATUS_TIMEOUT = 0x7fff;
		RSP::SP_SEMAPHORE_TIMEOUT = 4;

		RSP::cpu->set_dmem(reinterpret_cast<uint32_t *>(Rsp_Info.DMEM));
		RSP::cpu->set_imem(reinterpret_cast<uint32_t *>(Rsp_Info.IMEM));
		RSP::cpu->set_rdram(reinterpret_cast<uint32_t *>(Rsp_Info.RDRAM));
	}

	EXPORT void CALL CloseDLL(void)
	{
	}

	EXPORT void CALL DllConfig(int hWnd)
	{
	}
}
