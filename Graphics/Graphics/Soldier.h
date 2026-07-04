#pragma once
#include "glut.h"
#include "Definitions.h"
#include <vector>
#include <utility>
#include <queue>
#include <algorithm>
#include <limits>

enum class Group { DEFENSE, ATTACK };

enum class CommandType {
    MEDIC_AID,
    AMMO_SUPPLY,
    ATTACK_ORDER,
    DEFEND_ORDER,
    MOVE_ORDER
};

struct Command {
    CommandType   type;
    int           priority;
    int           x, y;
    int           requesterId;
    unsigned long timestamp;
};

extern std::queue<Command> gMedicQueueDefense;
extern std::queue<Command> gMedicQueueAttack;
extern std::queue<Command> gSupplyQueueDefense;
extern std::queue<Command> gSupplyQueueAttack;

extern std::queue<Command> gWarriorQueueDefense[2];
extern std::queue<Command> gWarriorQueueAttack[2];

class Commander;
extern Commander* gDefenseCommander;
extern Commander* gAttackCommander;

class Soldier {
public:
    static constexpr int VISION_RANGE_CELLS = 15;
    static constexpr unsigned char TERRAIN_UNKNOWN = 255;

protected:
    int   x, y;
    Group group;
    char  symbol;

    int health = 100;
    static constexpr int MAX_HEALTH = 100;
    static constexpr int INCAP_THRESHOLD = 25;

    bool requestedMedic = false;

    mutable int lastFovX = -1;
    mutable int lastFovY = -1;
    mutable std::vector<char> fovMask;
    mutable std::vector<std::pair<int, int>> fovCells;

public:
    Soldier(int x, int y, Group g, char s) : x(x), y(y), group(g), symbol(s) {}
    virtual ~Soldier() = default;

    int   getX()        const { return x; }
    int   getY()        const { return y; }
    Group getGroup()    const { return group; }
    char  getSymbol()   const { return symbol; }
    int   GetHealth()   const { return health; }
    bool  IsAlive()     const { return health > 0; }
    bool  IsIncapacitated() const { return IsAlive() && health <= INCAP_THRESHOLD; }
    bool  CanAct()      const { return IsAlive() && !IsIncapacitated(); }

    const std::vector<std::pair<int, int>>& GetFOVCells() const { return fovCells; }
    bool CanSeeCell(int cx, int cy) const {
        if (fovMask.empty()) return false;
        if (cx < 0 || cy < 0 || cx >= MSZ || cy >= MSZ) return false;
        return fovMask[cy * MSZ + cx] != 0;
    }

    void ApplyDamage(int dmg);
    void Heal(int amount);

    virtual void Draw() const;

    bool FindPathTo(int tx, int ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        std::vector<std::pair<int, int>>& outPath) const;

    static void BuildSafetyCostMap(const int map[MSZ][MSZ], double outCost[MSZ][MSZ]);

    void InvalidateFOV() const { lastFovX = -1; lastFovY = -1; }
    void RecomputeFOVIfNeeded(const int map[MSZ][MSZ]) const;
    static bool IsOpaqueTile(int t);
    static bool LineOfSight(int x0, int y0, int x1, int y1, const int map[MSZ][MSZ]);

    static bool IsCleanShot(double x0, double y0, double x1, double y1, const int map[MSZ][MSZ]);

protected:
    void setPos(int nx, int ny);
};

class Commander : public Soldier
{
public:
    Commander(int x, int y, Group g);

    void EnqueueRequest(const Command& cmd);

    void Update(const int map[MSZ][MSZ],
        int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ]);

    struct TeamIntel {
        unsigned char knownTerrain[MSZ][MSZ];
        bool          visibleNow[MSZ][MSZ];
        std::vector<std::pair<int, int>> seenEnemies;
    };

    void ResetIntel();
    void UpdateIntel(const std::vector<Soldier*>& friendlies,
        const std::vector<Soldier*>& hostiles,
        const int map[MSZ][MSZ]);

    void BuildTeamAwareSafety(const int baseMap[MSZ][MSZ],
        double outCost[MSZ][MSZ],
        const TeamIntel& enemyIntel) const;

    const TeamIntel& GetIntel() const { return intel; }

private:
    enum class State { AssessSafety, Relocate, Moving, ProcessQueue };
    State state;
    int   tickCounter;
    static const int  SAFETY_CHECK_EVERY = 20;
    static constexpr double SAFE_THRESHOLD = 0.60;

    struct CmdLess {
        bool operator()(const Command& a, const Command& b) const {
            if (a.priority != b.priority) return a.priority > b.priority;
            return a.timestamp > b.timestamp;
        }
    };
    std::priority_queue<Command, std::vector<Command>, CmdLess> mainQueue;

    std::queue<Command> supplyQueue;
    std::queue<Command> warriorQueue;

    TeamIntel intel;

    std::vector<std::pair<int, int>> path;
    size_t pathIdx = 0;

    bool isSafeHere(const double safety[MSZ][MSZ]) const;

    bool findSafeSpot(int& tx, int& ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ]) const;

    bool planPathTo(int tx, int ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ]);

    void stepMove(int unitOcc[MSZ][MSZ]);

    void processMainQueue();
    void tryDispatchBasics();
};

class Medic : public Soldier {
public:
    Medic(int x, int y, Group g);

    void Update(const int map[MSZ][MSZ],
        int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        const std::vector<Soldier*>& friendlies,
        const std::vector<Soldier*>& enemies);

private:
    enum class State { IdleSafe, GoGetBandage, GoToOrderLocation, SeekAndHeal, ReturnToSafe };
    State state = State::IdleSafe;

    bool hasBandage = false;

