; Read-only probes at verified instruction boundaries inside ProcessDynamicBones.
; The native instructions run from MinHook's trampoline with their inputs intact.
EXTERN GbbSwingStepActive:DWORD
EXTERN GbbSwingStepAvx:DWORD
EXTERN GbbSwingStepTrampolines:QWORD
EXTERN GbbObserveSwingStep:PROC

.code

SWING_STEP MACRO entryName, phase
    LOCAL capture, legacySave, invokeObserver, legacyRestore, restore
    entryName PROC
        pushfq
        cmp DWORD PTR [GbbSwingStepActive], 0
        jne capture
        popfq
        jmp QWORD PTR [GbbSwingStepTrampolines + phase * 8]
    capture:
        popfq
        jmp entryName&_Save
    entryName ENDP

    entryName&_Save PROC FRAME
        pushfq
        .allocstack 8
        push rax
        .allocstack 8
        push rcx
        .allocstack 8
        push rdx
        .allocstack 8
        push r8
        .allocstack 8
        push r9
        .allocstack 8
        push r10
        .allocstack 8
        push r11
        .allocstack 8
        sub rsp, 960
        .allocstack 960
        .endprolog
        lea r10, [rsp+127]
        and r10, -64
        cmp DWORD PTR [GbbSwingStepAvx], 0
        je legacySave
        xor eax, eax
        mov [r10+512], rax
        mov [r10+520], rax
        mov [r10+528], rax
        mov [r10+536], rax
        mov [r10+544], rax
        mov [r10+552], rax
        mov [r10+560], rax
        mov [r10+568], rax
        mov eax, 7
        xor edx, edx
        xsave64 [r10]
        ; XSAVE can omit initialized components. FXSAVE writes lower lanes
        ; unconditionally for the observation, into the same legacy region.
        fxsave64 [r10]
        movzx eax, r12b
        jmp invokeObserver
    legacySave:
        fxsave64 [r10]
        movzx eax, sil
    invokeObserver:
        mov [rsp+32], eax
        mov ecx, phase
        mov rdx, rbx
        mov r8, rbp
        lea r9, [r10+160]
        call GbbObserveSwingStep
        lea r10, [rsp+127]
        and r10, -64
        cmp DWORD PTR [GbbSwingStepAvx], 0
        je legacyRestore
        mov eax, 7
        xor edx, edx
        xrstor64 [r10]
        jmp restore
    legacyRestore:
        fxrstor64 [r10]
    restore:
        add rsp, 960
        pop r11
        pop r10
        pop r9
        pop r8
        pop rdx
        pop rcx
        pop rax
        popfq
        jmp QWORD PTR [GbbSwingStepTrampolines + phase * 8]
    entryName&_Save ENDP
ENDM

SWING_STEP GbbSwingAnchorReady, 0
SWING_STEP GbbSwingForceGate, 1
SWING_STEP GbbSwingVelocityStored, 2
SWING_STEP GbbSwingContactResult, 3
SWING_STEP GbbSwingTranslationStored, 4
END
