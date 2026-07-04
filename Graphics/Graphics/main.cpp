#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include <iostream> 
#include "glut.h"
#include "Definitions.h"
#include "Bullet.h"
#include "Grenade.h"
#include "Soldier.h"

const int NUM_ROCKS = 50;
const int H = 600;
const int W = 600;

int map[MSZ][MSZ] = { 0 };
double smap[MSZ][MSZ] = { 0 };

double gSafetyCostDefense[MSZ][MSZ] = { 0.0 };
double gSafetyCostAttack[MSZ][MSZ] = { 0.0 };

Bullet* pb = nullptr;
Grenade* pg = nullptr;

int unitOcc[MSZ][MSZ];

std::vector<Soldier*> defenders;
std::vector<Soldier*> attackers;

std::vector<Bullet*> gBullets;

static const int WALL_W = 30;
static const int WALL_H = 20;
static const int WATER_W = 32;
static const int WATER_H = 22;

static int gWallX, gWallY, gWallW, gWallH;
static int gEntranceTopY, gEntranceBotY, gEntranceCX;
static int gFortCenterY;
static int gWarehouseTopY, gWarehouseBotY;

static int gNoContactTicks = 0;
static const int NO_CONTACT_STUCK_TICKS = 200;

bool gAttackInfiltrated[2] = { false, false };
bool gEntranceClear[2] = { false, false };

static bool gDefOutsideVisibleNow[2] = { false, false };
static bool gDefOutsideSeenEver[2] = { false, false };

static bool gGameEnded = false;

static int  gTicksSinceLastKill = 0;
static const int NO_KILL_STALE_TICKS = 500; 
bool gGlobalHuntMode = false;

void NotifySoldierDied()
{
    gTicksSinceLastKill = 0;
    gGlobalHuntMode = false;
}

inline void getFortressRect(int& x, int& y, int& w, int& h) {
    w = WALL_W; h = WALL_H;
    x = (MSZ - w) / 2;
    y = (MSZ - h) / 2;
}
inline bool inRect(int cx, int cy, int rx, int ry, int rw, int rh) {
    return (cx >= rx && cx <= rx + rw - 1 && cy >= ry && cy <= ry + rh - 1);
}

inline bool IsInsideFortressCell(int x, int y) {
    int fx, fy, fw, fh;
    getFortressRect(fx, fy, fw, fh);
    int minX = fx + 1;
    int maxX = fx + fw - 2;
    int minY = fy + 1;
    int maxY = fy + fh - 2;
    return (x >= minX && x <= maxX && y >= minY && y <= maxY);
}

inline void ClearUnitOcc() {
    for (int i = 0; i < MSZ; ++i)
        for (int j = 0; j < MSZ; ++j)
            unitOcc[i][j] = SOLDIER_EMPTY;
}

inline bool isCellFreeForUnit(int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= MSZ || cy >= MSZ) return false;
    if (map[cy][cx] != 0) return false;
    if (unitOcc[cy][cx] != SOLDIER_EMPTY) return false;
    return true;
}

static const int MIN_UNIT_DIST = 4;
static const int MIN_UNIT_DIST2 = MIN_UNIT_DIST * MIN_UNIT_DIST;

inline bool isFarFromOthers(int x, int y) {
    auto farFrom = [&](const std::vector<Soldier*>& vec) {
        for (const Soldier* s : vec) {
            int dx = x - s->getX();
            int dy = y - s->getY();
            if (dx * dx + dy * dy < MIN_UNIT_DIST2) return false;
        }
        return true;
        };
    return farFrom(defenders) && farFrom(attackers);
}

inline bool isFarFromTerrain(int x, int y) {
    int r = MIN_UNIT_DIST;
    for (int dy = -r; dy <= r; ++dy) {
        int yy = y + dy;
        if (yy < 0 || yy >= MSZ) continue;
        for (int dx = -r; dx <= r; ++dx) {
            int xx = x + dx;
            if (xx < 0 || xx >= MSZ) continue;
            if (dx * dx + dy * dy >= MIN_UNIT_DIST2) continue;
            if (map[yy][xx] != 0) return false;
        }
    }
    return true;
}

bool pickRandomFreeCellInsideFortress(int& outx, int& outy) {
    int fx, fy, fw, fh;
    getFortressRect(fx, fy, fw, fh);

    int innerX1 = fx + 1;
    int innerY1 = fy + 1;
    int innerW = fw - 2;
    int innerH = fh - 2;
    if (innerW <= 0 || innerH <= 0) return false;

    for (int tries = 0; tries < 12000; ++tries) {
        int x = innerX1 + (rand() % innerW);
        int y = innerY1 + (rand() % innerH);
        if (isCellFreeForUnit(x, y) && isFarFromOthers(x, y) && isFarFromTerrain(x, y)) {
            outx = x; outy = y;
            return true;
        }
    }
    return false;
}

bool pickRandomFreeCellOutsideFortress(int& outx, int& outy) {
    int fx, fy, fw, fh;
    getFortressRect(fx, fy, fw, fh);

    for (int tries = 0; tries < 30000; ++tries) {
        int x = rand() % MSZ;
        int y = rand() % MSZ;

        bool insideFort = (x >= fx && x <= fx + fw - 1 && y >= fy && y <= fy + fh - 1);
        if (insideFort) continue;

        if (isCellFreeForUnit(x, y) && isFarFromOthers(x, y) && isFarFromTerrain(x, y)) {
            outx = x; outy = y;
            return true;
        }
    }
    return false;
}

