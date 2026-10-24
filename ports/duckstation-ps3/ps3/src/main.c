#include "ps3_jit_probe.h"

#include <errno.h>
#include <ppu-types.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/process.h>
#include <sys/stat.h>
#include <sys/tty.h>

SYS_PROCESS_PARAM(1001, 0x100000);

#define DATA_DIR "/dev_hdd0/duckstation-ps3"
#define LOG_NAME "phase0-jit-probe.log"

static const char *const log_paths[] = {
  "/dev_hdd0/game/DCKS00001/USRDIR/" LOG_NAME,
  DATA_DIR "/" LOG_NAME,
  "/dev_hdd0/tmp/" LOG_NAME,
  "/dev_hdd0/" LOG_NAME
};

static void tty_write(const char *s)
{
  u32 written = 0;
  (void)sysTtyWrite(0, s, (u32)strlen(s), &written);
}

static FILE *open_log(char *selected, size_t selected_size)
{
  FILE *files[sizeof(log_paths) / sizeof(log_paths[0])] = {0};
  (void)mkdir(DATA_DIR, 0777);
  (void)mkdir("/dev_hdd0/tmp", 0777);

  for (size_t i = 0; i < sizeof(log_paths) / sizeof(log_paths[0]); ++i) {
    files[i] = fopen(log_paths[i], "w");
    if (files[i]) {
      snprintf(selected, selected_size, "%s", log_paths[i]);
      /* Write the same diagnostic to every writable location, not just the first. */
      for (size_t j = i + 1; j < sizeof(log_paths) / sizeof(log_paths[0]); ++j)
        files[j] = fopen(log_paths[j], "w");
      return files[i];
    }
  }
  selected[0] = '\0';
  return NULL;
}

static void log_all(FILE *primary, const char *fmt, ...)
{
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  tty_write(line);
  tty_write("\n");
  if (primary) {
    fprintf(primary, "%s\n", line);
    fflush(primary);
    /* Mirror the line into other writable candidates where possible. */
    for (size_t i = 1; i < sizeof(log_paths) / sizeof(log_paths[0]); ++i) {
      FILE *f = fopen(log_paths[i], "a");
      if (f) {
        fprintf(f, "%s\n", line);
        fflush(f);
        fclose(f);
      }
    }
  }
}

int main(void)
{
  char log_path[256];
  FILE *log;

  /* Direct TTY marker happens before any filesystem calls. */
  tty_write("DuckStation PS3 Phase 0: main() entered\n");
  log = open_log(log_path, sizeof(log_path));

  log_all(log, "DuckStation PS3 port / phase 0");
  log_all(log, "startup: SYS_PROCESS_PARAM present; pid logging active");
  log_all(log, "log selected=%s errno=%d", log_path[0] ? log_path : "(no writable path)", errno);
  log_all(log, "PPE PPC64 JIT cache execution probe");
  log_all(log, "status=ABOUT_TO_RUN_JIT");

  /* If this step faults/hangs, the startup marker above should remain on disk. */
  {
    int32_t actual = 0;
    const int32_t input = 1234;
    const int16_t delta = -17;
    int rc = duckstation_ps3_jit_probe(input, delta, &actual);
    log_all(log, "JIT probe: rc=%d input=%ld delta=%d result=%ld expected=%ld",
            rc, (long)input, (int)delta, (long)actual, (long)(input + delta));
    log_all(log, "status=%s", rc == 0 ? "PASS" : "FAIL");
    if (log)
      fclose(log);
    return rc == 0 ? 0 : 1;
  }
}
