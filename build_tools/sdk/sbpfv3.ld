/* Derived from Anza rust sbf_base.rs, commit
 * 3175de2636b6e1145b7655fd4131095e23452137 (V3_LINKER_SCRIPT).
 * Apache-2.0 / MIT upstream; see build_tools/sdk/README.md.
 */
SECTIONS
{
  .rodata 0x000000000 : SUBALIGN(8) {
    BYTE(0);
    *(.rodata*)
    *(.data.rel.ro*)
    . = ALIGN(8);
  } :rodata
  .text 0x100000000 : {
    *(.text*)
  } :text
  /DISCARD/ : {
    *(.comment*)
    *(.eh_frame*)
    *(*hash*)
    *(.bss*)
    *(.data*)
    *(.rel.dyn*)
    *(.dynamic)
    *(.dynsym)
    *(.dynstr)
  }
}
PHDRS
{
  rodata PT_LOAD FLAGS(4);
  text PT_LOAD FLAGS(1);
  other PT_NULL FLAGS(0);
}
