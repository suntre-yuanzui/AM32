/*
 * commutation.c
 *
 *  Created on: Jan 15, 2026
 *      Author: Extracted from AM32 main.c for porting
 *  Description: Six-step commutation control for BLDC motors
 *
 *  NOTE: This is a STANDALONE extracted module for porting to other projects.
 *        Do NOT compile this file as part of the original AM32 project.
 *
 *  Core Functions:
 *  - Six-step commutation sequence (1->2->3->4->5->6)
 *  - BEMF zero-crossing detection
 *  - Commutation timing control with advance angle
 *  - Polling and interrupt-based operation modes
 *
 *  Hardware Abstraction Layer Requirements:
 *  You must implement these functions in your target platform:
 *  - getCompOutputLevel() - Read comparator output
 *  - changeCompInput() - Switch comparator input source
 *  - enableCompInterrupts() - Enable comparator interrupts
 *  - maskPhaseInterrupts() - Disable comparator interrupts
 *  - comStep(step) - Execute phase switching for given step
 *  - Macro definitions for timer access
 */

#include "commutation.h"

/* ============================================================================
 *  HARDWARE ABSTRACTION LAYER - Implement these in your platform
 * ============================================================================ */

// Comparator functions (implement in your comparator.c)
extern uint8_t getCompOutputLevel(void);
extern void changeCompInput(void);
extern void enableCompInterrupts(void);
extern void maskPhaseInterrupts(void);

// Phase output functions (implement in your phaseouts.c)
extern void comStep(int step);

/* Timer macros - define these in your targets.h or here
 * Example for STM32:
 * #define INTERVAL_TIMER_COUNT        (TIM1->CNT)
 * #define SET_INTERVAL_TIMER_COUNT(x) (TIM1->CNT = (x))
 * #define SET_AND_ENABLE_COM_INT(x)   do { TIM16->ARR = (x); TIM16->CR1 |= TIM_CR1_CEN; } while(0)
 * #define DISABLE_COM_TIMER_INT()     (TIM16->DIER &= ~TIM_DIER_UIE)
 * #define WRITE_REG(reg, val)         ((reg) = (val))
 * #define READ_REG(reg)               (reg)
 */

/* GPIO port definitions for pulse output (optional feature)
 * #define RPM_PULSE_PORT              GPIOA
 * #define RPM_PULSE_PIN               GPIO_PIN_8
 */

/* Phase pin definitions (for MCUs without comparator like F031/G031)
 * #define PHASE_A_EXTI_PORT           GPIOA
 * #define PHASE_A_EXTI_PIN            GPIO_PIN_0
 * #define PHASE_B_EXTI_PORT           GPIOA
 * #define PHASE_B_EXTI_PIN            GPIO_PIN_1
 * #define PHASE_C_EXTI_PORT           GPIOA
 * #define PHASE_C_EXTI_PIN            GPIO_PIN_2
 */

/* Conditional compilation flags
 * #define MCU_F031              // For MCUs without hardware comparator
 * #define MCU_G031              // For MCUs without hardware comparator
 * #define NO_POLLING_START      // Skip polling mode, use interrupt from start
 * #define INVERTED_EXTI         // If EXTI polarity is inverted
 * #define USE_PULSE_OUT         // Enable RPM pulse output
 */

/**
 * @brief Get BEMF state from comparator or GPIO
 * @note Polls the floating phase for zero-crossing detection
 *       Increments bemfcounter on valid state, resets on bad count threshold
 */
void getBemfState(void)
{
    uint8_t current_state = 0;
    
#if defined(MCU_F031) || defined(MCU_G031)
    // For MCUs without comparator, read GPIO directly
    if (step == 1 || step == 4) {
        current_state = PHASE_C_EXTI_PORT->IDR & PHASE_C_EXTI_PIN;
    }
    if (step == 2 || step == 5) { // in phase 2 or 5 read from phase A
        current_state = PHASE_A_EXTI_PORT->IDR & PHASE_A_EXTI_PIN;
    }
    if (step == 3 || step == 6) { // phase B
        current_state = PHASE_B_EXTI_PORT->IDR & PHASE_B_EXTI_PIN;
    }
#else
    // For MCUs with comparator (polarity reversed)
    current_state = !getCompOutputLevel();
#endif

    // Check if BEMF state matches expected rising/falling edge
    if (rising) {
        if (current_state) {
            bemfcounter++;
        } else {
            bad_count++;
            if (bad_count > bad_count_threshold) {
                bemfcounter = 0;
            }
        }
    } else {
        if (!current_state) {
            bemfcounter++;
        } else {
            bad_count++;
            if (bad_count > bad_count_threshold) {
                bemfcounter = 0;
            }
        }
    }
}

/**
 * @brief Execute commutation to next step
 * @note Updates step number, switches phases, changes comparator input
 *       Handles both forward and reverse rotation
 * 
 * Step sequence:
 *   Step 1: A-PWM, B-LOW, C-FLOAT (detect C)
 *   Step 2: C-PWM, B-LOW, A-FLOAT (detect A)
 *   Step 3: C-PWM, A-LOW, B-FLOAT (detect B)
 *   Step 4: B-PWM, A-LOW, C-FLOAT (detect C)
 *   Step 5: B-PWM, C-LOW, A-FLOAT (detect A)
 *   Step 6: A-PWM, C-LOW, B-FLOAT (detect B)
 */
