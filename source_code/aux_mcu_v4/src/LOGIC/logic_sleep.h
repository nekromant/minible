/*
 * logic_sleep.h
 *
 * Created: 30/07/2019 20:09:48
 *  Author: limpkin
 */ 


#ifndef LOGIC_SLEEP_H_
#define LOGIC_SLEEP_H_

/* debug defines */
//#define DEBUG_LOG_DISABLED
#if defined SLEEP_LOGIC_LOG_DISABLED
	#define DBG_SLP_LOG(...)    ()
#else
	#define DBG_SLP_LOG         comms_usb_debug_printf
#endif

/* Prototypes */
void logic_sleep_set_ble_to_sleep_between_events(void);
void logic_sleep_ble_not_sleeping_between_events(void);
void logic_sleep_set_awoken_by_no_comms(void);
void logic_sleep_ble_signal_to_sleep(void);
void logic_sleep_set_awoken_by_ble(void);
void logic_sleep_routine_ble_call(void);


#endif /* LOGIC_SLEEP_H_ */