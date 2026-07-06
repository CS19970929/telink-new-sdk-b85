/********************************************************************************************************
 * @file    bms_selftest_tc32.S
 *
 * @brief   TC32 assembly self-test helpers for BMS self-test.
 *******************************************************************************************************/

	.code	16
	.text

	.section	.text,"ax"
	.align	2
	.global	BMS_SelfTest_Tc32CpuRegAsm
	.code 16
	.thumb_func
	.type	BMS_SelfTest_Tc32CpuRegAsm, %function
BMS_SelfTest_Tc32CpuRegAsm:
	tpush	{r4, r5, r6, r7}
	tmov	r4, r8
	tmov	r5, r9
	tmov	r6, r10
	tmov	r7, r11
	tpush	{r4, r5, r6, r7}

	tloadr	r0, .Lcpu_p8
	tmov	r8, r0
	tloadr	r0, .Lcpu_p9
	tmov	r9, r0
	tloadr	r0, .Lcpu_p10
	tmov	r10, r0
	tloadr	r0, .Lcpu_p11
	tmov	r11, r0

	tloadr	r0, .Lcpu_p0
	tloadr	r1, .Lcpu_p1
	tloadr	r2, .Lcpu_p2
	tloadr	r3, .Lcpu_p3
	tloadr	r4, .Lcpu_p4
	tloadr	r5, .Lcpu_p5
	tloadr	r6, .Lcpu_p6
	tloadr	r7, .Lcpu_p7

	txor	r0, r1
	txor	r0, r2
	txor	r0, r3
	txor	r0, r4
	txor	r0, r5
	txor	r0, r6
	txor	r0, r7
	tmov	r1, r8
	txor	r0, r1
	tmov	r1, r9
	txor	r0, r1
	tmov	r1, r10
	txor	r0, r1
	tmov	r1, r11
	txor	r0, r1

	tloadr	r1, .Lcpu_expected
	tcmp	r0, r1
	tjne	.Lcpu_fail
	tmov	r0, #1
	tj	.Lcpu_restore

.Lcpu_fail:
	tmov	r0, #0

.Lcpu_restore:
	tpop	{r4, r5, r6, r7}
	tmov	r8, r4
	tmov	r9, r5
	tmov	r10, r6
	tmov	r11, r7
	tpop	{r4, r5, r6, r7}
	tjex	lr

	.align	4
.Lcpu_p0:
	.word	(0x13579bdf)
.Lcpu_p1:
	.word	(0x2468ace0)
.Lcpu_p2:
	.word	(0x55aa55aa)
.Lcpu_p3:
	.word	(0xaa55aa55)
.Lcpu_p4:
	.word	(0x0f0f0f0f)
.Lcpu_p5:
	.word	(0xf0f0f0f0)
.Lcpu_p6:
	.word	(0x12345678)
.Lcpu_p7:
	.word	(0x87654321)
.Lcpu_p8:
	.word	(0xdeadbeef)
.Lcpu_p9:
	.word	(0x21524110)
.Lcpu_p10:
	.word	(0x5a5aa5a5)
.Lcpu_p11:
	.word	(0xa5a55a5a)
.Lcpu_expected:
	.word	(0xa26e2266)
	.size	BMS_SelfTest_Tc32CpuRegAsm, .-BMS_SelfTest_Tc32CpuRegAsm

	.section	.text,"ax"
	.align	2
	.global	BMS_SelfTest_Tc32PcAsm
	.code 16
	.thumb_func
	.type	BMS_SelfTest_Tc32PcAsm, %function
BMS_SelfTest_Tc32PcAsm:
	tmov	r0, #0
	tj	.Lpc_step1

.Lpc_unreachable0:
	tmov	r0, #0
	tjex	lr

.Lpc_step1:
	tadd	r0, #1
	tcmp	r0, #1
	tjne	.Lpc_fail
	tj	.Lpc_step2

.Lpc_unreachable1:
	tmov	r0, #0
	tjex	lr

.Lpc_step2:
	tadd	r0, #2
	tcmp	r0, #3
	tjne	.Lpc_fail
	tmov	r1, #5
	tcmp	r1, #5
	tjeq	.Lpc_step3
	tj	.Lpc_fail

.Lpc_step3:
	tadd	r0, #4
	tcmp	r0, #7
	tjne	.Lpc_fail
	tmov	r0, #1
	tjex	lr

.Lpc_fail:
	tmov	r0, #0
	tjex	lr
	.size	BMS_SelfTest_Tc32PcAsm, .-BMS_SelfTest_Tc32PcAsm
