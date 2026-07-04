#include "Grenade.h"

Grenade::Grenade(double posX, double posY)
{
    int i;
    double alpha, teta = 2 * PI / NUM_BULLETS;
    x = posX;
    y = posY;

    for (i = 0, alpha = 0; i < NUM_BULLETS; i++, alpha += teta)
    {
        bullets[i] = new Bullet(x, y, alpha, true);
    }

    isExploding = false;
}

void Grenade::Show()
{
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->Show();
}

void Grenade::Explode(int map[MSZ][MSZ])
{
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->Move(map);
}

void Grenade::SetIsExploding(bool value)
{
    isExploding = value;
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->SetIsMoving(value);
}

void Grenade::CreateSecurityMap(int map[MSZ][MSZ], double smap[MSZ][MSZ])
{
    int i;
    for (i = 0; i < NUM_BULLETS; i++)
        bullets[i]->CreateSecurityMap(map, smap);
}
