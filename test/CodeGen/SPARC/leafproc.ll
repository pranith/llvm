; RUN: llc -march=sparc -disable-sparc-leaf-proc=0 < %s | FileCheck %s

; CHECK:      func_nobody:
; CHECK:      jmp %o7+8
; CHECK-NEXT: nop
define void @func_nobody() {
entry:
  ret void
}


; CHECK:      return_int_const:
; CHECK:      jmp %o7+8
; CHECK-NEXT: or %g0, 1729, %o0
define i32 @return_int_const() {
entry:
  ret i32 1729
}

; CHECK:      return_double_const:
; CHECK:      sethi
; CHECK:      jmp %o7+8
; CHECK-NEXT: ldd {{.*}}, %f0

define double @return_double_const() {
entry:
  ret double 0.000000e+00
}

; CHECK:      leaf_proc_with_args:
; CHECK:      add {{%o[0-1]}}, {{%o[0-1]}}, [[R:%[go][0-7]]]
; CHECK:      jmp %o7+8
; CHECK-NEXT: add [[R]], %o2, %o0

define i32 @leaf_proc_with_args(i32 %a, i32 %b, i32 %c) {
entry:
  %0 = add nsw i32 %b, %a
  %1 = add nsw i32 %0, %c
  ret i32 %1
}

; CHECK:     leaf_proc_with_args_in_stack:
; CHECK-DAG: ld [%sp+92], {{%[go][0-7]}}
; CHECK-DAG: ld [%sp+96], {{%[go][0-7]}}
; CHECK:     jmp %o7+8
; CHECK-NEXT: add {{.*}}, %o0
define i32 @leaf_proc_with_args_in_stack(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h) {
entry:
  %0 = add nsw i32 %b, %a
  %1 = add nsw i32 %0, %c
  %2 = add nsw i32 %1, %d
  %3 = add nsw i32 %2, %e
  %4 = add nsw i32 %3, %f
  %5 = add nsw i32 %4, %g
  %6 = add nsw i32 %5, %h
  ret i32 %6
}