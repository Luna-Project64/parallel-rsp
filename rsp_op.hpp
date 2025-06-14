#ifndef RSP_OP_HPP__
#define RSP_OP_HPP__

#include "state.hpp"
#include "jit_decl.h"

namespace LS
{
#define DECL_LS(op)             \
	template <unsigned e> \
	void JIT_DECL RSP_##op(RSP::CPUState *rsp, uint32_t)

DECL_LS(LBV);
DECL_LS(LSV);
DECL_LS(LLV);
DECL_LS(LDV);
DECL_LS(LQV);
DECL_LS(LRV);
DECL_LS(LPV);
DECL_LS(LUV);
DECL_LS(LHV);
DECL_LS(LFV);
DECL_LS(LWV);
DECL_LS(LTV);

DECL_LS(SBV);
DECL_LS(SSV);
DECL_LS(SLV);
DECL_LS(SDV);
DECL_LS(SQV);
DECL_LS(SRV);
DECL_LS(SPV);
DECL_LS(SUV);
DECL_LS(SHV);
DECL_LS(SFV);
DECL_LS(SWV);
DECL_LS(STV);
} // namespace LS

namespace CP
{
	int JIT_DECL RSP_MFC0(RSP::CPUState *rsp, uint32_t);
	int JIT_DECL RSP_MTC0(RSP::CPUState *rsp, uint32_t);

	void JIT_DECL RSP_MTC2(RSP::CPUState *rsp, uint32_t);
    void JIT_DECL RSP_MFC2(RSP::CPUState *rsp, uint32_t);
    void JIT_DECL RSP_CFC2(RSP::CPUState *rsp, uint32_t);
    void JIT_DECL RSP_CTC2(RSP::CPUState *rsp, uint32_t);
    }

extern "C"
{
	void JIT_DECL RSP_CALL(void *opaque, unsigned target, unsigned ret);
	void JIT_DECL RSP_RETURN(void *opaque, unsigned pc);
	void JIT_DECL RSP_EXIT(void *opaque, int mode);
	void JIT_DECL RSP_REPORT_PC(void *rsp, unsigned pc, unsigned instr);
}

namespace VU
{
#define DECL_COP2(op) \
template <unsigned e> \
	void JIT_DECL RSP_##op(RSP::CPUState *rsp, uint32_t)
    DECL_COP2(VRNDP);
    DECL_COP2(VMULQ);
    DECL_COP2(VRNDN);
    DECL_COP2(VMACQ);
    DECL_COP2(VRCP);
    DECL_COP2(VRCPL);
    DECL_COP2(VRCPH);
    DECL_COP2(VMOV);
    DECL_COP2(VRSQ);
    DECL_COP2(VRSQL);
    DECL_COP2(VRSQH);
    DECL_COP2(VNOP);
#undef DECL_COP2

#define DECL_COP2_V(op)     \
template <unsigned e>  rsp_vect_t JIT_VECTORDECL RSP_##op(RSP::CPUState *rsp, unsigned vt, rsp_vect_t vs)
    DECL_COP2_V(VMULF);
    DECL_COP2_V(VMULU);
    // DECL_COP2_V(VRNDP);
    // DECL_COP2_V(VMULQ);
    DECL_COP2_V(VMUDL);
    DECL_COP2_V(VMUDM);
    DECL_COP2_V(VMUDN);
    DECL_COP2_V(VMUDH);
    DECL_COP2_V(VMACF);
    DECL_COP2_V(VMACU);
    // DECL_COP2_V(VRNDN);
    // DECL_COP2_V(VMACQ);
    DECL_COP2_V(VMADL);
    DECL_COP2_V(VMADM);
    DECL_COP2_V(VMADN);
    DECL_COP2_V(VMADH);
    DECL_COP2_V(VADD);
    DECL_COP2_V(VSUB);
    DECL_COP2_V(VABS);
    DECL_COP2_V(VADDC);
    DECL_COP2_V(VSUBC);
    DECL_COP2_V(VSAR);
    DECL_COP2_V(VLT);
    DECL_COP2_V(VEQ);
    DECL_COP2_V(VNE);
    DECL_COP2_V(VGE);
    DECL_COP2_V(VCL);
    DECL_COP2_V(VCH);
    DECL_COP2_V(VCR);
    DECL_COP2_V(VMRG);
    DECL_COP2_V(VAND);
    DECL_COP2_V(VNAND);
    DECL_COP2_V(VOR);
    DECL_COP2_V(VNOR);
    DECL_COP2_V(VXOR);
    DECL_COP2_V(VNXOR);
    // DECL_COP2_V(VRCP);
    // DECL_COP2_V(VRCPL);
    // DECL_COP2_V(VRCPH);
    // DECL_COP2_V(VMOV);
    // DECL_COP2_V(VRSQ);
    // DECL_COP2_V(VRSQL);
    // DECL_COP2_V(VRSQH);
    // DECL_COP2_V(VNOP);
    DECL_COP2_V(RESERVED);
#undef DECL_COP2_V
    }

#endif
