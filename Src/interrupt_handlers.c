/*
 * interrupt_handlers.c
 *
 *  Created on: Jan 15, 2026
 *      Author: Extracted from AM32 at32f421_it.c for porting
 *  Description: Core interrupt handling logic for BLDC motor control
 *
 *  NOTE: This is a STANDALONE extracted module for porting to other projects.
 *        Do NOT compile this file as part of the original AM32 project.
 *
 *  Key Interrupts:
 *  1. Comparator Interrupt - Zero-crossing detection for commutation
 *  2. Commutation Timer - Triggers actual phase switching
 *  3. Control Loop Timer - 20kHz main control routine
 *  4. DMA Interrupts - ADC and input signal capture
 *
 *  Hardware Abstraction Layer Requirements:
 *  You must implement these in your platform:
 *  - getCompOutputLevel() - Read comparator output
 *  - Timer register access macros
 *  - Interrupt flag clear methods
 */

#include "interrupt_handlers.h"

/* ============================================================================
 *  HARDWARE ABSTRACTION LAYER - Implement these in your platform
 * ============================================================================ */

// Comparator function (implement in your comparator.c)
extern uint8_t getCompOutputLevel(void);

/* Timer/Interrupt macros - define these in your platform headers
 * 
 * Example for AT32:
 * #define INTERVAL_TIMER_COUNT    (INTERVAL_TIMER->cval)
 * #define EXTI_LINE               EXINT_LINE_21
 * 
 * For STM32:
 * #define INTERVAL_TIMER_COUNT    (TIM1->CNT)
 * #define EXTI_LINE               EXTI_Line21
 */

/* MCU-specific conditional compilation flags:
 * #define ARTERY           // For AT32 MCUs
 * #define STMICRO          // For STM32 MCUs  
 * #define GIGADEVICES      // For GD32 MCUs
 */

/* Timer peripheral definitions (define in your targets.h):
 * #define COM_TIMER              TIM16
 * #define CTRL_LOOP_TIMER        TIM14
 * #define INTERVAL_TIMER         TIM1
 * #define INPUT_DMA_CHANNEL      DMA1_Channel5
 */

/* ========================================================================== */
/*                    COMPARATOR INTERRUPT HANDLING                           */
/* ========================================================================== */

/**
 * @brief Check if comparator interrupt is valid
 * @return 1 if valid, 0 if should be filtered
 * 
 * @details Filters false zero-crossing triggers by checking timing:
 *          - Interrupt must occur after half of average interval
 *          - Or comparator output must match expected rising edge
 * 
 * This prevents triggering on noise or EMI during the first half of 
 * the commutation cycle when BEMF transitions are not yet stable.
 */
uint8_t isCompInterruptValid(void)
{
    // Check if we're past the halfway point of average commutation interval
    if ((INTERVAL_TIMER_COUNT) > (average_interval >> 1)) {
        return 1;  // Valid - we're in the expected zero-crossing window
    } else {
        // Early in the cycle - only accept if comparator output is correct
        if (getCompOutputLevel() == rising) {
            return 1;  // Valid - comparator confirms the edge
        }
        return 0;  // Invalid - filter this interrupt
    }
}

/**
 * @brief Handle comparator interrupt for zero-crossing detection
 * 
 * @details This is the core BEMF zero-crossing detection interrupt.
 *          When the floating phase voltage crosses the virtual neutral,
 *          the comparator triggers this interrupt.
 * 
 * Timing critical:
 * - Validates interrupt timing to filter noise
 * - Calls interruptRoutine() which records timing and schedules commutation
 * 
 * Called by: ADC1_CMP_IRQHandler() or equivalent MCU-specific handler
 */
void handleCompInterrupt(void)
{
    if (isCompInterruptValid()) {
        clearCompInterruptFlag();
        interruptRoutine();  // Process zero-crossing event
    } else {
        // Filter false trigger - just clear and ignore
        clearCompInterruptFlag();
    }
}

/**
 * @brief Clear comparator interrupt flag (MCU-specific)
 */
