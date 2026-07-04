#pragma once
const int MSZ = 100;
const int ROCK = 10;
const int TREE = 7;
const int WATER = 5;
const int WAREHOUSE = 1;
const int EMPTY = 0;
const double PI = 3.14;

const int VISION_RANGE_CELLS = 50;
const unsigned char TERRAIN_UNKNOWN = 255;

const double SOLDIER_SPEED = 0.3;
const double BULLET_SPEED = 0.9;

enum class Team { DEFENSE, ATTACK };

const int SOLDIER_EMPTY = -1;

const int DEF_NUM_C = 1;
const int DEF_NUM_P = 1;
const int DEF_NUM_M = 1;
const int DEF_NUM_W = 6;

const int ATK_NUM_C = 1;
const int ATK_NUM_P = 1;
const int ATK_NUM_M = 1;
const int ATK_NUM_W = 4;