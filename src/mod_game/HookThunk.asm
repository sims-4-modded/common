; ****************************************************************************
; Copyright (c) 2026 Sims 4 mods developers
; ****************************************************************************

; The x64 thunks of the hooks. The game jumps to a thunk in place of a hooked
; function. The thunk keeps every argument of the function (the integer
; registers, the vector registers and the stack arguments) and gives them to
; the original function with no change. Thus a function with more than four
; arguments or with a floating-point argument keeps all of its arguments, and a
; floating-point result comes back with no change.
;
; One stub for each hook place loads the place index and goes to the common
; thunk. The common thunk saves the argument registers, calls the C++ function
; HookDispatchBefore for the BEFORE callbacks, restores the registers, and jumps
; to the MinHook trampoline. The original function then runs on the stack of the
; caller, so its stack arguments stay in place.
;
; When an AFTER callback must run, the common thunk puts the address of the
; return thunk in place of the return address of the call. The original function
; returns into the return thunk, which saves the result, calls the C++ function
; HookDispatchAfter for the AFTER callbacks, and goes to the real return address.

OPTION CASEMAP:NONE

EXTERN HookDispatchBefore:PROC
EXTERN HookDispatchAfter:PROC

.code

; The original function returns here when an AFTER callback must run. The result
; is in RAX (an integer or a pointer) and in XMM0 (a floating-point value). The
; stack holds the frame of the caller, so RSP is the value just after the ret.
HookReturnThunk PROC FRAME
    push rbp
    .pushreg rbp
    sub rsp, 48h
    .allocstack 48h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog

    mov [rbp+30h], rax         ; keep the integer result
    movdqu [rbp+20h], xmm0     ; keep the floating-point result

    mov rcx, rax               ; HookDispatchAfter(result)
    call HookDispatchAfter
    mov r11, rax               ; the real return address

    mov rax, [rbp+30h]         ; restore the result
    movdqu xmm0, [rbp+20h]
    lea rsp, [rbp+48h]
    pop rbp
    jmp r11
HookReturnThunk ENDP

; The common thunk. R10 holds the hook place index from the stub. The stack and
; the registers hold the arguments of the call, exactly as the caller gave them.
HookThunkCommon PROC FRAME
    push rbp
    .pushreg rbp
    sub rsp, 0A0h
    .allocstack 0A0h
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog

    ; keep the integer argument registers as a contiguous array
    mov [rbp+20h], rcx
    mov [rbp+28h], rdx
    mov [rbp+30h], r8
    mov [rbp+38h], r9
    ; keep the first four vector argument registers
    movdqu [rbp+40h], xmm0
    movdqu [rbp+50h], xmm1
    movdqu [rbp+60h], xmm2
    movdqu [rbp+70h], xmm3
    mov [rbp+80h], r10         ; the place index

    ; HookDispatchBefore(index, savedArgs, realReturn, &needAfter)
    mov rcx, r10
    lea rdx, [rbp+20h]
    mov r8, [rbp+0A8h]
    lea r9, [rbp+88h]
    call HookDispatchBefore
    mov [rbp+90h], rax         ; the MinHook trampoline

    test rax, rax
    jz ReturnZero

    ; restore the arguments for the original function
    mov rcx, [rbp+20h]
    mov rdx, [rbp+28h]
    mov r8, [rbp+30h]
    mov r9, [rbp+38h]
    movdqu xmm0, [rbp+40h]
    movdqu xmm1, [rbp+50h]
    movdqu xmm2, [rbp+60h]
    movdqu xmm3, [rbp+70h]
    mov r11, [rbp+90h]
    mov eax, [rbp+88h]         ; needAfter
    test eax, eax
    jnz NeedAfter

    ; no AFTER callback: jump to the trampoline with the stack of the caller
    lea rsp, [rbp+0A0h]
    pop rbp
    jmp r11

NeedAfter:
    ; put the return thunk in place of the return address, then jump
    lea rax, [HookReturnThunk]
    mov [rbp+0A8h], rax
    lea rsp, [rbp+0A0h]
    pop rbp
    jmp r11

ReturnZero:
    ; the original function is gone: return 0 to the caller
    xor eax, eax
    lea rsp, [rbp+0A0h]
    pop rbp
    ret
HookThunkCommon ENDP

; One stub for each hook place. The stub loads its place index and goes to the
; common thunk. The stub does not change the stack, so it is a leaf.
MAKESTUB MACRO idx
HookStub_&idx&:
    mov r10d, idx
    jmp HookThunkCommon
ENDM

_ts4_stub = 0
REPT 64
    MAKESTUB %_ts4_stub
    _ts4_stub = _ts4_stub + 1
ENDM

; The table of the stub addresses. The C++ code gives one stub to MinHook for
; each hook place. The number of stubs is the same as k_MaxHooks.
.data
ALIGN 8
PUBLIC HookThunkTable
HookThunkTable LABEL QWORD
MAKEENTRY MACRO idx
    QWORD HookStub_&idx&
ENDM
_ts4_entry = 0
REPT 64
    MAKEENTRY %_ts4_entry
    _ts4_entry = _ts4_entry + 1
ENDM

END
