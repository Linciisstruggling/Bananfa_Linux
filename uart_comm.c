/*
 * uart_comm.c
 *
 *  Created on: 2026. nov. 7.
 *      Author: Lin Chen, Tóth Péter
 */
/***************************************************************************//**
 This file handles UART communication between the microcontroller and the PC through the
 virtual COM port
 ******************************************************************************/
#include"uart_comm.h"

#include"em_cmu.h"
#include"em_gpio.h"
#include"em_usart.h"

void UARTComm_Init(){
  //enable peripheral clocks
    CMU_ClockEnable(cmuClock_HFPER, true);
    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockEnable(cmuClock_UART0, true);

  //enable the virtual COM port
    GPIO_PinModeSet(gpioPortF, 7, gpioModePushPull, 1);

  //configure TX and RX pins
    GPIO_PinModeSet(gpioPortE, 0, gpioModePushPull, 1);
    GPIO_PinModeSet(gpioPortE, 1, gpioModeInput, 0);

   //configure UART0 for 115200 baud, 8N1
    USART_InitAsync_TypeDef uartInit= USART_INITASYNC_DEFAULT;

    uartInit.baudrate=115200;
    USART_InitAsync(UART0, &uartInit);

   //route UART0 signals to PE0 and PE1
    UART0->ROUTE = UART_ROUTE_LOCATION_LOC1
        |UART_ROUTE_TXPEN
        |UART_ROUTE_RXPEN;
}

bool UARTComm_ReadChar(uint8_t *ch){  //check whether a character has arrived
   if(USART_StatusGet(UART0) & USART_STATUS_RXDATAV) //RXDATAV shows that there's readable data in the receiver buffer
       {
         //store the received character
         *ch=USART_RxDataGet(UART0);
         return true;
        }
    return false;
}

void UARTComm_SendChar(uint8_t ch){
  //transmit a character
  USART_Tx(UART0, ch);
}
