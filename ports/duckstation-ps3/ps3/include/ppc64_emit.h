#ifndef DUCKSTATION_PS3_PPC64_EMIT_H
#define DUCKSTATION_PS3_PPC64_EMIT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct dps3_ppc64_emitter {
  uint8_t *data;
  size_t capacity;
  size_t size;
  int error;
} dps3_ppc64_emitter;

void dps3_ppc64_init(dps3_ppc64_emitter *e, void *buffer, size_t capacity);
int dps3_ppc64_ok(const dps3_ppc64_emitter *e);
int dps3_ppc64_addi(dps3_ppc64_emitter *e, unsigned rt, unsigned ra, int simm16);
int dps3_ppc64_blr(dps3_ppc64_emitter *e);

#ifdef __cplusplus
}
#endif
#endif
