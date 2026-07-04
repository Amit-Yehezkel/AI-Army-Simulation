#include "Bullet.h"
#include <math.h>
#include <vector>
#include "glut.h"
#include "Soldier.h" 

Bullet::Bullet(double xPos, double yPos, double alpha, bool fromGrenade)
{
    x = xPos;
    y = yPos;

    dirX = cos(alpha);
    dirY = sin(alpha);
    isMoving = false;
    isCreatingSecurityMap = false;
    alive = true;

    isFromGrenade = fromGrenade;
}

void Bullet::Move(int map[MSZ][MSZ])
{
    double tmpX = x, tmpY = y;
    if (isMoving)
    {
        tmpX += BULLET_SPEED * dirX;
        tmpY += BULLET_SPEED * dirY;
        if (tmpX > 0 && tmpX < MSZ && tmpY > 0 && tmpY < MSZ && map[(int)tmpY][(int)tmpX] == EMPTY)
        {
            x = tmpX;
            y = tmpY;
        }
        else isMoving = false;
    }
}

void Bullet::Show()
{
    if (!alive) return;

    if (isFromGrenade)
        glColor3d(0, 0, 0);  
    else
        glColor3d(1, 0, 0);  

    glBegin(GL_POLYGON);
    glVertex2d(x - 0.5, y);
    glVertex2d(x, y + 0.5);
    glVertex2d(x + 0.5, y);
    glVertex2d(x, y - 0.5);
    glEnd();
}

bool Bullet::MoveAndCollide(int map[MSZ][MSZ], const std::vector<Soldier*>& enemies)
{
    if (!alive) return false;

    double tmpX = x, tmpY = y;

    if (!isMoving) return alive;

    tmpX += BULLET_SPEED * dirX;
    tmpY += BULLET_SPEED * dirY;

    if (!(tmpX > 0 && tmpX < MSZ && tmpY > 0 && tmpY < MSZ))
    {
        Kill();
        return false;
    }

    int cx = (int)tmpX;
    int cy = (int)tmpY;

    if (map[cy][cx] == ROCK || map[cy][cx] == TREE || map[cy][cx] == WAREHOUSE)
    {
        Kill();
        return false;
    }

    for (Soldier* e : enemies)
    {
        if (!e || !e->IsAlive()) continue;
        if (e->getX() == cx && e->getY() == cy)
        {
            e->ApplyDamage(25);
            Kill();
            return false;
        }
    }

    x = tmpX;
    y = tmpY;
    return true;
}

void Bullet::CreateSecurityMap(int map[MSZ][MSZ], double smap[MSZ][MSZ])
{
    double tmpX = x, tmpY = y;
    double xsm = x, ysm = y;

    isCreatingSecurityMap = true;
    while (isCreatingSecurityMap)
    {
        tmpX += BULLET_SPEED * dirX;
        tmpY += BULLET_SPEED * dirY;
        if (tmpX > 0 && tmpX < MSZ && tmpY > 0 && tmpY < MSZ && map[(int)tmpY][(int)tmpX] == EMPTY)
        {
            xsm = tmpX;
            ysm = tmpY;
            smap[(int)ysm][(int)xsm] += 0.001;
        }
        else isCreatingSecurityMap = false;
    }
}
