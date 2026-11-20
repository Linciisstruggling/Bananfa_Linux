/*
 * game.h
 *
 *  Created on: 2026. nov. 7.
 *      Author: Lin Chen, Tóth Péter
 */

#ifndef GAME_H_
#define GAME_H_

#include<stdint.h>
#include<stdbool.h>

//initialize the game state
void Game_Init();
void Game_Start();

//move the basket
void Game_MoveLeft();
void Game_MoveRight();

//advance the banana animation
bool Game_NextBananaPhase(); //return true when a banana lands

//adjust the speed before starting
void Game_SpeedUp();
void Game_SlowDown();

//get the current position
uint8_t Game_GetBasketCol();
uint8_t Game_GetBananaCol();
uint8_t Game_GetBananaPhase();

//get the score
uint8_t Game_GetFallenCount();
uint8_t Game_GetCaughtCount();

//get the speed settings
uint8_t Game_GetSpeedLevel();
uint16_t Game_GetStepDurations();

//check whether the game has finished
bool Game_IsFinished();

//check whether the game has started
bool Game_IsRunning();

#endif /* GAME_H_ */
