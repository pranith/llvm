; Check the function call in PIC relocation model.

; If the relocation model is PIC, then the "bl" instruction for the function
; call to the external function should come with PLT fixup type.

; RUN:  llc < %s -mtriple=armv7-unknown-linux-gnueabi \
; RUN:           -relocation-model=pic -fast-isel \
; RUN:    | FileCheck %s

define void @test() {
entry:

  %0 = call i32 @get()
; CHECK: bl get(PLT)

  call void @put(i32 %0)
; CHECK: bl put(PLT)

  ret void
}

declare i32 @get()
declare void @put(i32)