void PlaceUnits() {
    for (int i = 0; i < DEF_NUM_C; ++i) { int x, y; if (pickRandomFreeCellInsideFortress(x, y)) { defenders.push_back(new Commander(x, y, Group::DEFENSE)); unitOcc[y][x] = 0; } }
    for (int i = 0; i < DEF_NUM_M; ++i) { int x, y; if (pickRandomFreeCellInsideFortress(x, y)) { defenders.push_back(new Medic(x, y, Group::DEFENSE)); unitOcc[y][x] = 0; } }
    for (int i = 0; i < DEF_NUM_P; ++i) { int x, y; if (pickRandomFreeCellInsideFortress(x, y)) { defenders.push_back(new Provider(x, y, Group::DEFENSE)); unitOcc[y][x] = 0; } }
    for (int i = 0; i < DEF_NUM_W; ++i) { int x, y; if (pickRandomFreeCellInsideFortress(x, y)) { defenders.push_back(new Warrior(x, y, Group::DEFENSE)); unitOcc[y][x] = 0; } }

    for (int i = 0; i < ATK_NUM_C; ++i) { int x, y; if (pickRandomFreeCellOutsideFortress(x, y)) { attackers.push_back(new Commander(x, y, Group::ATTACK)); unitOcc[y][x] = 0; } }
    for (int i = 0; i < ATK_NUM_M; ++i) { int x, y; if (pickRandomFreeCellOutsideFortress(x, y)) { attackers.push_back(new Medic(x, y, Group::ATTACK)); unitOcc[y][x] = 0; } }
    for (int i = 0; i < ATK_NUM_P; ++i) { int x, y; if (pickRandomFreeCellOutsideFortress(x, y)) { attackers.push_back(new Provider(x, y, Group::ATTACK)); unitOcc[y][x] = 0; } }
    for (int i = 0; i < ATK_NUM_W; ++i) { int x, y; if (pickRandomFreeCellOutsideFortress(x, y)) { attackers.push_back(new Warrior(x, y, Group::ATTACK)); unitOcc[y][x] = 0; } }
}

void DrawUnits() {
    for (const Soldier* s : defenders) s->Draw();
    for (const Soldier* s : attackers) s->Draw();
}

void CreateRect(int blockType, int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < w; i++) {
        int mid = w / 2;
        if (i != mid - 1 && i != mid && i != mid + 1) {
            map[y][x + i] = blockType;
            map[y + h - 1][x + i] = blockType;
        }
    }
    for (i = 0; i < h; i++) {
        map[y + i][x] = blockType;
        map[y + i][x + w - 1] = blockType;
    }
}

void CreateWarehouse(int x, int y, int w, int h)
{
    int i, j;
    for (i = 0; i < w; i++)
        for (j = 0; j < h; j++)
            map[y + j][x + i] = WAREHOUSE;
}

void generateEnvObjects()
{
    int wallW = WALL_W, wallH = WALL_H;
    int wallX = (MSZ - wallW) / 2;
    int wallY = (MSZ - wallH) / 2;

    int waterW = WATER_W, waterH = WATER_H;
    int waterX = (MSZ - waterW) / 2;
    int waterY = (MSZ - waterH) / 2;

    int warehouseW = 6, warehouseH = 2;
    int wh1x = (MSZ - warehouseW) / 2, wh1y = MSZ - warehouseH;
    int wh2x = (MSZ - warehouseW) / 2, wh2y = 0;
    int wh3x = (MSZ - warehouseW) / 2, wh3y = (MSZ - warehouseH) / 2;

    int bufFort = 5;
    int bufWH = 2;

    int ax1 = wallX - bufFort, ay1 = wallY - bufFort;
    int ax2 = wallX + wallW - 1 + bufFort, ay2 = wallY + wallH - 1 + bufFort;

    int bx1 = waterX - bufFort, by1 = waterY - bufFort;
    int bx2 = waterX + waterW - 1 + bufFort, by2 = waterY + waterH - 1 + bufFort;

    int cx1 = wh1x - bufWH, cy1 = wh1y - bufWH;
    int cx2 = wh1x + warehouseW - 1 + bufWH, cy2 = wh1y + warehouseH - 1 + bufWH;

    int dx1 = wh2x - bufWH, dy1 = wh2y - bufWH;
    int dx2 = wh2x + warehouseW - 1 + bufWH, dy2 = wh2y + warehouseH - 1 + bufWH;

    int ex1 = wh3x - bufWH, ey1 = wh3y - bufWH;
    int ex2 = wh3x + warehouseW - 1 + bufWH, ey2 = wh3y + warehouseH - 1 + bufWH;

    auto clampBox = [](int& x1, int& y1, int& x2, int& y2) {
        if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
        if (x2 >= MSZ) x2 = MSZ - 1; if (y2 >= MSZ) y2 = MSZ - 1;
        };
    clampBox(ax1, ay1, ax2, ay2);
    clampBox(bx1, by1, bx2, by2);
    clampBox(cx1, cy1, cx2, cy2);
    clampBox(dx1, dy1, dx2, dy2);
    clampBox(ex1, ey1, ex2, ey2);

    auto blocked = [&](int x, int y) {
        bool inA = (x >= ax1 && x <= ax2 && y >= ay1 && y <= ay2);
        bool inB = (x >= bx1 && x <= bx2 && y >= by1 && y <= by2);
        bool inC = (x >= cx1 && x <= cx2 && y >= cy1 && y <= cy2);
        bool inD = (x >= dx1 && x <= dx2 && y >= dy1 && y <= dy2);
        bool inE = (x >= ex1 && x <= ex2 && y >= ey1 && y <= ey2);
        return inA || inB || inC || inD || inE;
        };

    int tries = 50;
    for (int k = 0; k < tries; k++) {
        int x = rand() % MSZ;
        int y = rand() % MSZ;

        if (x + 2 >= MSZ || y + 2 >= MSZ) continue;

        bool bad = false;
        for (int dy = 0; dy < 3 && !bad; dy++)
            for (int dx = 0; dx < 3; dx++) {
                int xx = x + dx, yy = y + dy;
                if (blocked(xx, yy) || map[yy][xx] != 0) { bad = true; break; }
            }
        if (bad) continue;

        int t = rand() % 3;
        int type = (t == 0 ? TREE : (t == 1 ? WATER : ROCK));

        for (int dy = 0; dy < 3; dy++)
            for (int dx = 0; dx < 3; dx++)
                map[y + dy][x + dx] = type;
    }
}

