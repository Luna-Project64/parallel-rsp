#include "../state.hpp"
#include "../jit_decl.h"
#include "packed_cp.h"
#include <string.h>

#ifdef PARALLEL_INTEGRATION
#include "../Zilmar_Rsp.h"
namespace RSP
{
extern RSP_INFO rsp;
extern short MFC0_count[32];
extern short semaphore_count[32];
extern int SP_STATUS_TIMEOUT;
extern int SP_SEMAPHORE_TIMEOUT;
extern bool graphics_hle;
} // namespace RSP
extern uint32_t m64p_rsp_yielded_on_semaphore;
extern bool rsp_yield_aware;
#endif

using namespace RSP;

namespace CP
{

#ifdef INTENSE_DEBUG
	void log_rsp_mem_parallel(void);
#endif

	int JIT_DECL RSP_MFC0(RSP::CPUState *rsp, unsigned rt, unsigned rd)
	{
	    rd &= 15;
	    uint32_t res = *rsp->cp0.cr[rd];
	    if (rt)
		    rsp->sr[rt] = res;

#ifdef PARALLEL_INTEGRATION
	    if (rd == CP0_REGISTER_SP_STATUS)
	    {
		    // Might be waiting for the CPU to set a signal bit on the STATUS register. Increment timeout
		    RSP::MFC0_count[rt] += 1;
		    if (RSP::MFC0_count[rt] >= RSP::SP_STATUS_TIMEOUT)
		    {
			    *RSP::rsp.SP_STATUS_REG |= SP_STATUS_HALT;
			    return MODE_CHECK_FLAGS;
		    }
	    }
#endif

	    if (rd == CP0_REGISTER_SP_SEMAPHORE)
	    {
		    if (*rsp->cp0.cr[CP0_REGISTER_SP_SEMAPHORE])
		    {
#ifdef PARALLEL_INTEGRATION
			    if (rsp_yield_aware && (++RSP::semaphore_count[rt] >= RSP::SP_SEMAPHORE_TIMEOUT))
			    {
				    m64p_rsp_yielded_on_semaphore = 1;
				    return MODE_CHECK_FLAGS;
			    }
#endif
		    }
		    else
			    *rsp->cp0.cr[CP0_REGISTER_SP_SEMAPHORE] = 1;
	    }

	    //if (rd == 4) // SP_STATUS_REG
	    //   fprintf(stderr, "READING STATUS REG!\n");

	    return MODE_CONTINUE;
	}

