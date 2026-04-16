; ============================================================================
; INDIRECT SYSCALL BRIDGE (x64)
; Lightweight bridge layer for direct syscall dispatch.
; ============================================================================

PUBLIC IndirectSyscall
PUBLIC HellsGate
PUBLIC HalosGate
PUBLIC FindSyscallAddress
PUBLIC BuildIndirectSyscall
PUBLIC NtAllocateVirtualMemory_Indirect
PUBLIC NtProtectVirtualMemory_Indirect
PUBLIC NtCreateThreadEx_Indirect

.code

; RCX = syscall number, RDX = address of ntdll syscall stub
IndirectSyscall PROC
    mov r10, rcx
    mov eax, ecx
    jmp qword ptr [rdx]
IndirectSyscall ENDP

; Reserved for compatibility with staged call sites.
HellsGate PROC
    xor eax, eax
    ret
HellsGate ENDP

; Resolve nearby syscall number from a known/candidate stub in ntdll.
HalosGate PROC
    push rbx
    push rdi

    mov rdi, rcx
    xor ebx, ebx

scan_forward:
    cmp byte ptr [rdi], 4Ch
    jne scan_continue
    cmp byte ptr [rdi+1], 8Bh
    jne scan_continue
    cmp byte ptr [rdi+2], 0D1h
    jne scan_continue
    cmp byte ptr [rdi+3], 0B8h
    jne scan_continue

    movzx eax, word ptr [rdi+4]
    jmp halos_done

scan_continue:
    inc ebx
    cmp ebx, 0Ah
    jg scan_backward

    lea rdi, [rdi + 20h]
    jmp scan_forward

scan_backward:
    dec ebx
    cmp ebx, 0
    jg halos_done

    lea rdi, [rdi - 20h]
    jmp scan_forward

halos_done:
    pop rdi
    pop rbx
    ret
HalosGate ENDP

; Find first syscall opcode at/after the provided function body.
FindSyscallAddress PROC
    mov rax, rcx
    xor ebx, ebx

syscall_scan:
    cmp word ptr [rax], 050Fh
    je syscall_found
    inc rax
    inc ebx
    cmp ebx, 80
    jl syscall_scan

    xor eax, eax
syscall_found:
    ret
FindSyscallAddress ENDP

; Shared builder for all indirect stubs.
; RCX = function pointer in ntdll.
; Returns EAX=0 on failure, otherwise EAX=system-call number and R10 set.
BuildIndirectSyscall PROC
    call HellsGate
    mov r11d, eax
    call FindSyscallAddress
    test rax, rax
    jz build_failed

    mov r10, rax
    mov eax, r11d
    ret

build_failed:
    xor eax, eax
    ret
BuildIndirectSyscall ENDP

NtAllocateVirtualMemory_Indirect PROC
    mov [rsp+28h], rcx
    mov rcx, qword ptr [NtAllocateVirtualMemory_Addr]
    call BuildIndirectSyscall
    test eax, eax
    jz syscall_fail
    mov rcx, [rsp+28h]
    jmp r10
NtAllocateVirtualMemory_Indirect ENDP

NtProtectVirtualMemory_Indirect PROC
    mov [rsp+28h], rcx
    mov rcx, qword ptr [NtProtectVirtualMemory_Addr]
    call BuildIndirectSyscall
    test eax, eax
    jz syscall_fail
    mov rcx, [rsp+28h]
    jmp r10
NtProtectVirtualMemory_Indirect ENDP

NtCreateThreadEx_Indirect PROC
    mov [rsp+28h], rcx
    mov rcx, qword ptr [NtCreateThreadEx_Addr]
    call BuildIndirectSyscall
    test eax, eax
    jz syscall_fail
    mov rcx, [rsp+28h]
    jmp r10
NtCreateThreadEx_Indirect ENDP

syscall_fail:
    xor eax, eax
    ret

.data

NtAllocateVirtualMemory_Addr dq 0
NtProtectVirtualMemory_Addr dq 0
NtCreateThreadEx_Addr dq 0
NtWaitForSingleObject_Addr dq 0

END
