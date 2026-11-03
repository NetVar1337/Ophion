; AsmHostIdt.asm
; private host IDT handlers for VMX-root mode

PUBLIC asm_host_nmi_handler
PUBLIC asm_host_df_handler
PUBLIC asm_host_gp_handler
PUBLIC asm_host_pf_handler
PUBLIC asm_host_default_handler

EXTERN g_host_nmi_pending:DWORD
EXTERN hostcr3_pf_sync:PROC

.code _text


; NMI handler — sets per-cpu pending flag, vmexit handler injects to guest

asm_host_nmi_handler PROC

    push    rax
    push    rcx

    ; get cpu id from IA32_TSC_AUX (no memory access needed)
    mov     ecx, 0C0000103h
    rdmsr
    and     eax, 0FFFh

    lea     rcx, g_host_nmi_pending
    mov     dword ptr [rcx + rax*4], 1

    pop     rcx
    pop     rax
    iretq

asm_host_nmi_handler ENDP


; #DF in host mode = unrecoverable, halt

asm_host_df_handler PROC

    cli
    hlt
    jmp     $

asm_host_df_handler ENDP


; #GP in host mode = bug, halt

asm_host_gp_handler PROC

    cli
    hlt
    jmp     $

asm_host_gp_handler ENDP


; #PF in host mode: hostcr3_pf_sync() copies a missing top-level kernel
; PML4 entry from the system page tables into the private host page tables
; (for kernel VA space created after the snapshot) and we resume.
; anything else is unrecoverable -> halt.
; entry stack: error_code, rip, cs, rflags

asm_host_pf_handler PROC

    push    rax
    push    rcx
    push    rdx
    push    r8
    push    r9
    push    r10
    push    r11
    push    rbp
    mov     rbp, rsp
    and     rsp, -16               ; align for the C call
    sub     rsp, 060h              ; 020h shadow space + 040h xmm save
    movaps  xmmword ptr [rsp+020h], xmm0
    movaps  xmmword ptr [rsp+030h], xmm1
    movaps  xmmword ptr [rsp+040h], xmm2
    movaps  xmmword ptr [rsp+050h], xmm3
    movaps  xmmword ptr [rsp+060h], xmm4
    movaps  xmmword ptr [rsp+070h], xmm5

    mov     rcx, cr2
    call    hostcr3_pf_sync
    movzx   r10d, al                ; save result (r10 is saved above)

    movaps  xmm0, xmmword ptr [rsp+020h]
    movaps  xmm1, xmmword ptr [rsp+030h]
    movaps  xmm2, xmmword ptr [rsp+040h]
    movaps  xmm3, xmmword ptr [rsp+050h]
    movaps  xmm4, xmmword ptr [rsp+060h]
    movaps  xmm5, xmmword ptr [rsp+070h]

    mov     rsp, rbp
    pop     rbp

    test    r10d, r10d
    jz      pf_halt

    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdx
    pop     rcx
    pop     rax
    add     rsp, 08h                ; discard #PF error code
    iretq

pf_halt:
    cli
    hlt
    jmp     $

asm_host_pf_handler ENDP


; catch-all for unexpected vectors, halt

asm_host_default_handler PROC

    cli
    hlt
    jmp     $

asm_host_default_handler ENDP


END
