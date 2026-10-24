#include "ps3_jit_probe.h"
#include "ppc64_emit.h"
#include <stddef.h>
#include <stdint.h>

#if !defined(__powerpc64__) && !defined(__powerpc__) && !defined(__PPC64__) && !defined(__powerpc64)
#error "PS3 PPU-only test; run test_ppc64_emit.c for host-side tests"
#endif

#define DUCKSTATION_PS3_JIT_CACHE_BYTES (4u * 1024u * 1024u)
__asm__(
  ".section .duckstation_ps3_jit,\"ax\",@nobits\n"
  ".balign 65536\n"
  ".globl duckstation_ps3_jit_cache\n"
  "duckstation_ps3_jit_cache:\n"
  ".space 4194304\n"
  ".previous\n");
extern uint8_t duckstation_ps3_jit_cache[];

typedef struct duckstation_ps3_fn_descriptor {
  uintptr_t entry;
  uintptr_t toc;
  uintptr_t environment;
} duckstation_ps3_fn_descriptor;

static void flush_instruction_cache(const void *begin, size_t size)
{
  uintptr_t start = (uintptr_t)begin & ~(uintptr_t)127;
  uintptr_t end = (uintptr_t)begin + size;
  uintptr_t p;
  for (p = start; p < end; p += 128)
    __asm__ volatile("dcbst 0,%0" : : "r"(p) : "memory");
  __asm__ volatile("sync" : : : "memory");
  for (p = start; p < end; p += 128)
    __asm__ volatile("icbi 0,%0" : : "r"(p) : "memory");
  __asm__ volatile("sync\n\tisync" : : : "memory");
}

int duckstation_ps3_jit_probe(int32_t input, int16_t delta, int32_t *output)
{
  uint8_t *code = duckstation_ps3_jit_cache;
  dps3_ppc64_emitter e;
  duckstation_ps3_fn_descriptor descriptor;
  uintptr_t toc;
  int (*generated_fn)(int);
  int result;

  if (!output) return -1;
  if (DUCKSTATION_PS3_JIT_CACHE_BYTES < 64u) return -4;
  dps3_ppc64_init(&e, code, 64);
  if (!dps3_ppc64_addi(&e, 3, 3, delta) || !dps3_ppc64_blr(&e) || !dps3_ppc64_ok(&e)) return -2;

  flush_instruction_cache(code, e.size);
  __asm__ volatile("mr %0,2" : "=r"(toc));
  descriptor.entry = (uintptr_t)code;
  descriptor.toc = toc;
  descriptor.environment = 0;
  __asm__ volatile("" : : : "memory");
  generated_fn = (int (*)(int))(void *)&descriptor;
  result = generated_fn((int)input);
  *output = (int32_t)result;
  return result == input + delta ? 0 : -3;
}
