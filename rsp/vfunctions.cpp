//
// rsp/vfunctions.c: RSP vector execution functions.
//
// CEN64: Cycle-Accurate Nintendo 64 Emulator.
// Copyright (C) 2015, Tyler J. Stachecki.
//
// This file is subject to the terms and conditions defined in
// 'LICENSE', which is part of this source code package.
//

#include "../rsp_op.hpp"
#include "../state.hpp"
#include "element_instantiate.h"
#include "packed_vu.h"
#include "rsp_impl.h"
#include <stdio.h>

#define LOAD_VS() rsp_vect_load_unshuffled_operand(rsp->cp2.regs[vs].e)
#define LOAD_VT() rsp_vect_load_and_shuffle_operand<e>(rsp->cp2.regs[vt].e)
#define STORE_RESULT() rsp_vect_write_operand(rsp->cp2.regs[vd].e, result)

#ifdef TRACE_COP2
#define TRACE_VU(op) printf(#op " v%u, v%u, v%u[%u]\n", vd, vs, vt, e)
#else
#define TRACE_VU(op) ((void)0)
#endif

#define VU_INSTANTIATED(op, i) template void JIT_DECL RSP_##op<i>(RSP::CPUState * rsp, uint32_t);

#define IMPL_VU(op)                                                                    \
	template <unsigned e>                                                              \
	void JIT_DECL RSP_##op(RSP::CPUState *rsp, uint32_t);                              \
	ELEMENT_INSTANTIATE(op, VU_INSTANTIATED)                                           \
	template <unsigned e>                                                              \
	static rsp_vect_t JIT_DECL RSP_##op(RSP::CPUState *rsp, unsigned vs, unsigned vt); \
	template <unsigned e>                                                              \
	void JIT_DECL RSP_##op(RSP::CPUState *rsp, uint32_t value)                         \
	{                                                                                  \
		PackedVU pack;                                                                 \
		pack.value = value;                                                            \
		rsp_vect_t result = RSP_##op<e>(rsp, pack.vs, pack.vt);                        \
		unsigned vd = pack.vd;                                                         \
		STORE_RESULT();                                                                \
	}                                                                                  \
	template <unsigned e>                                                              \
	static rsp_vect_t JIT_DECL RSP_##op(RSP::CPUState *rsp, unsigned vs, unsigned vt)

// Some VU instructions are basically scalar instructions on lanes
#define IMPL_VU_S(op)                                                                         \
	template <unsigned e>                                                                     \
	void JIT_DECL RSP_##op(RSP::CPUState *rsp, uint32_t);                                     \
	ELEMENT_INSTANTIATE(op, VU_INSTANTIATED)                                                  \
	template <unsigned e>                                                                     \
	static void JIT_DECL RSP_##op(RSP::CPUState *rsp, unsigned vd, unsigned vs, unsigned vt); \
	template <unsigned e>                                                                     \
	void JIT_DECL RSP_##op(RSP::CPUState *rsp, uint32_t value)                                \
	{                                                                                         \
		PackedVU pack;                                                                        \
		pack.value = value;                                                                   \
		RSP_##op<e>(rsp, pack.vd, pack.vs, pack.vt);                                          \
	}                                                                                         \
	template <unsigned e>                                                                     \
	static void JIT_DECL RSP_##op(RSP::CPUState *rsp, unsigned vd, unsigned vs, unsigned vt)

namespace VU
{
static inline int32_t clamp16s(int32_t x)
{
	if (x > 0x7fff)
		return 0x7fff;
	if (x < -0x8000)
		return -0x8000;
	return x;
}

//
// VABS
//
IMPL_VU(VABS)
{
	TRACE_VU(VABS);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo;
	rsp_vect_t result = rsp_vabs(LOAD_VS(), LOAD_VT(), &acc_lo);
	write_acc_lo(acc, acc_lo);
	return result;
}

//
// VADD
//
IMPL_VU(VADD)
{
	TRACE_VU(VADD);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t carry, acc_lo;

	carry = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);
	rsp_vect_t result = rsp_vadd(LOAD_VS(), LOAD_VT(), carry, &acc_lo);

	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, acc_lo);
	return result;
}

