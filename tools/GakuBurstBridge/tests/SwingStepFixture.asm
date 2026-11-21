EXTERN GbbSwingVelocityStored:PROC
.code
GbbRunSwingStepFixture PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp, 32
    .allocstack 32
    movaps [rsp], xmm10
    .savexmm128 xmm10,0
    movaps [rsp+16], xmm11
    .savexmm128 xmm11,16
    .endprolog
    mov rbx, rcx
    test r9d, r9d
    jz legacy
    vmovups ymm0, [rdx]
    vmovaps ymm10, ymm0
    jmp setup
legacy:
    movups xmm0, [rdx]
    movaps xmm11, xmm0
setup:
    mov eax, 7
    cmp eax, 7
    jmp GbbSwingVelocityStored
GbbResumeSwingStepFixture LABEL NEAR
PUBLIC GbbResumeSwingStepFixture
    jne bad
    cmp eax, 7
    jne bad
    test r9d, r9d
    jz legacyOutput
    vmovups [r8], ymm0
    vmovups [r8+32], ymm10
    vzeroupper
    jmp done
legacyOutput:
    movups [r8], xmm0
    movups [r8+32], xmm11
done:
    mov eax, 1
    jmp restore
bad:
    xor eax, eax
restore:
    movaps xmm10, [rsp]
    movaps xmm11, [rsp+16]
    add rsp, 32
    pop rbx
    ret
GbbRunSwingStepFixture ENDP
END