	int JIT_DECL RSP_MFC0(RSP::CPUState* rsp, uint32_t value)
	{
	    PackedCP packed;
	    packed.value = value;
	    return RSP_MFC0(rsp, packed.rt, packed.rd);
	}

#define RSP_HANDLE_STATUS_WRITE(flag) \
	switch (rt & (SP_SET_##flag | SP_CLR_##flag)) \
	{ \
		case SP_SET_##flag: status |= SP_STATUS_##flag; break; \
		case SP_CLR_##flag: status &= ~SP_STATUS_##flag; break; \
		default: break; \
	}

	static inline int rsp_status_write(RSP::CPUState *rsp, uint32_t rt)
	{
		//fprintf(stderr, "Writing 0x%x to status reg!\n", rt);

		uint32_t status = *rsp->cp0.cr[CP0_REGISTER_SP_STATUS];

		RSP_HANDLE_STATUS_WRITE(HALT)
		RSP_HANDLE_STATUS_WRITE(SSTEP)
		RSP_HANDLE_STATUS_WRITE(INTR_BREAK)
		RSP_HANDLE_STATUS_WRITE(SIG0)
		RSP_HANDLE_STATUS_WRITE(SIG1)
		RSP_HANDLE_STATUS_WRITE(SIG2)
		RSP_HANDLE_STATUS_WRITE(SIG3)
		RSP_HANDLE_STATUS_WRITE(SIG4)
		RSP_HANDLE_STATUS_WRITE(SIG5)
		RSP_HANDLE_STATUS_WRITE(SIG6)
		RSP_HANDLE_STATUS_WRITE(SIG7)

		switch (rt & (SP_SET_INTR | SP_CLR_INTR))
		{
			case SP_SET_INTR: *rsp->cp0.irq |= 1; break;
			case SP_CLR_INTR: *rsp->cp0.irq &= ~1; break;
			default: break;
		}

		if (rt & SP_CLR_BROKE)
			status &= ~SP_STATUS_BROKE;

		*rsp->cp0.cr[CP0_REGISTER_SP_STATUS] = status;
		return ((*rsp->cp0.irq & 1) || (status & SP_STATUS_HALT)) ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

#ifdef PARALLEL_INTEGRATION
	static int rsp_dma_read(RSP::CPUState *rsp)
    {
	    uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH];
	    uint32_t length = ((length_reg & 0xFFF) | 7) + 1;
	    uint32_t skip = (length_reg >> 20) & 0xFF8;
	    unsigned count = ((length_reg >> 12) & 0xFF) + 1;

	    unsigned i = 0;
	    uint32_t rdram = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] & 0xfffff8;
	    uint32_t spmem = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xff8;
	    bool imem = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0x1000;
		// TODO: Technically aliasing
	    uint8_t *rsp_ptr = imem ? (uint8_t *)rsp->imem : (uint8_t *)rsp->dmem;

	    if ((0 == skip || 1 == count) && (spmem + length) <= 0x1000 && (rdram + length) <= rsp->rdram_size)
	    {
		    // TODO: Sane case should probably also include the case where skip == 0 but i do not care enough
		    uint8_t *rdram_ptr = ((uint8_t*)rsp->rdram) + rdram;
		    memcpy(rsp_ptr + spmem, rdram_ptr, length);
		    unsigned first = spmem / CODE_BLOCK_SIZE;
		    unsigned last = (spmem + length - 1) / CODE_BLOCK_SIZE;

		    for (unsigned block = first; block <= last; block++)
				rsp->dirty_blocks |= (1u << block);

			rdram += length;
		    spmem += length;
		}
	    else
	    {
		    do
		    {
			    unsigned j = 0;
			    do
			    {
				    uint32_t source_addr = rdram + j;
				    uint32_t dest_addr = (spmem + j) & 0xfff;
				    uint64_t word = source_addr >= rsp->rdram_size ? 0 : *(uint64_t *)(((uint8_t*)rsp->rdram) + source_addr);
					if (imem)
				    {
					    // Invalidate IMEM - this roundness is sufficient because of dest_addr
					    unsigned block = (dest_addr & 0xfff) / CODE_BLOCK_SIZE;
					    rsp->dirty_blocks |= (0x3 << block) >> 1;
					    //rsp->dirty_blocks = ~0u;
					}

				    *(uint64_t *)(rsp_ptr + dest_addr) = word;

				    j += 8;
			    } while (j < length);

			    rdram += length + skip;
			    spmem += length;
		    } while (++i < count);
	    }

	    *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = rdram;
	    *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = spmem & 0xff8;
	    *rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH] = 0xff8;

	    return rsp->dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

	static void rsp_dma_write(RSP::CPUState *rsp)
    {
	    uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH];
	    uint32_t length = ((length_reg & 0xFFF) | 7) + 1;
	    uint32_t skip = (length_reg >> 20) & 0xFF8;
	    unsigned count = ((length_reg >> 12) & 0xFF) + 1;

	    unsigned i = 0;
	    uint32_t rdram = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] & 0xfffff8;
	    uint32_t spmem = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xff8;
	    bool imem = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0x1000;
	    // TODO: Technically aliasing
	    uint8_t *rsp_ptr = imem ? (uint8_t *)rsp->imem : (uint8_t *)rsp->dmem;

