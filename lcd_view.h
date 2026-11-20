/*
 * lcd_view.h
 *
 *  Created on: 2026. nov. 7.
 *      Author: Lin Chen, Tóth Péter
 */

#ifndef LCD_VIEW_H_
#define LCD_VIEW_H_

//initialize the LCD display
void LCDView_Init();

//draw the current game state
void LCDView_Draw();

//draw the score
void LCDView_DrawScore();

#endif /* LCD_VIEW_H_ */
