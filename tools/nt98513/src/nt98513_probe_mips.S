/* SPDX-License-Identifier: MIT
 * Linux MIPS O32 only. See research/descriptor-probe.md for byte-backed review.
 * C signature: int32_t nt98513_syscall3(nr, arg0, arg1, arg2).
 */
    .set noreorder
    .text
    .align 2
    .globl nt98513_syscall3
    .type nt98513_syscall3,@function
nt98513_syscall3:
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
    .size nt98513_syscall3,.-nt98513_syscall3

    .globl _start
    .type _start,@function
_start:
    lw $a0,0($sp)
    addiu $a1,$sp,4
    addiu $sp,$sp,-32
    jal nt98513_probe_main
    nop
    move $a1,$v0
    li $a0,4001
    move $a2,$zero
    move $a3,$zero
    jal nt98513_syscall3
    nop
2:
    break 0
    b 2b
    nop
    .size _start,.-_start
    .section .note.GNU-stack,"",@progbits
