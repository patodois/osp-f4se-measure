/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Microsoft x64 calls from GCC, with an OS SEH scope around the foreign call.
 * bool osp_call_{float,word}(fn, this_pointer, argument, output_pointer)
 * bool osp_call_void2(fn, this_pointer, argument1, argument2)
 * The SEH handler is the Windows CRT __C_specific_handler, not GCC C++ EH.
 * No C++ objects live in this frame. Unwind info also permits unwinding the
 * game's MSVC frames if a getter encounters an access violation.
 */
.text
.macro getter name, floating
.globl \name
.def \name; .scl 2; .type 32; .endef
.seh_proc \name
\name:
    pushq %rbx
    .seh_pushreg %rbx
    subq $32, %rsp
    .seh_stackalloc 32
    .seh_endprologue
    movq %r9, %rbx
    testq %rcx, %rcx
    jz .Lfail\@
    testq %rdx, %rdx
    jz .Lfail\@
.if \floating != 2
    testq %rbx, %rbx
    jz .Lfail\@
.endif
    movq %rcx, %rax
    movq %rdx, %rcx
    movq %r8, %rdx
.if \floating == 2
    movq %rbx, %r8
.endif
.Lbegin\@:
    call *%rax
    /* Caller unwind ControlPc can be the return address: keep it in scope. */
    nop
.Lend\@:
.if \floating == 1
    movss %xmm0, (%rbx)
.elseif \floating == 0
    movq %rax, (%rbx)
.endif
    movl $1, %eax
    jmp .Lreturn\@
.Lfail\@:
    xorl %eax, %eax
.Lreturn\@:
    addq $32, %rsp
    popq %rbx
    ret
    .seh_handler __C_specific_handler, @except
    .seh_handlerdata
    .long 1
    .rva .Lbegin\@, .Lend\@
    .long 1
    .rva .Lfail\@
    .text
.seh_endproc
.endm
getter osp_call_float, 1
getter osp_call_word, 0
getter osp_call_void2, 2