//
// VADDC
//
IMPL_VU(VADDC)
{
	TRACE_VU(VADDC);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t sn;

	rsp_vect_t result = rsp_vaddc(LOAD_VS(), LOAD_VT(), rsp_vzero(), &sn);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero()); // TODO: Confirm.
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, sn);
	write_acc_lo(acc, result);
	return result;
}

//
// VAND
// VNAND
//
IMPL_VU(VAND)
{
	TRACE_VU(VAND);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t result = rsp_vand(LOAD_VS(), LOAD_VT());
	write_acc_lo(acc, result);
	return result;
}

IMPL_VU(VNAND)
{
	TRACE_VU(VNAND);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t result = rsp_vnand(LOAD_VS(), LOAD_VT());
	write_acc_lo(acc, result);
	return result;
}

//
// VCH
//
IMPL_VU(VCH)
{
	TRACE_VU(VCH);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t ge, le, sign, eq, vce;

	rsp_vect_t result = rsp_vch(LOAD_VS(), LOAD_VT(), rsp_vzero(), &ge, &le, &eq, &sign, &vce);

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, ge);
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, eq);
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, sign);
	write_vce(rsp->cp2.flags[RSP::RSP_VCE].e, vce);
	write_acc_lo(acc, result);
	return result;
}

//
// VCL
//
IMPL_VU(VCL)
{
	TRACE_VU(VCL);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t ge, le, eq, sign, vce;

	ge = read_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e);
	le = read_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e);
	eq = read_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e);
	sign = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);
	vce = read_vce(rsp->cp2.flags[RSP::RSP_VCE].e);

	rsp_vect_t result = rsp_vcl(LOAD_VS(), LOAD_VT(), rsp_vzero(), &ge, &le, eq, sign, vce);

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, ge);
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vce(rsp->cp2.flags[RSP::RSP_VCE].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

//
// VCR
//
IMPL_VU(VCR)
{
	TRACE_VU(VCR);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t ge, le;

	rsp_vect_t result = rsp_vcr(LOAD_VS(), LOAD_VT(), rsp_vzero(), &ge, &le);

#ifdef INTENSE_DEBUG
	for (unsigned i = 0; i < 8; i++)
		fprintf(stderr, "VD[%d] = %d\n", i, reinterpret_cast<int16_t *>(&result)[i]);
#endif

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, ge);
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vce(rsp->cp2.flags[RSP::RSP_VCE].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

//
// VEQ
// VGE
// VLT
// VNE
//
IMPL_VU(VEQ)
{
	TRACE_VU(VEQ);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t le, eq, sign;

	eq = read_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e);
	sign = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);

	rsp_vect_t result = rsp_veq(LOAD_VS(), LOAD_VT(), rsp_vzero(), &le, eq, sign);

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, rsp_vzero());
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

IMPL_VU(VGE)
{
	TRACE_VU(VGE);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t le, eq, sign;

	eq = read_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e);
	sign = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);

	rsp_vect_t result = rsp_vge(LOAD_VS(), LOAD_VT(), rsp_vzero(), &le, eq, sign);

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, rsp_vzero());
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

IMPL_VU(VLT)
{
	TRACE_VU(VLT);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t le, eq, sign;

	eq = read_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e);
	sign = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);

	rsp_vect_t result = rsp_vlt(LOAD_VS(), LOAD_VT(), rsp_vzero(), &le, eq, sign);

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, rsp_vzero());
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

IMPL_VU(VNE)
{
	TRACE_VU(VNE);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t le, eq, sign;

	eq = read_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e);
	sign = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);

	rsp_vect_t result = rsp_vne(LOAD_VS(), LOAD_VT(), rsp_vzero(), &le, eq, sign);