void clearCompInterruptFlag(void)
{
#ifdef ARTERY
    EXINT->intsts = EXTI_LINE;  // AT32 clear by writing 1
#endif
#ifdef STMICRO
    EXTI->PR = EXTI_LINE;       // STM32 clear pending register
#endif
#ifdef GIGADEVICES
    EXTI_PD = EXTI_LINE;        // GD32 clear pending bit
#endif
}

/* ========================================================================== */
/*                   COMMUTATION TIMER INTERRUPT HANDLING                     */
/* ========================================================================== */

/**
 * @brief Handle commutation timer interrupt
 * 
 * @details This interrupt fires after the calculated waitTime expires
 *          following a zero-crossing detection. It triggers the actual
 *          commutation (phase switching) at the optimal electrical angle.
 * 
 * Timing: Occurs at 30° electrical after zero-crossing (90° total from
 *         previous commutation), adjusted by advance angle.
 * 
 * Called by: TMR16_GLOBAL_IRQHandler() or equivalent
 */
void handleCommutationTimerInterrupt(void)
{
    clearCommutationTimerFlag();
    PeriodElapsedCallback();  // Execute commutation and timing calculations
}

/**
 * @brief Clear commutation timer interrupt flag
 */
void clearCommutationTimerFlag(void)
{
#ifdef ARTERY
    COM_TIMER->ists = 0x00;  // Clear all interrupt status bits
#endif
#ifdef STMICRO
    COM_TIMER->SR = 0;       // Clear status register
#endif
#ifdef GIGADEVICES
    TIMER_INTF(COM_TIMER) = 0;
#endif
}

/* ========================================================================== */
/*                   CONTROL LOOP INTERRUPT HANDLING                          */
/* ========================================================================== */

/**
 * @brief Handle control loop timer interrupt (20kHz)
 * 
 * @details Main periodic control loop running at 20kHz (50μs period).
 *          Handles:
 *          - Input signal processing
 *          - Duty cycle ramping and limiting
 *          - PID control loops (speed, current, stall protection)
 *          - Telemetry preparation
 *          - Arming/disarming logic
 * 
 * This is where most of the control logic happens outside of commutation.
 * 
 * Called by: TMR14_GLOBAL_IRQHandler() or equivalent
 */
void handleControlLoopInterrupt(void)
{
    clearControlLoopTimerFlag();
    tenKhzRoutine();  // Main 20kHz control function (misnamed from legacy code)
}

/**
 * @brief Clear control loop timer interrupt flag
 */
void clearControlLoopTimerFlag(void)
{
#ifdef ARTERY
    CTRL_LOOP_TIMER->ists = (uint16_t)~TMR_OVF_FLAG;
#endif
#ifdef STMICRO
    CTRL_LOOP_TIMER->SR = ~TIM_SR_UIF;
#endif
#ifdef GIGADEVICES
    TIMER_INTF(CTRL_LOOP_TIMER) &= ~TIMER_INT_FLAG_UP;
#endif
}

/* ========================================================================== */
/*                        DMA INTERRUPT HANDLING                              */
/* ========================================================================== */

/**
 * @brief Handle ADC DMA transfer complete interrupt
 * 
 * @details Called when DMA completes transferring ADC conversion results.
 *          ADC typically measures:
 *          - Battery voltage
 *          - Current sensing
 *          - Temperature
 *          - Input signal (if ADC input mode)
 * 
 * Called by: DMA1_Channel1_IRQHandler() or equivalent
 */
void handleAdcDmaInterrupt(void)
{
#ifdef USE_ADC
    ADC_DMA_Callback();  // Process ADC readings
#endif
}

/**
 * @brief Handle input capture DMA interrupt
 * 
 * @details Processes DMA completion for input signal capture:
 *          - DSHOT: Decodes digital shot protocol timing
 *          - PWM: Measures servo PWM pulse width
 * 
 * The DMA automatically captures timer values at each edge,
 * this interrupt processes the complete frame.
 * 
 * Called by: DMA1_Channel5_4_IRQHandler() or equivalent
 */
void handleInputDmaInterrupt(void)
{
    // Disable DMA channel
    INPUT_DMA_CHANNEL->ctrl_bit.chen = FALSE;
    
    // Signal transfer complete
    transfercomplete();
    
    // Trigger software interrupt for DSHOT processing
#ifdef ARTERY
    EXINT->swtrg = EXINT_LINE_15;
#endif
#ifdef STMICRO
    EXTI->SWIER = EXTI_LINE_15;
#endif
}

