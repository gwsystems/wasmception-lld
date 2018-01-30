; REQUIRES: x86
; RUN: llvm-as %s -o %t.o

; RUN: ld.lld %t.o -o %t.so -shared
; RUN: llvm-objdump -d -section=".text" -no-leading-addr -no-show-raw-insn %t.so | FileCheck %s

; RUN: ld.lld -mllvm -mcpu=znver1 %t.o -o %m.so -shared
; RUN: llvm-objdump -d -section=".text" -no-leading-addr -no-show-raw-insn %m.so | FileCheck -check-prefix=ZNVER1 %s

; CHECK: nop{{$}}

; ZNVER1: nopw

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @foo() #0 {
entry:
  call void asm sideeffect ".p2align        4, 0x90", "~{dirflag},~{fpsr},~{flags}"()
  ret void
}

attributes #0 = { "no-frame-pointer-elim"="true" }
