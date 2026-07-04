#pragma once
#include "Definitions.h"
#include <vector>

class Soldier;

class Bullet
{
private:
    double x, y;
    bool isMoving, isCreatingSecurityMap;
    double dirX, dirY;

    Team ownerTeam = Team::DEFENSE;
    bool alive = true;

    bool isFromGrenade = false;

public:
    Bullet(double xPos, double yPos, double alpha, bool fromGrenade = false);

    void Move(int map[MSZ][MSZ]);
    void Show();

    bool MoveAndCollide(int map[MSZ][MSZ], const std::vector<Soldier*>& enemies);

    void SetIsMoving(bool value) { isMoving = value; }
    void CreateSecurityMap(int map[MSZ][MSZ], double smap[MSZ][MSZ]);

    void SetOwnerTeam(Team t) { ownerTeam = t; }
    Team GetOwnerTeam() const { return ownerTeam; }
    bool IsAlive() const { return alive; }
    void Kill() { alive = false; isMoving = false; }
};