void InitMap()
{
    int wallW = WALL_W, wallH = WALL_H;
    int waterW = WATER_W, waterH = WATER_H;
    int warehouseW = 6, warehouseH = 2;
    int waterX = (MSZ - waterW) / 2, waterY = (MSZ - waterH) / 2;

    int wallX = (MSZ - wallW) / 2;
    int wallY = (MSZ - wallH) / 2;

    CreateRect(ROCK, wallX, wallY, wallW, wallH);
    CreateRect(WATER, waterX, waterY, waterW, waterH);
    CreateWarehouse((MSZ - warehouseW) / 2, MSZ - warehouseH, warehouseW, warehouseH);
    CreateWarehouse((MSZ - warehouseW) / 2, 0, warehouseW, warehouseH);
    CreateWarehouse((MSZ - warehouseW) / 2, (MSZ - warehouseH) / 2, warehouseW, warehouseH);

    int eC = waterX + waterW / 2;
    int eL = eC - 1;
    int eR = eC + 1;

    int yTopWall = waterY - 3;
    int yBottomWall = waterY + waterH - 1 + 3;

    if (yTopWall >= 0) {
        map[yTopWall][eL] = ROCK;
        map[yTopWall][eC] = ROCK;
        map[yTopWall][eR] = ROCK;
    }
    if (yBottomWall < MSZ) {
        map[yBottomWall][eL] = ROCK;
        map[yBottomWall][eC] = ROCK;
        map[yBottomWall][eR] = ROCK;
    }

    generateEnvObjects();

    gWallX = wallX; gWallY = wallY; gWallW = wallW; gWallH = wallH;
    gEntranceCX = gWallX + gWallW / 2;
    gEntranceTopY = gWallY - 1;
    gEntranceBotY = gWallY + gWallH;
    gFortCenterY = gWallY + gWallH / 2;

    gWarehouseTopY = 1;
    gWarehouseBotY = MSZ - 3;
}

struct FrontAssignment { std::vector<Warrior*> team; };
struct ArmyFronts { FrontAssignment fronts[2]; };

ArmyFronts gDefenseFronts;
ArmyFronts gAttackFronts;

enum class FrontCase { Case1, Case2, Case3 };
struct FrontState {
    FrontCase state = FrontCase::Case1;
    int sustainTicks = 0;
    int cooldownTicks = 0;
};

static FrontState gDefenseFrontState[2];
static FrontState gAttackFrontState[2];

struct FrontSeekState {
    bool inSearch = false;
    bool enemySeen = false;
};

static FrontSeekState gDefenseFrontSeek[2];
static FrontSeekState gAttackFrontSeek[2];

static const int RATIO_WINDOW_R = 12;
static const int SUSTAIN_REQ = 10;
static const int CASE_COOLDOWN = 30;

static const double SAFE_OK = 0.80;

static void collectWarriors(const std::vector<Soldier*>& vec, std::vector<Warrior*>& out) {
    out.clear();
    for (Soldier* s : vec) if (s->IsAlive()) {
        if (auto* w = dynamic_cast<Warrior*>(s)) out.push_back(w);
    }
}

// --- Helper to count Warriors in a vector ---
static int CountWarriors(const std::vector<Soldier*>& vec) {
    int count = 0;
    for (Soldier* s : vec) {
        if (s && s->IsAlive() && dynamic_cast<Warrior*>(s)) {
            count++;
        }
    }
    return count;
}

static void assignWarriorsToFronts()
{
    std::vector<Warrior*> defW, atkW;
    collectWarriors(defenders, defW);
    collectWarriors(attackers, atkW);

    {
        std::vector<Warrior*> shuffled = atkW;
        std::random_shuffle(shuffled.begin(), shuffled.end());
        int n = (int)shuffled.size();
        int nA = n / 2;
        int nB = n - nA;

        gAttackFronts.fronts[0].team.assign(shuffled.begin(), shuffled.begin() + nA);
        gAttackFronts.fronts[1].team.assign(shuffled.begin() + nA, shuffled.end());
    }

    gDefenseFronts.fronts[0].team.clear();
    gDefenseFronts.fronts[1].team.clear();

    int atk0 = (int)gAttackFronts.fronts[0].team.size();
    int atk1 = (int)gAttackFronts.fronts[1].team.size();

    std::vector<Warrior*> shuffledDef = defW;
    std::random_shuffle(shuffledDef.begin(), shuffledDef.end());

    size_t idx = 0;
    int need0 = atk0;
    int need1 = atk1;

    for (; idx < shuffledDef.size() && need0 > 0; ++idx, --need0) {
        Warrior* w = shuffledDef[idx];
        gDefenseFronts.fronts[0].team.push_back(w);
    }

    for (; idx < shuffledDef.size() && need1 > 0; ++idx, --need1) {
        Warrior* w = shuffledDef[idx];
        gDefenseFronts.fronts[1].team.push_back(w);
    }

    for (; idx < shuffledDef.size(); ++idx) {
        Warrior* w = shuffledDef[idx];
        if (gDefenseFronts.fronts[0].team.size() <= gDefenseFronts.fronts[1].team.size())
            gDefenseFronts.fronts[0].team.push_back(w);
        else
            gDefenseFronts.fronts[1].team.push_back(w);
    }

    for (int f = 0; f < 2; ++f) {
        for (Warrior* w : gDefenseFronts.fronts[f].team) w->SetFrontIndex(f);
        for (Warrior* w : gAttackFronts.fronts[f].team)  w->SetFrontIndex(f);
    }
}

static void countFront(const std::vector<Soldier*>& defendersAll,
    const std::vector<Soldier*>& attackersAll,
    int frontIdx, int& defCount, int& atkCount)
{
    defCount = atkCount = 0;
    int cx = gEntranceCX;
    int cy = (frontIdx == 0 ? gEntranceTopY : gEntranceBotY);

    auto inDisc = [&](int x, int y) {
        int dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= RATIO_WINDOW_R * RATIO_WINDOW_R;
        };

    for (const Soldier* s : defendersAll) {
        if (!s->IsAlive()) continue;
        if (s->getSymbol() != 'W') continue;
        if (inDisc(s->getX(), s->getY())) ++defCount;
    }
    for (const Soldier* s : attackersAll) {
        if (!s->IsAlive()) continue;
        if (s->getSymbol() != 'W') continue;
        if (inDisc(s->getX(), s->getY())) ++atkCount;
    }
}

