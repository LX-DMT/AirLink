/* AirLink R26 C906L bare-metal LVGL entry. */
.section .text.start, "ax", @progbits
.globl _start
.type _start, @function
.option norvc
_start:
 csrw mie, zero
 li t0, 8
 csrc mstatus, t0
 la t0, trap_loop
 csrw mtvec, t0
 csrw mscratch, zero
 li t0, 0x30013
 csrs 0x7c2, t0
 li t0, 2
 csrc 0x7c1, t0
 li t0, 1
 csrs 0x7c1, t0
 fence.i
 .option push
 .option norelax
 la gp, __global_pointer$
 .option pop
 la sp, __stack_top
 la t0, __bss_start
 la t1, __bss_end
1: bgeu t0, t1, 2f
 sw zero, 0(t0)
 addi t0, t0, 4
 j 1b
2: call r25_main
3: wfi
 j 3b
trap_loop:
 csrw mie, zero
4: wfi
 j 4b
.size _start, . - _start
