/*
 * interrupt_handlers.h
 *
 *  Created on: Jan 15, 2026
 *      Author: Extracted from AM32 at32f421_it.c for porting
 *  Description: Core interrupt handling logic for BLDC motor control
 *
 *  NOTE: This is a STANDALONE extracted module for porting to other projects.
 *        Do NOT include this file in the original AM32 project.
 */

#ifndef INC_INTERRUPT_HANDLERS_H_
#define INC_INTERRUPT_HANDLERS_H_

#include <stdint.h>

/* ========================================================================== */
/*                      INTERRUPT HANDLER FUNCTIONS                           */
/* ========================================================================== */

/**
 * @brief Comparator interrupt handler - Zero-crossing detection
 * @note Called when BEMF crosses zero on floating phase
 *       Filters false triggers and calls interruptRoutine()
 */
void handleCompInterrupt(void);

/**
 * @brief Commutation timer interrupt handler
 * @note Called after waitTime expires to execute commutation
 *       Calls PeriodElapsedCallback()
 */
void handleCommutationTimerInterrupt(void);

/**
 * @brief High-frequency control loop interrupt (20kHz)
 * @note Main control loop for duty cycle, input processing, PID
 *       Calls tenKhzRoutine()
 */
void handleControlLoopInterrupt(void);

/**
 * @brief ADC DMA transfer complete interrupt
 * @note Called when ADC conversion completes via DMA
 */
void handleAdcDmaInterrupt(void);

/**
 * @brief Input capture DMA interrupt - for DSHOT/PWM signal
 * @note Handles input signal capture completion
 */
void handleInputDmaInterrupt(void);

/**
 * @brief External interrupt handler - DSHOT processing
 * @note Triggered to process decoded DSHOT frame
 */
void handleDshotProcessInterrupt(void);

/* ========================================================================== */
/*                      INTERRUPT UTILITY FUNCTIONS                           */
/* ========================================================================== */

/**
 * @brief Check if comparator interrupt should be processed
 * @return 1 if interrupt is valid, 0 if it should be filtered
 * @note Filters early triggers based on timing
 */
uint8_t isCompInterruptValid(void);

/**
 * @brief Clear comparator interrupt flag
 */
void clearCompInterruptFlag(void);

/**
 * @brief Clear commutation timer interrupt flag
 */
void clearCommutationTimerFlag(void);

/**
 * @brief Clear control loop timer interrupt flag
 */
void clearControlLoopTimerFlag(void);

/* ========================================================================== */
/*                      EXTERNAL DEPENDENCIES                                 */
/* ========================================================================== */

/* Core callback functions - implemented in main.c or commutation.c */
extern void interruptRoutine(void);
extern void PeriodElapsedCallback(void);
extern void tenKhzRoutine(void);
extern void transfercomplete(void);
extern void processDshot(void);

#ifdef USE_ADC
extern void ADC_DMA_Callback(void);
#endif

/* Interrupt control variables */
extern uint32_t average_interval;
extern char rising;
extern char dshot;
extern char servoPwm;

#endif /* INC_INTERRUPT_HANDLERS_H_ */
