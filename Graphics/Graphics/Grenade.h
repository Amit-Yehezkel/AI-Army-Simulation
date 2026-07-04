#pragma once
#include "Bullet.h"

const int NUM_BULLETS = 20;

class Grenade
{
private:
    double x, y;
    bool isExploding;
    Bullet* bullets[NUM_BULLETS];
public:
    Grenade(double posX, double posY);
    void Show();
    void Explode(int map[MSZ][MSZ]);
    void SetIsExploding(bool value);
    void CreateSecurityMap(int map[MSZ][MSZ], double smap[MSZ][MSZ]);
};
