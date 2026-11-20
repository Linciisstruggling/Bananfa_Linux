/*
 * lcd_view.c
 *
 *  Created on: 2026. nov. 7.
 *      Author: Lin Chen, Tóth Péter
 */
/***************************************************************************//**
visual display of the game on the LCD:
*displays the basket position and falling banana animation using individual segments
*updates the score to show the number of fallen and caught bananas
 ******************************************************************************/

#include "lcd_view.h"
#include"game.h"

#include"segmentlcd.h"
#include"segmentlcd_individual.h"

#include<stdint.h>

void LCDView_Init()
{
    // Initialize the LCD hardware
    SegmentLCD_Init(false);
}

void LCDView_Draw()
{
    // Create an empty display buffer
    SegmentLCD_LowerCharSegments_TypeDef frame[
        SEGMENT_LCD_NUM_OF_LOWER_CHARS] = {0};

    uint8_t basket = Game_GetBasketCol();
    uint8_t banana = Game_GetBananaCol();
    uint8_t phase = Game_GetBananaPhase();

    // Draw the basket
    frame[basket].d = 1;

    // Draw the banana only while the game is running
    if (Game_IsRunning())
    {
        if (phase == 0)
        {
            frame[banana].a = 1;
        }
        else if (phase == 1)
        {
            frame[banana].j = 1;
        }
        else
        {
            frame[banana].p = 1;
        }
    }

    // Update the lower LCD
    SegmentLCD_LowerSegments(frame);
}

void LCDView_DrawScore()
{
    uint8_t fallen = Game_GetFallenCount();
    uint8_t caught = Game_GetCaughtCount();

    //combine the two scores into four digits
    int displayValue=fallen*100+caught;

    //display the score
    SegmentLCD_Number(displayValue);

    // Enable the colon
    SegmentLCD_Symbol(LCD_SYMBOL_COL10, 1);
}
