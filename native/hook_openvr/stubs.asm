; Copyright (C) 2026 EncidXVII
; SPDX-License-Identifier: GPL-3.0-or-later

; Export stubs of the openvr_api.dll proxy. Each one jumps to the same export of the game's original DLL with every
; argument register untouched, so no signature is needed. The first call of any stub loads the original (outside
; DllMain, where loading a library could deadlock) through fov_proxy_resolve. Indices match exports.h.

EXTERN fov_proxy_targets:QWORD
EXTERN fov_proxy_resolve:PROC

STUB MACRO name, index
name PROC
    mov eax, index
    lea r10, fov_proxy_targets
    mov r10, QWORD PTR [r10 + rax*8]
    test r10, r10
    jz resolve_and_jump
    jmp r10
name ENDP
ENDM

.code

; rax = export index. Saves the argument registers (integer and float), resolves, restores, jumps to the target.
resolve_and_jump PROC
    push rcx
    push rdx
    push r8
    push r9
    sub rsp, 68h
    movdqu [rsp+20h], xmm0
    movdqu [rsp+30h], xmm1
    movdqu [rsp+40h], xmm2
    movdqu [rsp+50h], xmm3
    mov rcx, rax
    call fov_proxy_resolve
    movdqu xmm0, [rsp+20h]
    movdqu xmm1, [rsp+30h]
    movdqu xmm2, [rsp+40h]
    movdqu xmm3, [rsp+50h]
    add rsp, 68h
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp rax
resolve_and_jump ENDP

STUB LiquidVR, 0
STUB VRControlPanel, 1
STUB VRHeadsetView, 2
STUB VRPaths, 3
STUB VRVirtualDisplay, 4
STUB VR_GetGenericInterface, 5
STUB VR_GetInitToken, 6
STUB VR_GetRuntimePath, 7
STUB VR_GetStringForHmdError, 8
STUB VR_GetVRInitErrorAsEnglishDescription, 9
STUB VR_GetVRInitErrorAsSymbol, 10
STUB VR_InitInternal, 11
STUB VR_InitInternal2, 12
STUB VR_IsHmdPresent, 13
STUB VR_IsInterfaceVersionValid, 14
STUB VR_IsRuntimeInstalled, 15
STUB VR_RuntimePath, 16
STUB VR_ShutdownInternal, 17

END
