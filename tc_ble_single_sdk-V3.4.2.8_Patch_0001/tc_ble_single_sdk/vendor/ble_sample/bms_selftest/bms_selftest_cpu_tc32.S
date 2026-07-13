/********************************************************************************************************
 * TC32 register and branch/return tests. The sequence follows the ABI observed in cstartup_825x.S:
 * r0-r3 caller-saved, r4-r11 preserved, r13 SP and r14 LR.
 *******************************************************************************************************/
	.code	16
	.section .text.bms_selftest_cpu,"ax"
	.align	2
	.global BMS_SelfTest_Tc32CpuRegPatternAsm
	.thumb_func
	.type BMS_SelfTest_Tc32CpuRegPatternAsm, %function
BMS_SelfTest_Tc32CpuRegPatternAsm:
	tpush	{r4, r5, r6, r7}
	tmov	r4, r8
	tmov	r5, r9
	tmov	r6, r10
	tmov	r7, r11
	tpush	{r4, r5, r6, r7}

	/* r0 contains the requested test pattern. */
	tmov	r1, r0
	tcmp	r0, r1
	tjne	.Lcpu_fail
	tmov	r2, r0
	tcmp	r2, r0
	tjne	.Lcpu_fail
	tmov	r3, r0
	tcmp	r3, r0
	tjne	.Lcpu_fail
	tmov	r4, r0
	tcmp	r4, r0
	tjne	.Lcpu_fail
	tmov	r5, r0
	tcmp	r5, r0
	tjne	.Lcpu_fail
	tmov	r6, r0
	tcmp	r6, r0
	tjne	.Lcpu_fail
	tmov	r7, r0
	tcmp	r7, r0
	tjne	.Lcpu_fail

	tmov	r8, r0
	tmov	r1, r8
	tcmp	r1, r0
	tjne	.Lcpu_fail
	tmov	r9, r0
	tmov	r1, r9
	tcmp	r1, r0
	tjne	.Lcpu_fail
	tmov	r10, r0
	tmov	r1, r10
	tcmp	r1, r0
	tjne	.Lcpu_fail
	tmov	r11, r0
	tmov	r1, r11
	tcmp	r1, r0
	tjne	.Lcpu_fail

	/* Equal and non-equal branches exercise the compare/Z decision path. */
	tcmp	r0, r0
	tjne	.Lcpu_fail
	tmov	r1, #1
	txor	r1, r0
	tcmp	r1, r0
	tjeq	.Lcpu_fail
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
	.size BMS_SelfTest_Tc32CpuRegPatternAsm, .-BMS_SelfTest_Tc32CpuRegPatternAsm

	.section .text.bms_selftest_flow,"ax"
	.align	2
	.global BMS_SelfTest_Tc32ControlFlowAsm
	.thumb_func
	.type BMS_SelfTest_Tc32ControlFlowAsm, %function
BMS_SelfTest_Tc32ControlFlowAsm:
	tmov	r0, #0
	tj	.Lflow_step1
.Lflow_unreachable0:
	tmov	r0, #0
	tjex	lr
.Lflow_step1:
	tadd	r0, #1
	tcmp	r0, #1
	tjne	.Lflow_fail
	tj	.Lflow_step2
.Lflow_unreachable1:
	tmov	r0, #0
	tjex	lr
.Lflow_step2:
	tadd	r0, #2
	tcmp	r0, #3
	tjne	.Lflow_fail
	tmov	r1, #5
	tcmp	r1, #5
	tjeq	.Lflow_step3
	tj	.Lflow_fail
.Lflow_step3:
	tadd	r0, #4
	tcmp	r0, #7
	tjne	.Lflow_fail
	tmov	r0, #1
	tjex	lr
.Lflow_fail:
	tmov	r0, #0
	tjex	lr
	.size BMS_SelfTest_Tc32ControlFlowAsm, .-BMS_SelfTest_Tc32ControlFlowAsm

	.section .text.bms_selftest_sp,"ax"
	.align	2
	.global BMS_SelfTest_Tc32GetSp
	.thumb_func
	.type BMS_SelfTest_Tc32GetSp, %function
BMS_SelfTest_Tc32GetSp:
	tmov	r0, r13
	tjex	lr
	.size BMS_SelfTest_Tc32GetSp, .-BMS_SelfTest_Tc32GetSp
