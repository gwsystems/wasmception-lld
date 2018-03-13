# RUN: llvm-mc -filetype=obj -triple=i686-unknown-linux /dev/null -o %t.o
# RUN: ld.lld -z wxneeded --script %s %t.o -o %t
# RUN: llvm-readobj --program-headers %t | FileCheck %s

PHDRS { text PT_LOAD FILEHDR PHDRS; wxneeded PT_OPENBSD_WXNEEDED; }

# CHECK:      ProgramHeader {
# CHECK:        Type: PT_OPENBSD_WXNEEDED (0x65A3DBE7)
# CHECK-NEXT:   Offset: 0x0
# CHECK-NEXT:   VirtualAddress: 0x0
# CHECK-NEXT:   PhysicalAddress: 0x0
# CHECK-NEXT:   FileSize: 0
# CHECK-NEXT:   MemSize: 0
# CHECK-NEXT:   Flags [
# CHECK-NEXT:     PF_R
# CHECK-NEXT:   ]
# CHECK-NEXT:   Alignment: 0
# CHECK-NEXT: }
