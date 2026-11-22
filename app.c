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

#include "em_cmu.h"  //enables the GPIO clock
#include "em_gpio.h"  //reading the physical buttons

#include<stdint.h>
#include<stdbool.h>
#include<stdlib.h>  //srand()
#include<stdio.h>  //snprintf()


//banana animation timing
static uint32_t lastStepTick=0;  //time of the previous banana movement
static uint32_t stepTicks=0; //time interval between movements

// Parse short UART commands and debounce the two physical buttons.
static char setting = 0;  // 'm' = max bananas (25..99), 'v' = speed (1..5)
static uint8_t settingValue = 0;  //number received so far (max 2 digits)
static uint8_t settingDigits = 0; //how many digits have been received
static uint8_t rawButtons = 0;  //button state at the last read (may be bouncing)
static uint8_t stableButtons = 0;  //debounced button state=bit0= left, bit1= right)
static uint32_t buttonTick = 0;  //time of the last raw state change


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

// Send the game state to Linux so it can draw the same game board.
//@STATE basketCol bananaCol bananPhase fallen caught speed running finished maxBananas
static void reportState(void)
{
    char text[96];
    snprintf(text, sizeof(text), "\r\n@STATE %u %u %u %u %u %u %u %u %u\r\n",
             (unsigned)Game_GetBasketCol(), (unsigned)Game_GetBananaCol(),
             (unsigned)Game_GetBananaPhase(), (unsigned)Game_GetFallenCount(),
             (unsigned)Game_GetCaughtCount(), (unsigned)Game_GetSpeedLevel(),
             Game_IsRunning() ? 1u : 0u, Game_IsFinished() ? 1u : 0u,
             (unsigned)Game_GetMaxBananas());
    sendString(text);
}

// Check PB0/PB1 without blocking the banana animation or UART.
//returns true if the basket was moved
static bool checkButtons(uint32_t now)
{
  //a pressed button reads 0
  //bit0 = left button (pin B9), bit1 = right button (pin B10)
  uint8_t buttons = (GPIO_PinInGet(gpioPortB, 9) == 0 ? 1u : 0u)
                        | (GPIO_PinInGet(gpioPortB, 10) == 0 ? 2u : 0u);

  if (buttons != rawButtons) { //the reading changed, remember it and restart the debounce timer
      rawButtons = buttons;
      buttonTick = now;
  }

  else if (buttons != stableButtons &&
      (uint32_t)(now - buttonTick) >= sl_sleeptimer_ms_to_tick(30)) {

      //the new state has been steady for 30 ms, so accept it
      //'pressed contains only the buttons that were just pressed (not released)
      uint8_t pressed = buttons & (uint8_t)~stableButtons;
      stableButtons = buttons;

      //buttons only move the basket while a game is running and only one button is pressed at one time
      if (Game_IsRunning() && pressed && buttons != 3u) {
          if (pressed & 1u){
              Game_MoveLeft();
          }

          else {
              Game_MoveRight();
          }

          LCDView_Draw();
          return true;
      }
  }
  return false;
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

  // Enable the two physical buttons (active-low PB0/PB1).
  CMU_ClockEnable(cmuClock_GPIO, true);  //GPIO peripheral need its clock before use

  GPIO_PinModeSet(gpioPortB, 9, gpioModeInput, 0);  //PB0 ->pin B9, left
  GPIO_PinModeSet(gpioPortB, 10, gpioModeInput, 0);  //PB1 ->pin B10, right

  //store the starting state, so a button held during boot is not seen as a new press
    rawButtons = stableButtons =
  (GPIO_PinInGet(gpioPortB, 9) == 0 ? 1u : 0u)
      | (GPIO_PinInGet(gpioPortB, 10) == 0 ? 2u : 0u);

  // Show the controls and tell the Linux client the current settings.
    sendString("\r\nBananfa: s=start, x=reset, b=left, j=right; m25..m99=limit\r\n");

  // Report the initial settings
  reportSpeed();
  reportState();
}

/***************************************************************************//**
 * App ticking function.
 ******************************************************************************/
void app_process_action(void)
{
    uint8_t ch;
    bool changed=false; //set to true whenever something changed that Linux client should know about

    // Check for received UART characters
    if (UARTComm_ReadChar(&ch))
    {
        // Echo the received character
        UARTComm_SendChar(ch);

        // Linux sends m50\n (50 bananas) or v3\n (speed level 3).
        // Keep UART output framed: only @STATE lines contain game data.
        if (ch == 'x' || ch == 'X') {
            // Reset immediately, even if a setting command was in progress.
            setting = 0;
            settingDigits = 0;
            settingValue = 0;
            Game_Reset();  //clears board, basket and scores, keeps speed and banana limit
            LCDView_DrawScore();  // Show 00 / 00 on the LCD.
            LCDView_Draw();       // Restore the initial basket and empty board.
            changed = true;
        }
        else if (setting) {
            //we are in the middle of an 'm' or 'v' command, collect up to 2 digits
            if (ch >= '0' && ch <= '9' && settingDigits < 2) {
                settingValue = (uint8_t)(settingValue * 10 + ch - '0');
                settingDigits++;
            }
            else {
                //any non-digit ends the command
                if ((ch == '\r' || ch == '\n') && settingDigits) {
                    // The game module validates 25..99 bananas and 1..5 speed.
                    bool accepted = (setting == 'm')
                                    ? Game_SetMaxBananas(settingValue)
                                        : Game_SetSpeedLevel(settingValue);
                    if (!accepted) sendString("\r\n@ERR CONFIG\r\n");
                    changed = true;  // Report the actual value back to Linux.
                }
                setting = 0;
                settingValue = 0;
                settingDigits = 0;
            }
        }

        else if (ch == 'm' || ch == 'v') { //start of a setting command, remember which one and wait for the digits
            setting = (char)ch;
            settingValue = 0;
            settingDigits = 0;
        }

        else if (ch == '?') {  //status request from linux, just report the current state
            changed = true;
        }

        // Move the basket left
        if (ch == 'b' && Game_IsRunning())
        {
            Game_MoveLeft();
            LCDView_Draw();
            changed = true;
        }
        // Move the basket right
        else if (ch == 'j' && Game_IsRunning())
        {
            Game_MoveRight();
            LCDView_Draw();
            changed = true;
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
            changed = true;
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

            lastStepTick=sl_sleeptimer_get_tick_count(); //the first banana step is counted from the moment of the start

            //reset the display
            LCDView_DrawScore();
            LCDView_Draw();
            changed = true;
        }
        else if(ch=='s' && Game_IsRunning()){ //if the s character was received during the game, tell the user it won't work
            sendString("\r\nJatek kozben nem lehet ujrainditani\r\n");
        }
    }

    // Read the current timer value
    uint32_t now = sl_sleeptimer_get_tick_count();

    if(checkButtons(now)){  //poll the physical buttons
        changed = true;
    }

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
        changed = true;
      }

      if (changed) {  //send the new state to Linux if anything changed
          reportState();
      }
}
