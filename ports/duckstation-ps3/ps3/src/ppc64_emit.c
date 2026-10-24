#include "ppc64_emit.h"
#include <limits.h>

static void write_be32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static int fail(dps3_ppc64_emitter *e, int code)
{
  if (e && e->error == 0) e->error = code;
  return 0;
}

static int emit_word(dps3_ppc64_emitter *e, uint32_t word)
{
  if (!e || !e->data) return 0;
  if (e->error) return 0;
  if (e->size > e->capacity || e->capacity - e->size < 4) return fail(e, 1);
  write_be32(e->data + e->size, word);
  e->size += 4;
  return 1;
}

void dps3_ppc64_init(dps3_ppc64_emitter *e, void *buffer, size_t capacity)
{
  if (!e) return;
  e->data = (uint8_t *)buffer;
  e->capacity = capacity;
  e->size = 0;
  e->error = buffer ? 0 : 2;
}

int dps3_ppc64_ok(const dps3_ppc64_emitter *e) { return e && e->error == 0; }

int dps3_ppc64_addi(dps3_ppc64_emitter *e, unsigned rt, unsigned ra, int imm)
{
  if (rt > 31 || ra > 31 || imm < INT16_MIN || imm > INT16_MAX) return fail(e, 3);
  return emit_word(e, 0x38000000u | (rt << 21) | (ra << 16) | (uint16_t)imm);
}

int dps3_ppc64_blr(dps3_ppc64_emitter *e) { return emit_word(e, 0x4e800020u); }
