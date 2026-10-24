#include "ps3_jit_probe.h"
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#define DATA_DIR "/dev_hdd0/duckstation-ps3"
#define LOG_PATH DATA_DIR "/phase0-jit-probe.log"

static FILE *open_log(void)
{
  (void)mkdir(DATA_DIR, 0777);
  return fopen(LOG_PATH, "w");
}

int main(void)
{
  FILE *log = open_log();
  FILE *out = log ? log : stdout;
  int32_t actual = 0;
  const int32_t input = 1234;
  const int16_t delta = -17;
  int rc;

  fprintf(out, "DuckStation PS3 port / phase 0\n");
  fprintf(out, "PPE PPC64 JIT cache execution probe\n");
  fflush(out);
  rc = duckstation_ps3_jit_probe(input, delta, &actual);
  fprintf(out, "JIT probe: rc=%d input=%ld delta=%d result=%ld expected=%ld\n",
          rc, (long)input, (int)delta, (long)actual, (long)(input + delta));
  fprintf(out, "status=%s\n", rc == 0 ? "PASS" : "FAIL");
  fflush(out);
  if (log) fclose(log);
  return rc == 0 ? 0 : 1;
}
