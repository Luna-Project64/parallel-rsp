#pragma once

#ifndef __x86_64__
#define HAS_FASTCALL
#endif

#define JIT_DECL __fastcall
#define JIT_VECTORDECL __vectorcall

#ifdef HAS_FASTCALL
#define JIT_UA_DECL __stdcall
#else
#define JIT_UA_DECL JIT_DECL
#endif