static void buildTargetsForBand(const int mapArr[MSZ][MSZ],
    const int unitOccArr[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    int targetYCenter, int yAllowance,
    int xSpread,
    std::vector<std::pair<int, int>>& outTargets)
{
    outTargets.clear();

    auto inBounds = [](int x, int y) {
        return x >= 0 && x < MSZ && y >= 0 && y < MSZ;
        };

    int yMin = std::max(0, targetYCenter - yAllowance);
    int yMax = std::min(MSZ - 1, targetYCenter + yAllowance);

    std::vector<int> xOffsets;
    xOffsets.push_back(0);
    for (int d = 1; d <= xSpread; ++d) { xOffsets.push_back(+d); xOffsets.push_back(-d); }

    struct Cand { int x, y; double sc; int dx; };
    std::vector<Cand> cands;

    for (int y = yMin; y <= yMax; ++y) {
        for (int k = 0; k < (int)xOffsets.size(); ++k) {
            int x = gEntranceCX + xOffsets[k];
            if (!inBounds(x, y)) continue;
            if (mapArr[y][x] != 0) continue;
            if (unitOccArr[y][x] != SOLDIER_EMPTY) continue;

            double sc = safety[y][x];
            cands.push_back({ x,y,sc, std::abs(xOffsets[k]) });
        }
    }

    if (cands.empty()) return;

    std::vector<Cand> good, rest;
    for (auto& c : cands) {
        if (c.sc <= SAFE_OK) good.push_back(c);
        else rest.push_back(c);
    }

    auto cmp = [](const Cand& a, const Cand& b) {
        if (a.sc == b.sc) {
            if (a.dx == b.dx) return a.y < b.y;
            return a.dx < b.dx;
        }
        return a.sc < b.sc;
        };

    std::sort(good.begin(), good.end(), cmp);
    std::sort(rest.begin(), rest.end(), cmp);

    for (auto& v : good) outTargets.emplace_back(v.x, v.y);
    for (auto& v : rest) outTargets.emplace_back(v.x, v.y);
}

static void makeFrontTargets(Group side, int frontIdx, FrontCase fc,
    const double safety[MSZ][MSZ],
    std::vector<std::pair<int, int>>& outTargets)
{
    const int Y_ALLOW = 2;
    int xSpread = 20;
    int targetY = (frontIdx == 0 ? gEntranceTopY : gEntranceBotY);

    if (fc == FrontCase::Case2 && side == Group::DEFENSE) {
        xSpread = 8;
        buildTargetsForBand(map, unitOcc, safety, gFortCenterY, 3, xSpread, outTargets);
        return;
    }

    if (fc == FrontCase::Case1) {
        if (side == Group::DEFENSE) {
            targetY = (frontIdx == 0 ? gEntranceTopY + 4 : gEntranceBotY - 4);
            buildTargetsForBand(map, unitOcc, safety, targetY, Y_ALLOW, xSpread, outTargets);
        }
        else {
            targetY = (frontIdx == 0 ? gEntranceTopY - 10 : gEntranceBotY + 10);
            buildTargetsForBand(map, unitOcc, safety, targetY, Y_ALLOW, xSpread, outTargets);
        }
    }
    else if (fc == FrontCase::Case2) {
        if (side == Group::ATTACK) {
            targetY = (frontIdx == 0 ? gEntranceTopY - 3 : gEntranceBotY + 3);
            buildTargetsForBand(map, unitOcc, safety, targetY, 4, xSpread, outTargets);
        }
        else {
            buildTargetsForBand(map, unitOcc, safety, gFortCenterY, 3, xSpread, outTargets);
        }
    }
    else {
        if (side == Group::DEFENSE) {
            targetY = (frontIdx == 0 ? gEntranceTopY - 10 : gEntranceBotY + 10);
            buildTargetsForBand(map, unitOcc, safety, targetY, Y_ALLOW, xSpread, outTargets);
        }
        else {
            targetY = (frontIdx == 0 ? gWarehouseTopY + 2 : gWarehouseBotY - 2);
            buildTargetsForBand(map, unitOcc, safety, targetY, 5, xSpread, outTargets);
        }
    }
}

static void dispatchFrontMoveOrders(Group side, int frontIdx,
    const std::vector<std::pair<int, int>>& targets,
    ArmyFronts& fronts)
{
    auto& team = fronts.fronts[frontIdx].team;
    int n = (int)team.size();
    if (n == 0 || targets.empty()) return;

    int m = std::min(n, (int)targets.size());
    for (int i = 0; i < m; ++i) {
        Command c;
        c.type = CommandType::MOVE_ORDER;
        c.priority = 0;
        c.x = targets[i].first;
        c.y = targets[i].second;
        c.requesterId = 0;
        c.timestamp = 0;
        if (side == Group::DEFENSE) gWarriorQueueDefense[frontIdx].push(c);
        else                         gWarriorQueueAttack[frontIdx].push(c);
    }
}

static FrontCase NextCaseTowardEnemy(Group side, FrontCase cur)
{
    if (side == Group::ATTACK)
    {
        if (cur == FrontCase::Case1) return FrontCase::Case2;
        if (cur == FrontCase::Case2) return FrontCase::Case3;
        return FrontCase::Case3;
    }
    else
    {
        if (cur == FrontCase::Case2) return FrontCase::Case1;
        if (cur == FrontCase::Case1) return FrontCase::Case3;
        return FrontCase::Case3;
    }
}

static void AdvanceFrontTowardEnemy(Group side,
    int frontIdx,
    FrontState* stArr,
    ArmyFronts& fronts,
    const double safety[MSZ][MSZ])
{
    FrontState& FS = stArr[frontIdx];
    FrontCase newCase = NextCaseTowardEnemy(side, FS.state);

    if (newCase != FS.state) {
        FS.state = newCase;
        FS.cooldownTicks = CASE_COOLDOWN;
        FS.sustainTicks = 0;
    }

    std::vector<std::pair<int, int>> targets;
    makeFrontTargets(side, frontIdx, FS.state, safety, targets);
    dispatchFrontMoveOrders(side, frontIdx, targets, fronts);
}

void DrawMap()
{
    for (int i = 0; i < MSZ; i++)
        for (int j = 0; j < MSZ; j++)
        {
            switch (map[i][j])
            {
            case 0:         glColor3d(0.7, 0.9, 0.7); break;
            case ROCK:      glColor3d(0.6, 0.6, 0.6); break;
            case WATER:     glColor3d(0.027, 0.647, 0.961); break;
            case TREE:      glColor3d(0.0, 0.7, 0.0); break;
            case WAREHOUSE: glColor3d(1, 0.984, 0); break;
            }
            glBegin(GL_POLYGON);
            glVertex2d(j, i);
            glVertex2d(j, i + 1);
            glVertex2d(j + 1, i + 1);
            glVertex2d(j + 1, i);
            glEnd();
        }
}

void CreateSecurityMap()
{
    Grenade* gr[50];
    for (int i = 0; i < 50; i++)
    {
        gr[i] = new Grenade(MSZ / 2 - 5 + rand() % 10, MSZ / 2 - 5 + rand() % 10);
        gr[i]->CreateSecurityMap(map, smap);
    }
}

void DrawSecurityMap()
{
    for (int i = 0; i < MSZ; i++)
        for (int j = 0; j < MSZ; j++)
        {
            switch (map[i][j])
            {
            case 0:
                glColor3d(1 - smap[i][j], 1 - smap[i][j], 1 - smap[i][j]);
                break;
            case ROCK:
                glColor3d(1, 0, 1);
                break;
            }
            glBegin(GL_POLYGON);
            glVertex2d(j, i);
            glVertex2d(j, i + 1);
            glVertex2d(j + 1, i + 1);
            glVertex2d(j + 1, i);
            glEnd();
        }
}

static bool anyMedicAlive(const std::vector<Soldier*>& vec) {
    for (const Soldier* s : vec) {
        if (!s->IsAlive()) continue;
        if (dynamic_cast<const Medic*>(s)) return true;
    }
    return false;
}
static bool anyProviderAlive(const std::vector<Soldier*>& vec) {
    for (const Soldier* s : vec) {
        if (!s->IsAlive()) continue;
        if (dynamic_cast<const Provider*>(s)) return true;
    }
    return false;
}

static void pruneDead(std::vector<Soldier*>& vec) {
    for (size_t i = 0; i < vec.size(); ) {
        Soldier* s = vec[i];
        if (!s->IsAlive()) {
            int sx = s->getX(), sy = s->getY();
            if (sy >= 0 && sy < MSZ && sx >= 0 && sx < MSZ) {
                unitOcc[sy][sx] = SOLDIER_EMPTY;
            }
            delete s;
            vec.erase(vec.begin() + i);
        }
        else {
            ++i;
        }
    }
}

static Commander* getCommander(std::vector<Soldier*>& team)
{
    for (Soldier* s : team) {
        if (auto* c = dynamic_cast<Commander*>(s)) return c;
    }
    return nullptr;
}

static bool IsWarriorStillInArmies(Warrior* w)
{
    if (w == nullptr) return false;

    for (Soldier* s : defenders) {
        if (s == w) return s->IsAlive();
    }
    for (Soldier* s : attackers) {
        if (s == w) return s->IsAlive();
    }
    return false;
}

void CleanDeadFromFronts()
{
    auto cleanVec = [](std::vector<Warrior*>& vec) {
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                [](Warrior* w) { return !IsWarriorStillInArmies(w); }),
            vec.end());
        };

    cleanVec(gDefenseFronts.fronts[0].team);
    cleanVec(gDefenseFronts.fronts[1].team);
    cleanVec(gAttackFronts.fronts[0].team);
    cleanVec(gAttackFronts.fronts[1].team);
}

