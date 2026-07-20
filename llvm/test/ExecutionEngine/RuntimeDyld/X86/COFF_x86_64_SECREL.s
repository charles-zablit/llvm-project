# RUN: rm -rf %t && mkdir -p %t
# RUN: llvm-mc -triple=x86_64-pc-win32 -filetype=obj -o %t/COFF_x86_64_SECREL.o %s
# RUN: llvm-rtdyld -triple=x86_64-pc-win32 -verify -check=%s %t/COFF_x86_64_SECREL.o
#
# Verify IMAGE_REL_AMD64_SECREL. The relocated 32-bit value must be the
# section-relative offset of the target symbol PLUS the addend stored in the
# relocated field. RuntimeDyld previously ignored the in-field addend for
# SECREL, which silently zeroed DWARF DW_FORM_strp / DW_FORM_sec_offset values
# in JIT'd debug info: they are emitted as SECREL relocations against a debug
# section symbol (section offset 0) with the real offset carried as the addend.

	.section	.rdata,"dr"
sec_base:                               # section-relative offset 0
	.zero	0x10
target:                                 # section-relative offset 0x10
	.long	0

	.data
	.globl	relocations
relocations:

# (1) SECREL to a symbol at section offset 0x10, no addend => 0x10.
sr_symoff:
	.secrel32	target
# rtdyld-check: *{4}sr_symoff = 0x10

# (2) SECREL to a section-offset-0 symbol with an in-field addend => the addend.
#     This mirrors the JIT'd-DWARF case RuntimeDyld got wrong (it wrote 0).
sr_addend:
	.secrel32	sec_base+0x2a
# rtdyld-check: *{4}sr_addend = 0x2a

# (3) SECREL combining a nonzero symbol offset and a nonzero addend => the sum.
sr_both:
	.secrel32	target+0x2a
# rtdyld-check: *{4}sr_both = 0x3a
