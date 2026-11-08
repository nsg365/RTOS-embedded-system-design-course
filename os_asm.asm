;******************************************************************************
; os_asm.asm - Context switch and low-level helpers.
;
; Stack layout of a switched-out task, from its saved SP upwards:
;   R4 R5 R6 R7 R8 R9 R10 R11 EXC_RETURN    <- saved by PendSV_Handler (9 words)
;   [S16..S31]                              <- only if the task used the FPU (16 words)
;   R0 R1 R2 R3 R12 LR PC xPSR [S0-S15 FPSCR] <- pushed by hardware on entry
;
; EXC_RETURN bit 4 = 0 means the hardware stacked an FPU frame, so the task was using floating point and S16-S31 must be saved and restored as well.
;******************************************************************************

        .thumb ;tells assembler to generate thumb instructions
        .text ;Places the following code in the program's executable code section.

        .global PendSV_Handler ;makes the symbol visible to the linker.
        .global os_hardfault_handler
        .global os_crit_enter
        .global os_crit_exit

        .global os_curr
        .global os_sched_select
        .global os_fault_report
;------------------------------------------------------------------------------
; PendSV_Handler: save the running task, ask the scheduler for the next one,
; restore it. Runs at the lowest exception priority, on MSP.
; R0-R3 are free to use here: the hardware already saved them on entry.
;------------------------------------------------------------------------------
PendSV_Handler: .asmfunc
        MOV     R3, #1
        MSR     PRIMASK, R3             ; PRIMASK = 1: interrupts off (PRIMASK masks configurable-priority interrupts and exceptions ONLY)

        LDR     R1, pCurr               ; R1 = &os_curr
        LDR     R2, [R1]                ; R2 = os_curr
        CMP     R2, #0
        BEQ     restore                 ; first switch: no task to save

        ; save the old task onto its own stack 
        MRS     R0, PSP                 ; R0 = task(process) stack pointer. (R0 now points to the hardware-saved context.)

        AND     R3, LR, #0x10           ; R3 = bit 4 of LR (EXC_RETURN): 1 - no extended fpu frame, 0 - fpu frame
        CMP     R3, #0
        BNE     save_core               ; if bit 4 is 1, skip S16-S31

        SUB     R0, R0, #64             ; make space for 16 FPU registers.
        VSTR    S16, [R0, #0] ;basically take the 32-bit floating-point number currently sitting in register S16 and copy it directly into the memory location pointed to by the address in register R0.
        VSTR    S17, [R0, #4] 
        VSTR    S18, [R0, #8]
        VSTR    S19, [R0, #12]
        VSTR    S20, [R0, #16]
        VSTR    S21, [R0, #20]
        VSTR    S22, [R0, #24]
        VSTR    S23, [R0, #28]
        VSTR    S24, [R0, #32]
        VSTR    S25, [R0, #36]
        VSTR    S26, [R0, #40]
        VSTR    S27, [R0, #44]
        VSTR    S28, [R0, #48]
        VSTR    S29, [R0, #52]
        VSTR    S30, [R0, #56]
        VSTR    S31, [R0, #60]

save_core:
        SUB     R0, R0, #36             ; make room for R4-R11 + EXC_RETURN
        STR     R4,  [R0, #0]
        STR     R5,  [R0, #4]
        STR     R6,  [R0, #8]
        STR     R7,  [R0, #12]
        STR     R8,  [R0, #16]
        STR     R9,  [R0, #20]
        STR     R10, [R0, #24]
        STR     R11, [R0, #28]
        STR     LR,  [R0, #32]          ; EXC_RETURN
        STR     R0, [R2]                ; os_curr->sp = R0 (sp is the first TCB field) (Save the updated SP in the TCB)

        ;Pick the next task 
restore:
        BL      os_sched_select         ; R0 = address of the next task's TCB (interrupts still off); calling BL overwrites LR. But we've already storeed the old task's EXC_RETURN on its stack so we dont lose anything.
        LDR     R1, pCurr
        STR     R0, [R1]                ; os_curr = next

        ;restore the new task from its stack
        LDR     R0, [R0]                ; R0 = next->sp
        LDR     R4,  [R0, #0]
        LDR     R5,  [R0, #4]
        LDR     R6,  [R0, #8]
        LDR     R7,  [R0, #12]
        LDR     R8,  [R0, #16]
        LDR     R9,  [R0, #20]
        LDR     R10, [R0, #24]
        LDR     R11, [R0, #28]
        LDR     LR,  [R0, #32]          ; EXC_RETURN of the new task
        ADD     R0, R0, #36             ; step past those 9 words

        AND     R3, LR, #0x10
        CMP     R3, #0
        BNE     restore_done            ; no FPU frame: skip S16-S31

        VLDR    S16, [R0, #0]
        VLDR    S17, [R0, #4]
        VLDR    S18, [R0, #8]
        VLDR    S19, [R0, #12]
        VLDR    S20, [R0, #16]
        VLDR    S21, [R0, #20]
        VLDR    S22, [R0, #24]
        VLDR    S23, [R0, #28]
        VLDR    S24, [R0, #32]
        VLDR    S25, [R0, #36]
        VLDR    S26, [R0, #40]
        VLDR    S27, [R0, #44]
        VLDR    S28, [R0, #48]
        VLDR    S29, [R0, #52]
        VLDR    S30, [R0, #56]
        VLDR    S31, [R0, #60]
        ADD     R0, R0, #64             ; step past the 16 float registers

restore_done:
        MSR     PSP, R0                 ; PSP = address of new task's hardware exception frame
        MOV     R3, #0
        MSR     PRIMASK, R3             ; PRIMASK = 0: interrupts on
        BX      LR                      ; exception return: hardware pops the rest
        .endasmfunc

;------------------------------------------------------------------------------
; os_hardfault_handler: pass the faulting stack frame to os_fault_report().
;------------------------------------------------------------------------------
os_hardfault_handler: .asmfunc
        MRS     R0, MSP                 ; assume the handler stack (MSP) was in use
        AND     R1, LR, #0x4            ; EXC_RETURN bit 2: 0 = MSP, 1 = PSP
        CMP     R1, #0
        BEQ     fault_report
        MRS     R0, PSP                 ; a task was running: use its stack
fault_report:
        B       os_fault_report         ; os_fault_report(frame), never returns
        .endasmfunc

;------------------------------------------------------------------------------
; uint32_t os_crit_enter(void): disable interrupts, return previous PRIMASK.
; void os_crit_exit(uint32_t primask): restore PRIMASK.
;------------------------------------------------------------------------------
os_crit_enter: .asmfunc
        MRS     R0, PRIMASK             ; return value = old PRIMASK
        MOV     R1, #1
        MSR     PRIMASK, R1             ; interrupts off
        BX      LR
        .endasmfunc

os_crit_exit: .asmfunc
        MSR     PRIMASK, R0             ; put back what os_crit_enter returned
        BX      LR
        .endasmfunc

;------------------------------------------------------------------------------
        .align  4
pCurr:  .word   os_curr ;(creates a word in memory containing the address of the global variable os_curr)

        .end
