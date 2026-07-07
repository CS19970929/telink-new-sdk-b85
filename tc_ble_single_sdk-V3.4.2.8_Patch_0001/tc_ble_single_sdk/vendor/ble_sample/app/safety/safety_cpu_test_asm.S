/********************************************************************************************************
 * Telink TC32 轻量 CPU 寄存器自检。
 * 该例程只破坏调用者保存寄存器，并在返回前恢复 r1-r7/lr。
 *******************************************************************************************************/
	.code 16
	.section	.text.safety_cpu_test_asm,"ax"
	.global		safety_cpu_register_test_asm
	.thumb_func
	.type		safety_cpu_register_test_asm, %function

safety_cpu_register_test_asm:
	tpush	{r1-r7, lr}

	tmrcs	r7
	tmcsr	r7

	tmov	r1, #0xaa
	tcmp	r1, #0xaa
	tjne	.L_safety_cpu_fail
	tmov	r1, #0x55
	tcmp	r1, #0x55
	tjne	.L_safety_cpu_fail

	tmov	r2, #0xaa
	tcmp	r2, #0xaa
	tjne	.L_safety_cpu_fail
	tmov	r2, #0x55
	tcmp	r2, #0x55
	tjne	.L_safety_cpu_fail

	tmov	r3, #0xaa
	tcmp	r3, #0xaa
	tjne	.L_safety_cpu_fail
	tmov	r3, #0x55
	tcmp	r3, #0x55
	tjne	.L_safety_cpu_fail

	tmov	r4, #0xaa
	tcmp	r4, #0xaa
	tjne	.L_safety_cpu_fail
	tmov	r4, #0x55
	tcmp	r4, #0x55
	tjne	.L_safety_cpu_fail

	tmov	r5, #0xaa
	tcmp	r5, #0xaa
	tjne	.L_safety_cpu_fail
	tmov	r5, #0x55
	tcmp	r5, #0x55
	tjne	.L_safety_cpu_fail

	tmov	r6, #0xaa
	tcmp	r6, #0xaa
	tjne	.L_safety_cpu_fail
	tmov	r6, #0x55
	tcmp	r6, #0x55
	tjne	.L_safety_cpu_fail

	tmov	r0, #1
	tpop	{r1-r7, pc}

.L_safety_cpu_fail:
	tmov	r0, #0
	tpop	{r1-r7, pc}

	.size	safety_cpu_register_test_asm, .-safety_cpu_register_test_asm