void commutate(void)
{
    // Update step counter based on rotation direction
    if (forward == 1) {
        step++;
        if (step > 6) {
            step = 1;
            desync_check = 1;
        }
        rising = step % 2;  // Odd steps: rising edge, Even steps: falling edge
    } else {
        step--;
        if (step < 1) {
            step = 6;
            desync_check = 1;
        }
        rising = !(step % 2);
    }
    
#ifdef INVERTED_EXTI
    rising = !rising;
#endif

    // Execute phase switching (critical section to prevent dshot interrupt)
    __disable_irq();
    if (!prop_brake_active) {
        comStep(step);  // Switch to new commutation step
    }
    __enable_irq();
    
    // Configure comparator to monitor the new floating phase
    changeCompInput();
    
#ifndef NO_POLLING_START
    // Check if need to switch back to polling mode (for very low speeds)
    if (average_interval > polling_mode_changeover + 500) {
        old_routine = 1;
    }
#endif

    // Reset detection counters
    bemfcounter = 0;
    zcfound = 0;
    
    // Record commutation interval for averaging
    commutation_intervals[step - 1] = commutation_interval;
    
#ifdef USE_PULSE_OUT
    // Toggle pulse output pin for RPM measurement
    if (step == 1 || step == 4) {
        WRITE_REG(RPM_PULSE_PORT->ODR, READ_REG(RPM_PULSE_PORT->ODR) ^ RPM_PULSE_PIN);
    }
#endif
}

/**
 * @brief Period elapsed callback - executes commutation with timing
 * @note Called by timer interrupt after waitTime expires
 *       Calculates next commutation interval with advance angle compensation
 */
void PeriodElapsedCallback(void)
{
    DISABLE_COM_TIMER_INT(); // Disable commutation timer interrupt
    
    // Execute the commutation
    commutate();
    
    // Calculate smoothed commutation interval
    // Formula: new_interval = (old_interval + (lastzctime + thiszctime)/2) / 2
    commutation_interval = ((commutation_interval) + 
                           ((lastzctime + thiszctime) >> 1)) >> 1;
    
    // Calculate advance angle (in units of timer ticks)
    // Advance = commutation_interval * advance_level / 64
    // This provides 0.9375° electrical angle increments (60° / 64)
    if (!eepromBuffer.auto_advance) {
        advance = (commutation_interval * temp_advance) >> 6;
    } else {
        advance = (commutation_interval * auto_advance_level) >> 6;
    }
    
    // Calculate wait time for next commutation
    // Wait time = half commutation interval - advance angle
    // This ensures commutation happens 30° electrical after zero-crossing
    waitTime = (commutation_interval >> 1) - advance;
    
    // Re-enable comparator interrupts for next zero-crossing detection
    if (!old_routine) {
        enableCompInterrupts();
    }
    
    // Increment zero-crossing counter (saturate at 10000)
    if (zero_crosses < 10000) {
        zero_crosses++;
    }
}

/**
 * @brief Interrupt routine for zero-crossing detection
 * @note Called by comparator interrupt when BEMF crosses zero
 *       Filters signal, records timing, and schedules commutation
 */
void interruptRoutine(void)
{
    // Multi-sample filtering to reject noise
    // Check comparator output multiple times (filter_level iterations)
    for (int i = 0; i < filter_level; i++) {
#if defined(MCU_F031) || defined(MCU_G031)
        if (((current_GPIO_PORT->IDR & current_GPIO_PIN) == !(rising))) {
#else
        if (getCompOutputLevel() == rising) {
#endif
            return;  // False zero-crossing, abort
        }
    }
    
    // Valid zero-crossing detected
    __disable_irq();
    maskPhaseInterrupts();  // Disable comparator interrupts
    
    // Record timing information
    lastzctime = thiszctime;
    thiszctime = INTERVAL_TIMER_COUNT;  // Capture current interval timer value
    SET_INTERVAL_TIMER_COUNT(0);        // Reset interval timer
    
    // Schedule commutation after waitTime
    SET_AND_ENABLE_COM_INT(waitTime + 1);  // Enable commutation timer interrupt
    
    __enable_irq();
}

/**
 * @brief Start motor in open-loop mode
 * @note Initializes commutation sequence and enables zero-crossing detection
 */
void startMotor(void)
{
    if (running == 0) {
        commutate();  // Execute first commutation
        commutation_interval = 10000;  // Set initial commutation interval
        SET_INTERVAL_TIMER_COUNT(5000);  // Preset interval timer
        running = 1;
    }
    enableCompInterrupts();  // Enable comparator interrupts for BEMF detection
}

/**
 * @brief Zero-crossing found routine for polling mode
 * @note Used during startup or low-speed operation
 *       Blocking routine that waits for commutation timing
 */
void zcfoundroutine(void)
{
    // Set commutation timer wait time
#ifdef MCU_GDE23
    TIMER_CAR(COM_TIMER) = waitTime;
#endif
#ifdef STMICRO
    COM_TIMER->ARR = waitTime;
#endif
#ifdef MCU_AT32
    COM_TIMER->pr = waitTime;
#endif
    
    // Wait for timer to expire, then commutate
    // (This is blocking - main loop will poll for BEMF during this time)
    
    zero_crosses++;
    
#ifdef NO_POLLING_START
    // Switch to interrupt mode after 2 zero-crosses
    if (zero_crosses == 2) {
        old_routine = 0;
    }
#else
    // Dynamic switch based on speed
    if (average_interval < polling_mode_changeover) {
        // Speed is high enough, switch to interrupt-driven mode
        old_routine = 0;
        enableCompInterrupts();
    }
    
    if (zero_crosses > 100) {
        // Force switch after 100 zero-crosses
        old_routine = 0;
    }
#endif
}
