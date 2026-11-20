/*
 * game.c
 *
 *  Created on: 2026. nov. 7.
 *      Author: Lin Chen, Tóth Péter
 */

/***************************************************************************//**
game logic: it manages the basket and banana positions, controls the animation phases
*generates random banana locations, update the score, handles game speed and termination
 ******************************************************************************/

#include "game.h"
#include<stdlib.h>

#define TREE_COUNT 4   //4 banana trees
#define MAX_BANANAS 25  //maximum number of bananas
#define MAX_SPEED_LEVEL 5  //5 speed levels

//Game state
static bool gameRunning=false;

// Current basket position
static uint8_t basketCol = 1;

// Current banana tree
static uint8_t bananaCol = 0;

// Current banana animation phase
static uint8_t bananaPhase = 0;

//score counters
static uint8_t fallen=0;
static uint8_t caught=0;

//current speed level
static uint8_t speedLevel=1;

//duration of one banana phase in milliseconds
static const uint16_t speedDurations[MAX_SPEED_LEVEL]={
  700, 550, 400, 300, 200
}; //AI generated speed duration

/***************************************************************************//**
 *Game status functions
 ******************************************************************************/
void Game_Init()
{
  //initialize the game in waiting state
  gameRunning=false;

  basketCol = 1;
  bananaCol = 0;
  bananaPhase = 0;

  fallen = 0;
  caught = 0;

  speedLevel = 1;
}

void Game_Start(){
  // reset the game and choose the first tree
  basketCol = 1;
  bananaCol = rand() % TREE_COUNT;  //random banana tree
  bananaPhase = 0;

  fallen=0;
  caught=0;

  gameRunning=true;
}

void Game_MoveLeft() //if the game is running and we still have space to move left
{
   if(Game_IsRunning() && basketCol>0){
       basketCol--;
   }
}

void Game_MoveRight() //if the game is running and we still have space to move right
{
   if(Game_IsRunning() && basketCol< TREE_COUNT-1){
       basketCol++;
   }
}

void Game_SpeedUp(){
  //increase speed only outside an active game, and is not yet at maximum speed
  if(!gameRunning && speedLevel< MAX_SPEED_LEVEL){
      speedLevel++;
  }
}

void Game_SlowDown(){
  //decrease speed only outside an active game, and is not yet at minimum speed
  if(!gameRunning && speedLevel>1){
      speedLevel--;
  }
}


bool Game_NextBananaPhase()
{
  // do nothing if the game is not running
  if(!gameRunning){
      return false;
  }

  //show phase 0, 1 and 2 before counting the landing
  if(bananaPhase<2){
      bananaPhase++;
      return false;
  }

  //count the landed banana
  fallen++;

  //check whether the basket caught the banana
  if(basketCol == bananaCol){
      caught++;
  }

  //stop after 25 landed bananas
  if(fallen>= MAX_BANANAS){
      gameRunning=false;
  }
  else{
      //select a random tree for the next banana
      bananaCol=rand()% TREE_COUNT;
      bananaPhase=0;
  }
  return true;
}

/***************************************************************************//**
 *Getter functions
 ******************************************************************************/
uint8_t Game_GetBasketCol()
{
    return basketCol;
}

uint8_t Game_GetBananaCol()
{
    return bananaCol;
}

uint8_t Game_GetBananaPhase()
{
    return bananaPhase;
}

uint8_t Game_GetFallenCount()
{
    return fallen;
}

uint8_t Game_GetCaughtCount()
{
    return caught;
}

uint8_t Game_GetSpeedLevel(){
  return speedLevel;
}

uint16_t Game_GetStepDurations(){
  return speedDurations[speedLevel-1];
}

/***************************************************************************//**
 *Check if game is over
 ******************************************************************************/
bool Game_IsFinished()//game finishes when 25 bananas had dropped
{
    return fallen >= MAX_BANANAS;
}

bool Game_IsRunning(){
  return gameRunning;
}