static void UpdateEntranceClearFlags()
{
    for (int f = 0; f < 2; ++f) {
        if (gEntranceClear[f]) continue;
        int defCount = 0, atkCount = 0;
        countFront(defenders, attackers, f, defCount, atkCount);
        if (defCount == 0) {
            gEntranceClear[f] = true;
        }
    }
}

void ManageReinforcements()
{
    auto& defF0 = gDefenseFronts.fronts[0].team;
    auto& defF1 = gDefenseFronts.fronts[1].team;
    auto& atkF0 = gAttackFronts.fronts[0].team;
    auto& atkF1 = gAttackFronts.fronts[1].team;

    if (!defF0.empty() && atkF0.empty() && !atkF1.empty()) {
        for (Warrior* w : defF0) {
            w->SetFrontIndex(1);
            defF1.push_back(w);
        }
        defF0.clear();
    }
    else if (!defF1.empty() && atkF1.empty() && !atkF0.empty()) {
        for (Warrior* w : defF1) {
            w->SetFrontIndex(0);
            defF0.push_back(w);
        }
        defF1.clear();
    }
}

static bool AnyWarriorSeesEnemy(const std::vector<Soldier*>& sideA,
    const std::vector<Soldier*>& sideB)
{
    for (Soldier* s : sideA) {
        Warrior* w = dynamic_cast<Warrior*>(s);
        if (!w || !w->IsAlive()) continue;
        for (const Soldier* e : sideB) {
            if (!e->IsAlive()) continue;
            if (w->CanSeeCell(e->getX(), e->getY())) {
                return true;
            }
        }
    }
    return false;
}

static bool AnyDefenseWarriorAlive()
{
    for (Soldier* s : defenders) {
        Warrior* w = dynamic_cast<Warrior*>(s);
        if (w && w->IsAlive()) return true;
    }
    return false;
}

