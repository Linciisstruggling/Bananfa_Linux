/***************************************************************************//**
 * @file
 * @brief Top level application functions
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

/***************************************************************************//**
 * Initialize application.
 ******************************************************************************/
#include "app.h"
#include"game.h" //actual gaming functions
#include"lcd_view.h"
#include"uart_comm.h"  //usart initialization and communication

#include "sl_sleeptimer.h" //to provide non-blocking timing for the banana animation

#include<stdint.h>
#include<stdbool.h>
#include<stdlib.h>


//banana animation timing
static uint32_t lastStepTick=0;  //time of the previous banana movement
static uint32_t stepTicks=0; //time interval between movements

// Send a null-terminated string over UART
static void sendString(const char *message)
{
  while (*message != '\0') {
      UARTComm_SendChar((uint8_t)*message++);
  }
}

// Report the selected level as a complete line on the serial terminal
static void reportSpeed(void)
{
  sendString("\r\nSebesseg: ");
  UARTComm_SendChar((uint8_t)('0' + Game_GetSpeedLevel()));
  sendString("/5\r\n");
}

void app_init(void)
{
  //initialize the game state
  Game_Init();

  //initialize and update the LCD
  LCDView_Init();
  LCDView_Draw();
  LCDView_DrawScore();

  //initialize UART communication
  UARTComm_Init();

  //tell the user how the game starts
  sendString("\r\n s betuvel lehet elinditani a jatekot\r\n");

  //report the current speed on the serial terminal
  reportSpeed();
}

/***************************************************************************//**
 * App ticking function.
 ******************************************************************************/
void app_process_action(void)
{
    uint8_t ch;

    // Check for received UART characters
    if (UARTComm_ReadChar(&ch))
    {
        // Echo the received character
        UARTComm_SendChar(ch);

        // Move the basket left
        if (ch == 'b' && Game_IsRunning())
        {
            Game_MoveLeft();
            LCDView_Draw();
        }
        // Move the basket right
        else if (ch == 'j' && Game_IsRunning())
        {
            Game_MoveRight();
            LCDView_Draw();
        }

        else if(ch=='+' || ch=='-'){
            if(!Game_IsRunning()){  //adjust the speed of the game if the game is not currently running
                if(ch=='+'){
                    Game_SpeedUp();
                }
                else{
                    Game_SlowDown();
                }
            }
            else {
                sendString("\r\nSebesseg csak a jatek elott allithato\r\n");
            }
            reportSpeed();
        }

        else if(ch=='s' && !Game_IsRunning()){ //start the game if the game isn't already started
            //seed the random generator using user timing
            srand(sl_sleeptimer_get_tick_count());

            //start a new game
            Game_Start();
            reportSpeed();

            //configure the selected animation speed
            stepTicks=sl_sleeptimer_ms_to_tick(
                Game_GetStepDurations());

            lastStepTick=sl_sleeptimer_get_tick_count();

            //reset the display
            LCDView_DrawScore();
            LCDView_Draw();
        }
        else if(ch=='s' && Game_IsRunning()){ //if the s character was received during the game, tell the user it won't work
            sendString("\r\nJatek kozben nem lehet ujrainditani\r\n");
        }
    }

    // Read the current timer value
    uint32_t now = sl_sleeptimer_get_tick_count();

    // Check whether the banana should move
    if (Game_IsRunning() &&
         (uint32_t)(now - lastStepTick) >= stepTicks)
    {
        lastStepTick = now;

        // Advance the banana and check for landing
        bool landed = Game_NextBananaPhase();

        // Update the score only after landing
        if (landed)
          {
            LCDView_DrawScore();
          }

        // Update the basket and banana display
        LCDView_Draw();
    }
}
