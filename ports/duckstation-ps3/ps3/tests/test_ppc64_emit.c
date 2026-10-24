#include "ppc64_emit.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
int main(void)
{
  uint8_t bytes[8];
  static const uint8_t expected[] = {0x38,0x63,0xff,0xef,0x4e,0x80,0x00,0x20};
  dps3_ppc64_emitter e;
  memset(bytes, 0xcc, sizeof(bytes));
  dps3_ppc64_init(&e, bytes, sizeof(bytes));
  CHECK(dps3_ppc64_addi(&e, 3, 3, -17));
  CHECK(dps3_ppc64_blr(&e));
  CHECK(dps3_ppc64_ok(&e));
  CHECK(e.size == sizeof(expected));
  CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
  dps3_ppc64_init(&e, bytes, 2);
  CHECK(!dps3_ppc64_addi(&e, 3, 3, 1));
  CHECK(!dps3_ppc64_ok(&e));
  dps3_ppc64_init(&e, bytes, sizeof(bytes));
  CHECK(!dps3_ppc64_addi(&e, 32, 3, 1));
  CHECK(!dps3_ppc64_ok(&e));
  if (failures) return 1;
  puts("PPC64 emitter tests: PASS");
  return 0;
}