static void UpdateDefenseOutsideVisibility()
{
    for (int f = 0; f < 2; ++f) {
        bool visible = false;

        for (Soldier* sA : attackers) {
            Warrior* w = dynamic_cast<Warrior*>(sA);
            if (!w || !w->IsAlive()) continue;
            int wf = w->GetFrontIndex();
            if (wf != f) continue;

            for (Soldier* sD : defenders) {
                if (!sD->IsAlive()) continue;
                int dx = sD->getX();
                int dy = sD->getY();
                if (IsInsideFortressCell(dx, dy)) continue;
                if (w->CanSeeCell(dx, dy)) {
                    visible = true;
                    break;
                }
            }
            if (visible) break;
        }

        gDefOutsideVisibleNow[f] = visible;
        if (visible) gDefOutsideSeenEver[f] = true;
    }
}

static void InfiltrateAttackFrontsIfClear(const double safetyAtk[MSZ][MSZ])
{
    if (!AnyDefenseWarriorAlive()) {
        gAttackInfiltrated[0] = gAttackInfiltrated[1] = true;
        return;
    }

    for (int f = 0; f < 2; ++f) {
        if (gAttackInfiltrated[f]) continue;

        if (gAttackFronts.fronts[f].team.empty())
            continue;

        const FrontState& FS = gAttackFrontState[f];
        bool stateAllows = (FS.state == FrontCase::Case2 || FS.state == FrontCase::Case3);
        if (!stateAllows) continue;

        if (!gDefOutsideSeenEver[f]) continue;

        if (gDefOutsideVisibleNow[f]) continue;

        int defCount = 0, atkCount = 0;
        countFront(defenders, attackers, f, defCount, atkCount);
        if (defCount > 0) continue;
        if (atkCount == 0) continue;

        std::vector<std::pair<int, int>> targets;

        int innerYCenter = (f == 0 ? gFortCenterY - 2 : gFortCenterY + 2);
        if (innerYCenter < 0) innerYCenter = 0;
        if (innerYCenter >= MSZ) innerYCenter = MSZ - 1;

        int yAllowance = WALL_H / 2;
        int xSpread = WALL_W / 2 + 3;

        buildTargetsForBand(map, unitOcc, safetyAtk, innerYCenter, yAllowance, xSpread, targets);
        dispatchFrontMoveOrders(Group::ATTACK, f, targets, gAttackFronts);

        gAttackInfiltrated[f] = true;
    }
}

static void ForceMutualAdvanceNoContact(const double safetyDef[MSZ][MSZ],
    const double safetyAtk[MSZ][MSZ])
{
    for (int f = 0; f < 2; ++f) {
        {
            std::vector<std::pair<int, int>> defTargets;
            int defTargetY = (f == 0 ? gEntranceTopY - 8 : gEntranceBotY + 8);
            if (defTargetY < 0) defTargetY = 0;
            if (defTargetY >= MSZ) defTargetY = MSZ - 1;
            buildTargetsForBand(map, unitOcc, safetyDef, defTargetY, 3, 15, defTargets);
            dispatchFrontMoveOrders(Group::DEFENSE, f, defTargets, gDefenseFronts);
        }

        {
            std::vector<std::pair<int, int>> atkTargets;
            int atkTargetY = (f == 0 ? gEntranceTopY - 10 : gEntranceBotY + 10);
            if (atkTargetY < 0) atkTargetY = 0;
            if (atkTargetY >= MSZ) atkTargetY = MSZ - 1;
            buildTargetsForBand(map, unitOcc, safetyAtk, atkTargetY, 3, 15, atkTargets);
            dispatchFrontMoveOrders(Group::ATTACK, f, atkTargets, gAttackFronts);
        }
    }
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT);
    DrawMap();
    DrawUnits();
    for (Bullet* b : gBullets) if (b && b->IsAlive()) b->Show();
    if (pg != nullptr)  pg->Show();
    glutSwapBuffers();
}

void displaySecurityMap()
{
    glClear(GL_COLOR_BUFFER_BIT);
    DrawSecurityMap();
    glutSwapBuffers();
}

