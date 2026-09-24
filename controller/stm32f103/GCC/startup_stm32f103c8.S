.syntax unified
.cpu cortex-m3
.thumb

.section .isr_vector,"a",%progbits
.global vectors
vectors:
    .word _estack
    .word Reset_Handler
    .word Default_Handler /* NMI */
    .word Default_Handler /* HardFault */
    .word Default_Handler /* MemManage */
    .word Default_Handler /* BusFault */
    .word Default_Handler /* UsageFault */
    .word 0, 0, 0, 0
    .word Default_Handler /* SVC */
    .word Default_Handler /* DebugMon */
    .word 0
    .word Default_Handler /* PendSV */
    .word SysTick_Handler
    .rept 60
    .word Default_Handler
    .endr

.section .text.Reset_Handler,"ax",%progbits
.thumb_func
.global Reset_Handler
Reset_Handler:
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:  bl main
5:  b 5b

.section .text.Default_Handler,"ax",%progbits
.thumb_func
.global Default_Handler
Default_Handler:
    b Default_Handler
