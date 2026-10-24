/* The cache reserves runtime executable memory, without bloating EBOOT. */
SECTIONS
{
  .duckstation_ps3_jit (NOLOAD) : ALIGN(64K)
  {
    . = ALIGN(1M);
    KEEP(*(.duckstation_ps3_jit))
  } : hdr_text
}
INSERT AFTER .sys_proc_prx_param;