void idle()
{
    glutPostRedisplay();

    CleanDeadFromFronts();
    pruneDead(defenders);
    pruneDead(attackers);

    if (!gGameEnded) {
        int defWarriorsCount = CountWarriors(defenders);
        int atkWarriorsCount = CountWarriors(attackers);

        Commander* defCmdTmp = getCommander(defenders);
        Commander* atkCmdTmp = getCommander(attackers);

        bool defCommanderAlive = (defCmdTmp && defCmdTmp->IsAlive());
        bool atkCommanderAlive = (atkCmdTmp && atkCmdTmp->IsAlive());

        if (defWarriorsCount == 0 && atkWarriorsCount == 0 &&
            !defCommanderAlive && !atkCommanderAlive)
        {
            std::cout << "It's a draw!" << std::endl;
            gGameEnded = true;
        }
        else if (atkWarriorsCount == 0 && !atkCommanderAlive)
        {
            std::cout << "Defense team won!" << std::endl;
            gGameEnded = true;
        }
        else if (defWarriorsCount == 0 && !defCommanderAlive)
        {
            std::cout << "Attack team won!" << std::endl;
            gGameEnded = true;
        }
    }

    if (gGameEnded) return;

    gTicksSinceLastKill++;
    if (!gGlobalHuntMode && gTicksSinceLastKill >= NO_KILL_STALE_TICKS) {
        gGlobalHuntMode = true;
    }

    if (defenders.empty() || attackers.empty()) {
        return;
    }

    for (Soldier* s : defenders) s->RecomputeFOVIfNeeded(map);
    for (Soldier* s : attackers) s->RecomputeFOVIfNeeded(map);

    bool anyContact =
        AnyWarriorSeesEnemy(defenders, attackers) ||
        AnyWarriorSeesEnemy(attackers, defenders);

    if (anyContact) gNoContactTicks = 0;
    else           ++gNoContactTicks;

    Commander* defCmd = getCommander(defenders);
    Commander* atkCmd = getCommander(attackers);

    gDefenseCommander = defCmd;
    gAttackCommander = atkCmd;

    bool defCommanderAlive = (defCmd != nullptr);
    bool atkCommanderAlive = (atkCmd != nullptr);

    if (defCmd) defCmd->UpdateIntel(defenders, attackers, map);
    if (atkCmd) atkCmd->UpdateIntel(attackers, defenders, map);

    if (defCmd && atkCmd) {
        defCmd->BuildTeamAwareSafety(map, gSafetyCostDefense, atkCmd->GetIntel());
        atkCmd->BuildTeamAwareSafety(map, gSafetyCostAttack, defCmd->GetIntel());
    }
    else {
        Soldier::BuildSafetyCostMap(map, gSafetyCostDefense);
        Soldier::BuildSafetyCostMap(map, gSafetyCostAttack);
    }

    if (!anyContact &&
        gNoContactTicks >= NO_CONTACT_STUCK_TICKS &&
        defCommanderAlive && atkCommanderAlive) {
        ForceMutualAdvanceNoContact(gSafetyCostDefense, gSafetyCostAttack);
        gNoContactTicks = 0;
    }

    if (atkCommanderAlive) {
        UpdateDefenseOutsideVisibility();
        InfiltrateAttackFrontsIfClear(gSafetyCostAttack);
    }

    for (Soldier* s : defenders) {
        if (auto* c = dynamic_cast<Commander*>(s)) {
            c->Update(map, unitOcc, gSafetyCostDefense);
        }
        else if (auto* m = dynamic_cast<Medic*>(s)) {
            std::vector<Soldier*> friends = defenders;
            m->Update(map, unitOcc, gSafetyCostDefense, friends, attackers);
        }
        else if (auto* p = dynamic_cast<Provider*>(s)) {
            std::vector<Soldier*> friends = defenders;
            p->Update(map, unitOcc, gSafetyCostDefense, friends, attackers);
        }
    }
    for (Soldier* s : attackers) {
        if (auto* c = dynamic_cast<Commander*>(s)) {
            c->Update(map, unitOcc, gSafetyCostAttack);
        }
        else if (auto* m = dynamic_cast<Medic*>(s)) {
            std::vector<Soldier*> friends = attackers;
            m->Update(map, unitOcc, gSafetyCostAttack, friends, defenders);
        }
        else if (auto* p = dynamic_cast<Provider*>(s)) {
            std::vector<Soldier*> friends = attackers;
            p->Update(map, unitOcc, gSafetyCostAttack, friends, defenders);
        }
    }

    ManageReinforcements();

    auto updateFrontSide = [&](Group side,
        FrontState st[2],
        ArmyFronts& fronts,
        const std::vector<Soldier*>& defAll,
        const std::vector<Soldier*>& atkAll,
        const double safety[MSZ][MSZ])
        {
            for (int f = 0; f < 2; ++f) {
                int defC, atkC;
                countFront(defAll, atkAll, f, defC, atkC);

                auto& FS = st[f];
                if (FS.cooldownTicks > 0) --FS.cooldownTicks;

                bool wantCase2 = false, wantCase3 = false, wantBackTo1 = false;

                if (FS.state == FrontCase::Case1) {
                    if (atkC >= 2 * defC) { wantCase2 = true; }
                    else if (defC >= 2 * atkC) { wantCase3 = true; }
                }
                else if (FS.state == FrontCase::Case2) {
                    if (defC >= 2 * atkC) { wantBackTo1 = true; }
                }
                else if (FS.state == FrontCase::Case3) {
                    if (atkC >= 2 * defC) { wantBackTo1 = true; }
                }

                bool requestChange = wantCase2 || wantCase3 || wantBackTo1;

                if (requestChange && FS.cooldownTicks == 0) {
                    FS.sustainTicks++;
                    if (FS.sustainTicks >= SUSTAIN_REQ) {
                        FrontCase newCase = FS.state;
                        if (wantCase2) newCase = FrontCase::Case2;
                        else if (wantCase3) newCase = FrontCase::Case3;
                        else if (wantBackTo1) newCase = FrontCase::Case1;

                        if (newCase != FS.state) {
                            FS.state = newCase;
                            FS.cooldownTicks = CASE_COOLDOWN;
                            FS.sustainTicks = 0;

                            std::vector<std::pair<int, int>> targets;
                            makeFrontTargets(side, f, newCase, safety, targets);
                            dispatchFrontMoveOrders(side, f, targets, fronts);
                        }
                    }
                }
                else {
                    FS.sustainTicks = 0;
                }
            }
        };

    if (defCommanderAlive) {
        updateFrontSide(Group::DEFENSE, gDefenseFrontState, gDefenseFronts, defenders, attackers, gSafetyCostDefense);
    }
    if (atkCommanderAlive) {
        updateFrontSide(Group::ATTACK, gAttackFrontState, gAttackFronts, defenders, attackers, gSafetyCostAttack);
    }

    auto updateSearchSide = [&](Group side,
        ArmyFronts& fronts,
        FrontState* stArr,
        FrontSeekState* seekArr,
        const std::vector<Soldier*>& defAll,
        const std::vector<Soldier*>& atkAll)
        {
            const std::vector<Soldier*>& enemies = (side == Group::DEFENSE ? atkAll : defAll);

            for (int f = 0; f < 2; ++f) {
                FrontSeekState& FS = seekArr[f];

                bool enemySeen = false;
                for (Warrior* w : fronts.fronts[f].team) {
                    if (!w || !w->IsAlive()) continue;
                    for (const Soldier* e : enemies) {
                        if (!e->IsAlive()) continue;
                        if (w->CanSeeCell(e->getX(), e->getY())) {
                            enemySeen = true;
                            break;
                        }
                    }
                    if (enemySeen) break;
                }

                if (enemySeen) {
                    if (FS.inSearch) {
                        FS.inSearch = false;
                        FS.enemySeen = true;
                        for (Warrior* w : fronts.fronts[f].team)
                            if (w) w->CancelSearchPattern();
                    }
                    continue;
                }

                if (FS.inSearch) {
                    bool allDone = true;
                    bool anyAlive = false;
                    for (Warrior* w : fronts.fronts[f].team) {
                        if (!w || !w->IsAlive()) continue;
                        anyAlive = true;
                        if (!w->HasFinishedSearchCycles()) {
                            allDone = false;
                            break;
                        }
                    }

                    if (allDone && anyAlive) {
                        FS.inSearch = false;
                        for (Warrior* w : fronts.fronts[f].team)
                            if (w) w->CancelSearchPattern();

                        if (side == Group::DEFENSE)
                            AdvanceFrontTowardEnemy(side, f, stArr, fronts, gSafetyCostDefense);
                        else
                            AdvanceFrontTowardEnemy(side, f, stArr, fronts, gSafetyCostAttack);
                    }
                }
                else {
                    std::queue<Command>& q =
                        (side == Group::DEFENSE ? gWarriorQueueDefense[f] : gWarriorQueueAttack[f]);
                    if (!q.empty()) continue;

                    bool anyAvail = false;
                    for (Warrior* w : fronts.fronts[f].team) {
                        if (w && w->IsAvailableForSearch()) {
                            anyAvail = true;
                            break;
                        }
                    }
                    if (!anyAvail) continue;

                    FS.inSearch = true;
                    FS.enemySeen = false;
                    for (Warrior* w : fronts.fronts[f].team) {
                        if (w && w->IsAvailableForSearch())
                            w->StartSearchPattern();
                    }
                }
            }
        };

    if (defCommanderAlive) {
        updateSearchSide(Group::DEFENSE, gDefenseFronts, gDefenseFrontState, gDefenseFrontSeek, defenders, attackers);
    }
    if (atkCommanderAlive) {
        updateSearchSide(Group::ATTACK, gAttackFronts, gAttackFrontState, gAttackFrontSeek, defenders, attackers);
    }

    UpdateEntranceClearFlags();

    for (Soldier* s : defenders) {
        if (auto* w = dynamic_cast<Warrior*>(s)) {
            std::vector<Soldier*> friends = defenders;
            std::vector<Soldier*> enemies = attackers;
            bool hasCommanderFlag = defCommanderAlive;
            w->Update(map, unitOcc, gSafetyCostDefense, hasCommanderFlag, friends, enemies);
            w->TryShoot(map, friends, enemies);
            w->TickCooldown();
        }
    }
    for (Soldier* s : attackers) {
        if (auto* w = dynamic_cast<Warrior*>(s)) {
            std::vector<Soldier*> friends = attackers;
            std::vector<Soldier*> enemies = defenders;
            bool hasCommanderFlag = atkCommanderAlive;
            w->Update(map, unitOcc, gSafetyCostAttack, hasCommanderFlag, friends, enemies);
            w->TryShoot(map, friends, enemies);
            w->TickCooldown();
        }
    }

    for (Bullet* b : gBullets) {
        if (!b || !b->IsAlive()) continue;
        std::vector<Soldier*> enemies;
        if (b->GetOwnerTeam() == Team::DEFENSE) enemies = attackers;
        else enemies = defenders;
        b->MoveAndCollide(map, enemies);
    }
    gBullets.erase(std::remove_if(gBullets.begin(), gBullets.end(),
        [](Bullet* b) { if (!b) return true; if (!b->IsAlive()) { delete b; return true; } return false; }),
        gBullets.end());

    if (!anyMedicAlive(defenders)) {
        while (!gMedicQueueDefense.empty()) gMedicQueueDefense.pop();
    }
    if (!anyMedicAlive(attackers)) {
        while (!gMedicQueueAttack.empty()) gMedicQueueAttack.pop();
    }
    if (!anyProviderAlive(defenders)) {
        while (!gSupplyQueueDefense.empty()) gSupplyQueueDefense.pop();
    }
    if (!anyProviderAlive(attackers)) {
        while (!gSupplyQueueAttack.empty()) gSupplyQueueAttack.pop();
    }

    if (pg != nullptr)
        pg->Explode(map);
}

