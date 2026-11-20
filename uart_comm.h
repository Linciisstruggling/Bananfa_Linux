/*
 * uart_comm.h
 *
 *  Created on: 2026. nov. 7.
 *      Author: Lin Chen, Tóth Péter
 */

#ifndef UART_COMM_H_
#define UART_COMM_H_

#include <stdint.h>
#include <stdbool.h>

// Initialize UART communication
void UARTComm_Init(void);

// Read a character if available
bool UARTComm_ReadChar(uint8_t *ch);

// Send a character
void UARTComm_SendChar(uint8_t ch);

#endif /* UART_COMM_H_ */
