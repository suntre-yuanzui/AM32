/*
 * commutation.h
 *
 *  Created on: Jan 15, 2026
 *      Author: Extracted from AM32 main.c for porting
 *  Description: Six-step commutation control for BLDC motors
 *
 *  NOTE: This is a STANDALONE extracted module for porting to other projects.
 *        Do NOT include this file in the original AM32 project.
 *
 *  Related Files:
 *  - interrupt_handlers.h/c - Interrupt service routines
 *  - comparator.h/c - BEMF comparator control (need to implement)
 *  - phaseouts.h/c - Phase switching implementation (need to implement)
 */

#ifndef INC_COMMUTATION_H_
#define INC_COMMUTATION_H_

#include <stdint.h>

/* Forward declarations - define these types in your project */
#ifndef EEPROM_T_DEFINED
typedef struct {
    uint8_t auto_advance;
    // Add other needed fields from your EEprom_t
} EEprom_t;
#define EEPROM_T_DEFINED
#endif

/* Commutation Control Functions */
void commutate(void);
void getBemfState(void);
void interruptRoutine(void);
void PeriodElapsedCallback(void);
void zcfoundroutine(void);
void startMotor(void);

/* Exported Variables - need to be defined in main.c */
extern char step;
extern char forward;
extern char rising;
extern char old_routine;
extern char prop_brake_active;
extern char desync_check;
extern char running;
extern char zcfound;

extern uint8_t bemfcounter;
extern uint8_t bad_count;
extern uint8_t bad_count_threshold;
extern uint8_t filter_level;
extern uint8_t min_bemf_counts_up;
extern uint8_t min_bemf_counts_down;

extern uint16_t lastzctime;
extern uint16_t thiszctime;
extern uint16_t waitTime;
extern uint16_t duty_cycle;

extern uint32_t commutation_interval;
extern uint32_t average_interval;
extern uint32_t zero_crosses;
extern uint32_t polling_mode_changeover;

extern uint16_t commutation_intervals[6];
extern int e_com_time;
extern uint16_t advance;
extern uint8_t temp_advance;
extern uint8_t auto_advance_level;

/* From eepromBuffer */
extern EEprom_t eepromBuffer;

#endif /* INC_COMMUTATION_H_ */