void MouseClick(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)
    {
        pg = new Grenade(MSZ * x / (double)W, MSZ * (H - y - 1) / (double)H);
        pg->SetIsExploding(true);
    }
}

void Menu(int choice)
{
    if (choice == 1)      CreateSecurityMap();
    else if (choice == 2) glutDisplayFunc(displaySecurityMap);
    else if (choice == 3) glutDisplayFunc(display);
}

int main(int argc, char* argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(W, H);
    glutInitWindowPosition(400, 100);
    glutCreateWindow("AI Army Simulation");

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutMouseFunc(MouseClick);

    glutCreateMenu(Menu);
    glutAddMenuEntry("Create Security Map", 1);
    glutAddMenuEntry("Show Security Map", 2);
    glutAddMenuEntry("Show Map", 3);
    glutAttachMenu(GLUT_RIGHT_BUTTON);

    srand(static_cast<unsigned int>(time(nullptr)));
    glClearColor(0.0f, 0.5f, 0.8f, 0.0f);
    glOrtho(0, MSZ, 0, MSZ, -1, 1);

    InitMap();

    Soldier::BuildSafetyCostMap(map, gSafetyCostDefense);
    Soldier::BuildSafetyCostMap(map, gSafetyCostAttack);

    ClearUnitOcc();
    PlaceUnits();

    assignWarriorsToFronts();

    {
        Command c1; c1.type = CommandType::MEDIC_AID; c1.priority = 0;
        c1.x = MSZ / 2; c1.y = MSZ / 2; c1.requesterId = 0; c1.timestamp = 0;
        gMedicQueueDefense.push(c1);

        Command c2; c2.type = CommandType::AMMO_SUPPLY; c2.priority = 0;
        c2.x = MSZ / 2 + 4; c2.y = MSZ / 2 + 4; c2.requesterId = 0; c2.timestamp = 1;
        gSupplyQueueDefense.push(c2);
    }

    glutMainLoop();

    for (Bullet* b : gBullets) delete b;
    gBullets.clear();

    return 0;
}