	    if ((0 == skip || 1 == count) && (spmem + length) <= 0x1000 && (rdram + length) <= rsp->rdram_size)
	    {
		    // TODO: Sane case should probably also include the case where skip == 0 but i do not care enough
		    uint8_t *rdram_ptr = ((uint8_t*)rsp->rdram) + rdram;
		    memcpy(rdram_ptr, rsp_ptr + spmem, length);
		    rdram += length;
		    spmem += length;
	    }
	    else
	    {
		    do
		    {
			    unsigned j = 0;
			    do
			    {
				    uint32_t dest_addr = rdram + j;
				    uint32_t source_addr = (spmem + j) & 0xfff;
				    uint64_t word = *(uint64_t *)(rsp_ptr + source_addr);
				    if (dest_addr < rsp->rdram_size)
					    *(uint64_t *)(((uint8_t*)rsp->rdram) + dest_addr) = word;

				    j += 8;
			    } while (j < length);

			    rdram += length + skip;
			    spmem += length;
		    } while (++i < count);
	    }

	    *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = rdram;
	    *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = spmem & 0xff8;
	    *rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH] = 0xff8;
	}
#endif

	int JIT_DECL RSP_MTC0(RSP::CPUState *rsp, unsigned rd, unsigned rt)
	{
		uint32_t val = rsp->sr[rt];

		switch (static_cast<CP0Registers>(rd & 15))
		{
		case CP0_REGISTER_DMA_CACHE:
			*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = val & 0x1fff;
			break;

		case CP0_REGISTER_DMA_DRAM:
			*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = val & 0xffffff;
			break;

		case CP0_REGISTER_DMA_READ_LENGTH:
			*rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH] = val;
#ifdef PARALLEL_INTEGRATION
			return rsp_dma_read(rsp);
#else
			return MODE_DMA_READ;
#endif

		case CP0_REGISTER_DMA_WRITE_LENGTH:
			*rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH] = val;
#ifdef PARALLEL_INTEGRATION
			rsp_dma_write(rsp);
#endif
			break;

		case CP0_REGISTER_SP_STATUS:
			return rsp_status_write(rsp, val);

		case CP0_REGISTER_SP_SEMAPHORE:
			// Any write to the semaphore register, regardless of value, sets it to 0 for the next read
			*rsp->cp0.cr[CP0_REGISTER_SP_SEMAPHORE] = 0;
			break;

		case CP0_REGISTER_CMD_START:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_START 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_START] = *rsp->cp0.cr[CP0_REGISTER_CMD_CURRENT] =
			    *rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;
			break;

		case CP0_REGISTER_CMD_END:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_END 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;

#ifdef PARALLEL_INTEGRATION
			RSP::rsp.ProcessRdpList();
#endif
			break;

		case CP0_REGISTER_CMD_CLOCK:
			*rsp->cp0.cr[CP0_REGISTER_CMD_CLOCK] = val;
			break;

		case CP0_REGISTER_CMD_STATUS:
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x1) << 0);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x2) << 0);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x4) << 1);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x8) << 1);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x10) << 2);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x20) << 2);
			*rsp->cp0.cr[CP0_REGISTER_CMD_TMEM_BUSY] &= !(val & 0x40) * -1;
			*rsp->cp0.cr[CP0_REGISTER_CMD_CLOCK] &= !(val & 0x200) * -1;
			break;

		case CP0_REGISTER_CMD_CURRENT:
		case CP0_REGISTER_CMD_BUSY:
		case CP0_REGISTER_CMD_PIPE_BUSY:
		case CP0_REGISTER_CMD_TMEM_BUSY:
			break;

		default:
			*rsp->cp0.cr[rd & 15] = val;
			break;
		}

		return MODE_CONTINUE;
    }

    int JIT_DECL RSP_MTC0(RSP::CPUState *rsp, uint32_t value)
    {
	    PackedCP packed;
	    packed.value = value;
	    return RSP_MTC0(rsp, packed.rd, packed.rt);
    }
}