#ifdef INTENSE_DEBUG
	for (unsigned i = 0; i < 8; i++)
		fprintf(stderr, "VD[%d] = %d\n", i, reinterpret_cast<int16_t *>(&result)[i]);
#endif

	write_vcc_hi(rsp->cp2.flags[RSP::RSP_VCC].e, rsp_vzero());
	write_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e, le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

//
// VINVALID
//
IMPL_VU_S(VINVALID)
{
	fprintf(stderr, "Unimplemented ...\n");
}

//
// VMACF
// VMACU
//
IMPL_VU(VMACF)
{
	TRACE_VU(VMACF);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;
	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmacf_vmacu<false>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU(VMACU)
{
	TRACE_VU(VMACU);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;
	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmacf_vmacu<true>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU_S(VMACQ)
{
	TRACE_VU(VMACQ);
	uint16_t *acc = rsp->cp2.acc.e;
	for (unsigned i = 0; i < 8; i++)
	{
		int32_t prod = (int16_t)acc[i] << 16;
		prod |= acc[8 + i];
		if (prod < 0 && !(prod & 1 << 5))
			prod += 32;
		else if (prod >= 32 && !(prod & 1 << 5))
			prod -= 32;
		acc[i] = prod >> 16;
		acc[8 + i] = prod & 0xffffu;

		rsp->cp2.regs[vd].e[i] = clamp16s(prod >> 1) & ~15;
	}
}

//
// VMADH
// VMUDH
//
IMPL_VU(VMADH)
{
	TRACE_VU(VMADH);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadh_vmudh<true>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU(VMUDH)
{
	TRACE_VU(VMUDH);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadh_vmudh<false>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

//
// VMADL
// VMUDL
//
IMPL_VU(VMADL)
{
	TRACE_VU(VMADL);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadl_vmudl<true>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU(VMUDL)
{
	TRACE_VU(VMUDL);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadl_vmudl<false>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

//
// VMADM
// VMUDM
//
IMPL_VU(VMADM)
{
	TRACE_VU(VMADM);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadm_vmudm<true>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU(VMUDM)
{
	TRACE_VU(VMUDM);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadm_vmudm<false>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

//
// VMADN
// VMUDN
//
IMPL_VU(VMADN)
{
	TRACE_VU(VMADN);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadn_vmudn<true>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU(VMUDN)
{
	TRACE_VU(VMUDN);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	acc_lo = read_acc_lo(acc);
	acc_md = read_acc_md(acc);
	acc_hi = read_acc_hi(acc);

	result = rsp_vmadn_vmudn<false>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

//
// VMOV
//
IMPL_VU_S(VMOV)
{
	TRACE_VU(VMOV);
	uint16_t *acc = rsp->cp2.acc.e;
	write_acc_lo(acc, LOAD_VT());
	vs &= 0x7;
	rsp->cp2.regs[vd].e[vs] = rsp->cp2.acc.e[16 + vs];
}

//
// VMRG
//
IMPL_VU(VMRG)
{
	TRACE_VU(VMRG);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t le;

	le = read_vcc_lo(rsp->cp2.flags[RSP::RSP_VCC].e);
	rsp_vect_t result = rsp_vmrg(LOAD_VS(), LOAD_VT(), le);
	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, result);
	return result;
}

//
// VMULF
// VMULQ
// VMULU
//
IMPL_VU(VMULF)
{
	TRACE_VU(VMULF);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	result = rsp_vmulf_vmulu<false>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

IMPL_VU_S(VMULQ)
{
	TRACE_VU(VMULQ);
	uint16_t *acc = rsp->cp2.acc.e;
	uint16_t *vde = rsp->cp2.regs[vd].e;
	int16_t *vse = (int16_t *)rsp->cp2.regs[vs].e;

	int16_t vte[8];
	rsp_vect_t vtt = LOAD_VT();
	rsp_vect_write_operand((uint16_t *)vte, vtt);

	for (unsigned i = 0; i < 8; i++)
	{
		int32_t prod = vse[i] * vte[i];
		if (prod < 0)
			prod += 31;
		acc[i] = prod >> 16;
		acc[8 + i] = prod & 0xffff;
		acc[16 + i] = 0;
		vde[i] = clamp16s(prod >> 1) & ~15;
	}
}

IMPL_VU(VMULU)
{
	TRACE_VU(VMULU);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t acc_lo, acc_md, acc_hi, result;

	result = rsp_vmulf_vmulu<true>(LOAD_VS(), LOAD_VT(), rsp_vzero(), &acc_lo, &acc_md, &acc_hi);

	write_acc_lo(acc, acc_lo);
	write_acc_md(acc, acc_md);
	write_acc_hi(acc, acc_hi);
	return result;
}

//
// VRNDP
// VRNDN
//
template <unsigned e>
static inline void RSP_VRND(RSP::CPUState *rsp, unsigned vd, unsigned vs, unsigned vt, uint_fast8_t variant)
{
	int16_t vte[8];
	rsp_vect_t vtt = LOAD_VT();
	rsp_vect_write_operand((uint16_t *)vte, vtt);
	uint16_t *acc = rsp->cp2.acc.e;
	uint16_t *vde = rsp->cp2.regs[vd].e;

	for (unsigned i = 0; i < 8; i++)
	{
		int64_t acc48 = ((int64_t)(int16_t)acc[i] << 32) | ((int64_t)acc[8 + i] << 16) | (int64_t)acc[16 + i];

		const uint_fast8_t negative_acc = acc48 < 0;
		if (!!variant xor !!negative_acc)
		{
			int64_t value = (int64_t)(int16_t)vte[i];
			if (vs & 1)
				value <<= 16;
			acc48 += value;
		}

		acc[i] = (acc48 >> 32) & 0xffff;
		acc[8 + i] = (acc48 >> 16) & 0xffff;
		acc[16 + i] = acc48 & 0xffff;
		vde[i] = clamp16s((int32_t)(acc48 >> 16));
	}
}

IMPL_VU_S(VRNDN)
{
	TRACE_VU(RSP_VRNDN);
	RSP_VRND<e>(rsp, vd, vs, vt, 0);
}

IMPL_VU_S(VRNDP)
{
	TRACE_VU(RSP_VRNDP);
	RSP_VRND<e>(rsp, vd, vs, vt, 1);
}

//
// VNOP
//
IMPL_VU_S(VNOP)
{
}

//
// VOR
// VNOR
//
IMPL_VU(VOR)
{
	TRACE_VU(VOR);
	uint16_t *acc = rsp->cp2.acc.e;

	rsp_vect_t result = rsp_vor(LOAD_VS(), LOAD_VT());

	write_acc_lo(acc, result);
	return result;
}

IMPL_VU(VNOR)
{
	TRACE_VU(VNOR);
	uint16_t *acc = rsp->cp2.acc.e;

	rsp_vect_t result = rsp_vnor(LOAD_VS(), LOAD_VT());

	write_acc_lo(acc, result);
	return result;
}

//
// VRCP
// VRCPL
// VRSQ
// VRSQL
//
IMPL_VU_S(VRCP)
{
	TRACE_VU(VRCP);
	uint16_t *acc = rsp->cp2.acc.e;
	unsigned de = vs & 0x7;

	write_acc_lo(acc, LOAD_VT());

	rsp->cp2.dp_flag = 0;
	return rsp_vrcp_vrsq<false>(rsp, 0, vt, e, vd, de);
}

IMPL_VU_S(VRCPL)
{
	TRACE_VU(VRCPL);
	uint16_t *acc = rsp->cp2.acc.e;
	unsigned de = vs & 0x7;

	write_acc_lo(acc, LOAD_VT());

	int dp = rsp->cp2.dp_flag & 1;
	rsp->cp2.dp_flag = 0;

	return rsp_vrcp_vrsq<false>(rsp, dp, vt, e, vd, de);
}

IMPL_VU_S(VRSQ)
{
	TRACE_VU(VRSQ);
	uint16_t *acc = rsp->cp2.acc.e;
	unsigned de = vs & 0x7;

	write_acc_lo(acc, LOAD_VT());

	rsp->cp2.dp_flag = 0;
	return rsp_vrcp_vrsq<true>(rsp, 0, vt, e, vd, de);
}

IMPL_VU_S(VRSQL)
{
	TRACE_VU(VRSQL);
	uint16_t *acc = rsp->cp2.acc.e;
	unsigned de = vs & 0x7;

	write_acc_lo(acc, LOAD_VT());

	int dp = rsp->cp2.dp_flag & 1;
	rsp->cp2.dp_flag = 0;

	return rsp_vrcp_vrsq<true>(rsp, dp, vt, e, vd, de);
}

//
// VRCPH
// VRSQH
//
IMPL_VU_S(VRCPH)
{
	TRACE_VU(VRCPH);
	uint16_t *acc = rsp->cp2.acc.e;
	unsigned de = vs & 0x7;

	write_acc_lo(acc, LOAD_VT());

	// Specify double-precision for VRCPL on the next pass.
	rsp->cp2.dp_flag = 1;

	return rsp_vdivh(rsp, vt, e, vd, de);
}

IMPL_VU_S(VRSQH)
{
	TRACE_VU(VRSQH);
	uint16_t *acc = rsp->cp2.acc.e;
	unsigned de = vs & 0x7;

	write_acc_lo(acc, LOAD_VT());

	// Specify double-precision for VRCPL on the next pass.
	rsp->cp2.dp_flag = 1;

	return rsp_vdivh(rsp, vt, e, vd, de);
}

//
// VSAR
//
IMPL_VU(VSAR)
{
	TRACE_VU(VSAR);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t result;

	switch (e)
	{
	case 8:
		result = read_acc_hi(acc);
		break;
	case 9:
		result = read_acc_md(acc);
		break;
	case 10:
		result = read_acc_lo(acc);
		break;
	default:
		result = rsp_vzero();
		break;
	}

	return result;
}

//
// VSUB
//
IMPL_VU(VSUB)
{
	TRACE_VU(VSUB);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t carry, acc_lo;

	carry = read_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e);

	rsp_vect_t result = rsp_vsub(LOAD_VS(), LOAD_VT(), carry, &acc_lo);

	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, rsp_vzero());
	write_acc_lo(acc, acc_lo);
	return result;
}

//
// VSUBC
//
IMPL_VU(VSUBC)
{
	TRACE_VU(VSUBC);
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t eq, sn;

	rsp_vect_t result = rsp_vsubc(LOAD_VS(), LOAD_VT(), rsp_vzero(), &eq, &sn);

	write_vco_hi(rsp->cp2.flags[RSP::RSP_VCO].e, eq);
	write_vco_lo(rsp->cp2.flags[RSP::RSP_VCO].e, sn);
	write_acc_lo(acc, result);
	return result;
}

//
// VXOR
// VNXOR
//
IMPL_VU(VXOR)
{
	TRACE_VU(VXOR);
	uint16_t *acc = rsp->cp2.acc.e;

	rsp_vect_t result = rsp_vxor(LOAD_VS(), LOAD_VT());

	write_acc_lo(acc, result);
	return result;
}

IMPL_VU(VNXOR)
{
	TRACE_VU(VXNOR);
	uint16_t *acc = rsp->cp2.acc.e;

	rsp_vect_t result = rsp_vnxor(LOAD_VS(), LOAD_VT());

	write_acc_lo(acc, result);
	return result;
}

// RESERVED
IMPL_VU(RESERVED)
{
	uint16_t *acc = rsp->cp2.acc.e;
	rsp_vect_t result = _mm_add_epi16(LOAD_VS(), LOAD_VT());
	write_acc_lo(acc, result);

	result = rsp_vzero();
	return result;
}
} // namespace VU
