/* SPDX-License-Identifier: MIT */
/* Linux O32 syscall contract: see research/mips-syscall-contract.md. */
#ifndef NT98513_CLIENT_ENTRY
#define NT98513_CLIENT_ENTRY nt98513_client_main
#endif
    .set noreorder
    .text
    .globl nt98513_client_syscall3
    .ent nt98513_client_syscall3
nt98513_client_syscall3:
    /* Stock entry reads user argument slots 4..7, even for syscall3. */
    addiu $sp,$sp,-32
    sw $zero,16($sp)
    sw $zero,20($sp)
    sw $zero,24($sp)
    sw $zero,28($sp)
    move $v0,$a0
    move $a0,$a1
    move $a1,$a2
    move $a2,$a3
    syscall
    beq $a3,$zero,1f
    nop
    subu $v0,$zero,$v0
1:
    addiu $sp,$sp,32
    jr $ra
    nop
    .end nt98513_client_syscall3

    .globl _start
    .ent _start
_start:
    lw $a0,0($sp)
    addiu $a1,$sp,4
    addiu $sp,$sp,-32
    jal NT98513_CLIENT_ENTRY
    nop
    move $a1,$v0
    li $a0,4001
    move $a2,$zero
    move $a3,$zero
    jal nt98513_client_syscall3
    nop
2:
    break 0
    b 2b
    nop
    .end _start
    .section .note.GNU-stack,"",@progbits