/* ========================================================================== */
/*                   EXTERNAL INTERRUPT HANDLING                              */
/* ========================================================================== */

/**
 * @brief Handle DSHOT processing interrupt
 * 
 * @details Software-triggered interrupt to process DSHOT frame.
 *          Decodes the captured timing data into throttle value
 *          and command bits.
 * 
 * Called by: EXINT15_4_IRQHandler() or equivalent
 */
void handleDshotProcessInterrupt(void)
{
    processDshot();  // Decode DSHOT frame data
}

/* ========================================================================== */
/*                      MCU-SPECIFIC WRAPPER FUNCTIONS                        */
/*         (These should be implemented in MCU-specific it.c files)           */
/* ========================================================================== */

#ifdef EXAMPLE_IMPLEMENTATIONS
/*
 * Example implementations for reference - 
 * actual implementations should be in MCU-specific it.c files
 */

/**
 * @brief AT32F421 Comparator Interrupt Handler
 * @note Hardware interrupt - do not call directly
 */
void ADC1_CMP_IRQHandler(void)
{
    handleCompInterrupt();
}

/**
 * @brief AT32F421 Commutation Timer Interrupt Handler (TMR16)
 * @note Hardware interrupt - do not call directly
 */
void TMR16_GLOBAL_IRQHandler(void)
{
    handleCommutationTimerInterrupt();
}

/**
 * @brief AT32F421 Control Loop Timer Interrupt Handler (TMR14)
 * @note Hardware interrupt - do not call directly
 */
void TMR14_GLOBAL_IRQHandler(void)
{
    handleControlLoopInterrupt();
}

/**
 * @brief AT32F421 ADC DMA Interrupt Handler
 * @note Hardware interrupt - do not call directly
 */
void DMA1_Channel1_IRQHandler(void)
{
    if (dma_flag_get(DMA1_FDT1_FLAG) == SET) {
        DMA1->clr = DMA1_GL1_FLAG;
        handleAdcDmaInterrupt();
        
        if (dma_flag_get(DMA1_DTERR1_FLAG) == SET) {
            DMA1->clr = DMA1_GL1_FLAG;
        }
    }
}

/**
 * @brief AT32F421 Input Capture DMA Interrupt Handler
 * @note Hardware interrupt - do not call directly
 */
void DMA1_Channel5_4_IRQHandler(void)
{
#ifdef USE_TIMER_15_CHANNEL_1
    if (dshot) {
        DMA1->clr = DMA1_GL5_FLAG;
        handleInputDmaInterrupt();
        return;
    }
    
    if (dma_flag_get(DMA1_FDT5_FLAG) == SET) {
        DMA1->clr = DMA1_GL5_FLAG;
        handleInputDmaInterrupt();
    }
    
    if (dma_flag_get(DMA1_DTERR5_FLAG) == SET) {
        DMA1->clr = DMA1_GL5_FLAG;
    }
#endif

#ifdef USE_TIMER_3_CHANNEL_1
    if (dshot) {
        DMA1->clr = DMA1_GL4_FLAG;
        handleInputDmaInterrupt();
        return;
    }
    
    if (dma_flag_get(DMA1_FDT4_FLAG) == SET) {
        DMA1->clr = DMA1_GL4_FLAG;
        handleInputDmaInterrupt();
    }
    
    if (dma_flag_get(DMA1_DTERR4_FLAG) == SET) {
        DMA1->clr = DMA1_GL4_FLAG;
    }
#endif
}

/**
 * @brief AT32F421 External Interrupt Handler for DSHOT processing
 * @note Hardware interrupt - do not call directly
 */
void EXINT15_4_IRQHandler(void)
{
    if ((EXINT->intsts & EXINT_LINE_15) != (uint32_t)RESET) {
        EXINT->intsts = EXINT_LINE_15;
        handleDshotProcessInterrupt();
    }
}

#endif /* EXAMPLE_IMPLEMENTATIONS */