    std::vector<std::pair<int, int>> path;
    size_t pathIdx = 0;

    bool   hasOrder = false;
    int    orderX = -1, orderY = -1;

    bool findSafeSpot(int& tx, int& ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        const std::vector<Soldier*>& enemies) const;

    bool isSafeHere(const double safety[MSZ][MSZ]) const;

    bool findNearestWarehouseAdj(int& tx, int& ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ]) const;

    bool planPathTo(int tx, int ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ]);

    void stepMove(int unitOcc[MSZ][MSZ]);

    bool findLowestHealthAllyAndApproachCell(const std::vector<Soldier*>& friendlies,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        int& approachX, int& approachY,
        Soldier*& woundedOut) const;

    void healAlly(Soldier* ally);
};

class Provider : public Soldier {
public:
    Provider(int x, int y, Group g);

    void Update(const int map[MSZ][MSZ],
        int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        const std::vector<Soldier*>& friendlies,
        const std::vector<Soldier*>& enemies);

private:
    enum class State { IdleSafe, GoGetAmmo, GoToOrderLocation, SeekAndSupply, ReturnToSafe };
    State state = State::IdleSafe;

    bool hasAmmo = false;
    bool hasOrder = false;
    int  orderX = -1, orderY = -1;

    std::vector<std::pair<int, int>> path;
    size_t pathIdx = 0;

    bool isSafeHere(const double safety[MSZ][MSZ]) const;

    bool findSafeSpot(int& tx, int& ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        const std::vector<Soldier*>& enemies) const;

    bool findNearestWarehouseAdj(int& tx, int& ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ]) const;

    bool planPathTo(int tx, int ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ]);

    void stepMove(int unitOcc[MSZ][MSZ]);

    bool findBestAmmoTargetAndApproachCell(const std::vector<Soldier*>& friendlies,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        int centerX, int centerY,
        int& approachX, int& approachY,
        Soldier*& targetOut) const;

    void giveAmmoTo(Soldier* ally);
};

class Warrior : public Soldier {
public:
    Warrior(int x, int y, Group g) : Soldier(x, y, g, 'W'), noEnemySeenTicks(0) {
        grenades = 1;
    }

    int  GetGrenades()   const { return grenades; }
    bool HasGrenades()   const { return grenades > 0; }
    void AddGrenades(int n) { if (n > 0) grenades += n; }
    bool ConsumeGrenade() { if (grenades > 0) { --grenades; return true; } return false; }

    void OnAmmoSupplied(int n);
    bool NeedsAmmo() const { return grenades <= LOW_AMMO_THRESHOLD; }

    void  SetFrontIndex(int idx) { frontIndex = (idx == 0 ? 0 : 1); }
    int   GetFrontIndex() const { return frontIndex; }

    void TryShoot(const int map[MSZ][MSZ],
        const std::vector<Soldier*>& allies,
        const std::vector<Soldier*>& enemies);
    void TickCooldown();

    void Update(const int map[MSZ][MSZ],
        int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        bool hasCommander,
        const std::vector<Soldier*>& friendlies,
        const std::vector<Soldier*>& enemies);

    void StartSearchPattern();
    void CancelSearchPattern();

    bool HasFinishedSearchCycles() const { return searchFinished; }
    bool IsSearching() const { return state == State::Searching; }
    bool IsAvailableForSearch() const {
        return CanAct() && state == State::Hold;
    }

private:
    int grenades = 0;
    int fireCooldown = 0;

    bool requestedAmmo = false;
    static constexpr int LOW_AMMO_THRESHOLD = 0;

    enum class State { Hold, GoToTarget, Searching, FortressPatrol, GlobalHunt };
    State state = State::Hold;

    std::vector<std::pair<int, int>> path;
    size_t pathIdx = 0;

    bool planPathTo(int tx, int ty,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ]);

    bool FindVantagePoint(int targetX, int targetY,
        const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        int& outX, int& outY);

    bool FindSafeRetreatBFS(const int map[MSZ][MSZ],
        const int unitOcc[MSZ][MSZ],
        const double safety[MSZ][MSZ],
        int& outX, int& outY);

    void stepMove(int unitOcc[MSZ][MSZ]);

    int frontIndex = 0;

    int  baseSearchX = 0, baseSearchY = 0;
    int  searchCycle = 0;
    int  searchPhase = 0;
    bool searchGoingBack = false;
    bool searchFinished = false;

    int  noEnemySeenTicks;

    static constexpr int    NO_ENEMY_SEARCH_THRESHOLD = 80;
    static constexpr double DANGER_SAFETY_THRESHOLD = 0.90;
    static constexpr int    LOW_HEALTH_THRESHOLD = 40;

    static constexpr int    DEFEND_BFS_RADIUS = 10;
    static constexpr double DEFEND_SAFE_THRESHOLD = 0.60;

    bool inFortressPatrol = false;
    int  fortressLoopsCount = 0;
    bool sawDefenseInCurrentLoop = false;
    int  fortressPatrolIndex = 0;
    int  fortressPatrolPoints[4][2]{};

    void StartFortressPatrol();

    bool FindBestGrenadeTarget(const std::vector<Soldier*>& enemies,
        double& outGX, double& outGY, int& outHits) const;
    void ApplyGrenadeExplosion(double gx, double gy,
        const std::vector<Soldier*>& enemies);

    static constexpr int    GRENADE_MIN_ENEMIES = 2;
    static constexpr double GRENADE_RADIUS = 4.0;
    static constexpr int    GRENADE_DAMAGE = 60;

    static constexpr int    BULLET_COOLDOWN_TICKS = 25;
    static constexpr int    GRENADE_COOLDOWN_TICKS = 40;
};
