#include "Soldier.h"
#include "Bullet.h"
#include <cmath>
#include <cstring>
#include <stdlib.h> 

std::queue<Command> gMedicQueueDefense;
std::queue<Command> gMedicQueueAttack;
std::queue<Command> gSupplyQueueDefense;
std::queue<Command> gSupplyQueueAttack;

std::queue<Command> gWarriorQueueDefense[2];
std::queue<Command> gWarriorQueueAttack[2];

Commander* gDefenseCommander = nullptr;
Commander* gAttackCommander = nullptr;

extern bool gEntranceClear[2];
extern bool gAttackInfiltrated[2];

extern bool gGlobalHuntMode;
extern void NotifySoldierDied();

extern std::vector<Bullet*> gBullets;

static inline std::queue<Command>& medicQueueFor(Group g) {
    return (g == Group::DEFENSE) ? gMedicQueueDefense : gMedicQueueAttack;
}
static inline std::queue<Command>& supplyQueueFor(Group g) {
    return (g == Group::DEFENSE) ? gSupplyQueueDefense : gSupplyQueueAttack;
}

static inline std::queue<Command>& warriorQueueFor(Group g, int frontIdx) {
    return (g == Group::DEFENSE) ? gWarriorQueueDefense[frontIdx ? 1 : 0]
        : gWarriorQueueAttack[frontIdx ? 1 : 0];
}

static bool IsInsideFortress(int x, int y) {
    const int WALL_W = 30;
    const int WALL_H = 20;

    int fx = (MSZ - WALL_W) / 2;
    int fy = (MSZ - WALL_H) / 2;

    int minX = fx + 1;
    int maxX = fx + WALL_W - 2;
    int minY = fy + 1;
    int maxY = fy + WALL_H - 2;

    return (x >= minX && x <= maxX && y >= minY && y <= maxY);
}

static int gTempMap[MSZ][MSZ];

void Warrior::ApplyGrenadeExplosion(double gx, double gy, const std::vector<Soldier*>& enemies)
{
    (void)enemies;

    const int NUM_DIR = 20;
    const double TWO_PI = 2.0 * PI;
    const double step = TWO_PI / NUM_DIR;

    Team owner = (getGroup() == Group::DEFENSE ? Team::DEFENSE : Team::ATTACK);

    for (int i = 0; i < NUM_DIR; ++i) {
        double alpha = i * step;

        Bullet* b = new Bullet(gx, gy, alpha, true);
        b->SetOwnerTeam(owner);
        b->SetIsMoving(true);

        gBullets.push_back(b);
    }
}

void Soldier::ApplyDamage(int dmg)
{
    if (dmg <= 0 || !IsAlive()) return;

    int old = health;
    health -= dmg;
    if (health < 0) health = 0;

    if (old > 0 && health == 0) {
        NotifySoldierDied();
    }

    if (health > 0 && health <= INCAP_THRESHOLD && !requestedMedic) {
        Command c;
        c.type = CommandType::MEDIC_AID;
        c.priority = 0;
        c.x = x;
        c.y = y;
        c.requesterId = 0;
        c.timestamp = 0;

        bool sentViaCommander = false;

        Commander* cmdr = (group == Group::DEFENSE ? gDefenseCommander : gAttackCommander);
        if (cmdr && cmdr->IsAlive()) {
            cmdr->EnqueueRequest(c);
            sentViaCommander = true;
        }

        if (!sentViaCommander) {
            medicQueueFor(group).push(c);
        }

        requestedMedic = true;
    }
}

void Soldier::Heal(int amount)
{
    if (amount <= 0 || !IsAlive()) return;
    int old = health;
    health += amount;
    if (health > MAX_HEALTH) health = MAX_HEALTH;

    if (old <= INCAP_THRESHOLD && health > INCAP_THRESHOLD) {
        requestedMedic = false;
    }
}

void Soldier::setPos(int nx, int ny)
{
    x = nx; y = ny;
    InvalidateFOV();
}

void Soldier::Draw() const
{
    if (!IsAlive()) return;

    double r = (group == Group::DEFENSE) ? 0.1 : 0.9;
    double g = (group == Group::DEFENSE) ? 0.2 : 0.1;
    double b = (group == Group::DEFENSE) ? 0.9 : 0.1;

    const double pad = 0.1;
    double x1 = (x - 1) + pad;
    double y1 = (y - 1) + pad;
    double x2 = (x + 2) - pad;
    double y2 = (y + 2) - pad;

    glColor3d(r, g, b);
    glBegin(GL_POLYGON);
    glVertex2d(x1, y1);
    glVertex2d(x1, y2);
    glVertex2d(x2, y2);
    glVertex2d(x2, y1);
    glEnd();

    glColor3d(1.0, 1.0, 1.0);
    glRasterPos2d(x + 0.25 - 1.0, y + 0.2 - 0.8);
    glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, symbol);
}

bool Soldier::IsOpaqueTile(int t)
{
    return (t == ROCK || t == TREE || t == WAREHOUSE);
}

bool Soldier::LineOfSight(int x0, int y0, int x1, int y1, const int map[MSZ][MSZ])
{
    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    int cx = x0, cy = y0;
    while (true) {
        if (!(cx == x0 && cy == y0)) {
            if (cx < 0 || cy < 0 || cx >= MSZ || cy >= MSZ) return false;
            if (IsOpaqueTile(map[cy][cx])) {
                if (!(cx == x1 && cy == y1)) return false;
            }
        }
        if (cx == x1 && cy == y1) return true;

        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
}

bool Soldier::IsCleanShot(double x0, double y0, double x1, double y1, const int map[MSZ][MSZ])
{
    double dx = x1 - x0;
    double dy = y1 - y0;
    double dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 0.001) return true;

    double dirX = dx / dist;
    double dirY = dy / dist;

    double curX = x0 + 0.5;
    double curY = y0 + 0.5;

    double checkDist = dist - 0.5;

    for (double d = 0.5; d < checkDist; d += BULLET_SPEED) {
        curX += dirX * BULLET_SPEED;
        curY += dirY * BULLET_SPEED;

        int cx = (int)curX;
        int cy = (int)curY;

        if (cx < 0 || cx >= MSZ || cy < 0 || cy >= MSZ) return false;

        int cellType = map[cy][cx];
        if (cellType == ROCK || cellType == TREE || cellType == WAREHOUSE) {
            return false;
        }
    }
    return true;
}

void Soldier::RecomputeFOVIfNeeded(const int map[MSZ][MSZ]) const
{
    if (lastFovX == x && lastFovY == y && !fovMask.empty()) return;

    if (fovMask.size() != (size_t)(MSZ * MSZ)) fovMask.assign(MSZ * MSZ, 0);
    else std::fill(fovMask.begin(), fovMask.end(), 0);
    fovCells.clear();

    const int R = VISION_RANGE_CELLS;
    int x0 = x, y0 = y;

    for (int dy = -R; dy <= R; ++dy) {
        int cy = y0 + dy;
        if (cy < 0 || cy >= MSZ) continue;
        for (int dx = -R; dx <= R; ++dx) {
            int cx = x0 + dx;
            if (cx < 0 || cx >= MSZ) continue;
            if (dx * dx + dy * dy > R * R) continue;

            if (LineOfSight(x0, y0, cx, cy, map)) {
                fovMask[cy * MSZ + cx] = 1;
                fovCells.emplace_back(cx, cy);
            }
        }
    }
    lastFovX = x; lastFovY = y;
}

void Soldier::BuildSafetyCostMap(const int map[MSZ][MSZ], double outCost[MSZ][MSZ])
{
    for (int y = 0; y < MSZ; ++y)
        for (int x = 0; x < MSZ; ++x)
            outCost[y][x] = 1.0;

    auto addRing = [&](int cx, int cy, int r, double add) {
        for (int dy = -r; dy <= r; ++dy) {
            int yy = cy + dy; if (yy < 0 || yy >= MSZ) continue;
            for (int dx = -r; dx <= r; ++dx) {
                int xx = cx + dx; if (xx < 0 || xx >= MSZ) continue;
                if (dx * dx + dy * dy > r * r) continue;
                if (map[yy][xx] == 0)
                    outCost[yy][xx] = std::max(0.1, outCost[yy][xx] - add);
            }
        }
        };

    for (int y = 0; y < MSZ; ++y)
        for (int x = 0; x < MSZ; ++x) {
            if (map[y][x] == ROCK)      addRing(x, y, 2, 0.30);
            else if (map[y][x] == TREE) addRing(x, y, 2, 0.20);
        }
}

bool Soldier::FindPathTo(int tx, int ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safetyCost[MSZ][MSZ],
    std::vector<std::pair<int, int>>& outPath) const
{
    outPath.clear();

    if (!IsAlive() || IsIncapacitated())
        return false;

    auto inBounds = [&](int cx, int cy) {
        return cx >= 0 && cx < MSZ && cy >= 0 && cy < MSZ;
        };
    auto walkable = [&](int cx, int cy) {
        return inBounds(cx, cy) && map[cy][cx] == 0 && unitOcc[cy][cx] == SOLDIER_EMPTY;
        };

    if (!(tx == x && ty == y) && !walkable(tx, ty)) return false;

    auto toIdx = [&](int cx, int cy) { return cy * MSZ + cx; };
    const int N = MSZ * MSZ;
    static const int DIRS[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };

    std::vector<int>    cameFrom(N, -1);
    std::vector<double> gScore(N, std::numeric_limits<double>::infinity());
    std::vector<double> fScore(N, std::numeric_limits<double>::infinity());
    std::vector<char>   closed(N, 0);

    auto h = [&](int cx, int cy) -> double {
        return std::abs(cx - tx) + std::abs(cy - ty);
        };

    struct PQNode { int idx; double f; bool operator<(const PQNode& o) const { return f > o.f; } };
    std::priority_queue<PQNode> open;

    const int sx = x, sy = y;
    const int sIdx = toIdx(sx, sy);
    gScore[sIdx] = 0.0;
    fScore[sIdx] = h(sx, sy);
    open.push({ sIdx, fScore[sIdx] });

    while (!open.empty()) {
        int current = open.top().idx; open.pop();
        if (closed[current]) continue;
        closed[current] = 1;

        int cx = current % MSZ, cy = current / MSZ;
        if (cx == tx && cy == ty) {
            std::vector<std::pair<int, int>> rev;
            int cur = current;
            while (cur != -1) {
                int px = cur % MSZ, py = cur / MSZ;
                rev.emplace_back(px, py);
                cur = cameFrom[cur];
            }
            std::reverse(rev.begin(), rev.end());
            outPath.swap(rev);
            return true;
        }

        for (int i = 0; i < 4; ++i) {
            int nx = cx + DIRS[i][0], ny = cy + DIRS[i][1];
            if (!inBounds(nx, ny)) continue;
            if (!(nx == tx && ny == ty) && !walkable(nx, ny)) continue;

            int nIdx = toIdx(nx, ny);
            if (closed[nIdx]) continue;

            double stepCost = 1.0;
            stepCost *= safetyCost[ny][nx];

            double tentative = gScore[current] + stepCost;
            if (tentative < gScore[nIdx]) {
                cameFrom[nIdx] = current;
                gScore[nIdx] = tentative;
                fScore[nIdx] = tentative + h(nx, ny);
                open.push({ nIdx, fScore[nIdx] });
            }
        }
    }
    return false;
}

Commander::Commander(int x, int y, Group g)
    : Soldier(x, y, g, 'C'), state(State::AssessSafety), tickCounter(0)
{
    ResetIntel();
}

void Commander::ResetIntel()
{
    for (int yy = 0; yy < MSZ; ++yy) {
        for (int xx = 0; xx < MSZ; ++xx) {
            intel.knownTerrain[yy][xx] = TERRAIN_UNKNOWN;
            intel.visibleNow[yy][xx] = false;
        }
    }
    intel.seenEnemies.clear();
}

void Commander::EnqueueRequest(const Command& cmd)
{
    mainQueue.push(cmd);
}

bool Commander::isSafeHere(const double safety[MSZ][MSZ]) const
{
    double s = safety[y][x];
    return s <= SAFE_THRESHOLD;
}

bool Commander::findSafeSpot(int& tx, int& ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ]) const
{
    double enemyAvgX = -1, enemyAvgY = -1;
    if (!intel.seenEnemies.empty()) {
        double sumX = 0, sumY = 0;
        for (auto& p : intel.seenEnemies) { sumX += p.first; sumY += p.second; }
        enemyAvgX = sumX / intel.seenEnemies.size();
        enemyAvgY = sumY / intel.seenEnemies.size();
    }

    const int maxRadius = 40;
    double bestScore = std::numeric_limits<double>::infinity();
    int bestX = -1, bestY = -1;
    std::vector<std::pair<int, int>> path;

    for (int r = 1; r <= maxRadius; r += 2) {
        for (int dy = -r; dy <= r; dy += 2) {
            int yy = y + dy; if (yy < 0 || yy >= MSZ) continue;
            for (int dx = -r; dx <= r; dx += 2) {
                int xx = x + dx; if (xx < 0 || xx >= MSZ) continue;
                if (std::abs(dx) != r && std::abs(dy) != r) continue;

                if (group == Group::DEFENSE) {
                    if (!IsInsideFortress(xx, yy)) continue;
                }
                else {
                    if (IsInsideFortress(xx, yy)) continue;
                }

                if (map[yy][xx] != 0 || unitOcc[yy][xx] != SOLDIER_EMPTY) continue;
                double sc = safety[yy][xx];
                if (sc > 0.60) continue;

                double distToEnemy = 0;
                if (enemyAvgX >= 0) {
                    distToEnemy = std::abs(xx - enemyAvgX) + std::abs(yy - enemyAvgY);
                }

                double score = (sc * 5000.0) - distToEnemy;
                double distToMe = std::abs(xx - x) + std::abs(yy - y);
                score += (distToMe * 0.1);

                if (score < bestScore) {
                    if (FindPathTo(xx, yy, map, unitOcc, safety, path)) {

                        if (group == Group::ATTACK) {
                            bool pathInside = false;
                            for (const auto& step : path) {
                                if (IsInsideFortress(step.first, step.second)) {
                                    pathInside = true;
                                    break;
                                }
                            }
                            if (pathInside)
                                continue;
                        }

                        bestScore = score; bestX = xx; bestY = yy;
                    }
                }
            }
        }
    }
    if (bestX != -1) { tx = bestX; ty = bestY; return true; }
    return false;
}

bool Commander::planPathTo(int tx, int ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ])
{
    path.clear(); pathIdx = 0;
    if (!CanAct()) return false;
    if (!FindPathTo(tx, ty, map, unitOcc, safety, path)) return false;
    return true;
}

void Commander::stepMove(int unitOcc[MSZ][MSZ])
{
    if (!CanAct()) return;
    if (path.empty() || pathIdx >= path.size()) return;

    std::pair<int, int> p = path[pathIdx];
    int cx = p.first, cy = p.second;

    if (cx == x && cy == y) {
        ++pathIdx;
        if (pathIdx >= path.size()) return;
        p = path[pathIdx];
        cx = p.first; cy = p.second;
    }

    unitOcc[y][x] = SOLDIER_EMPTY;
    setPos(cx, cy);
    unitOcc[y][x] = 0;
    ++pathIdx;
}

void Commander::processMainQueue()
{
    int batch = 4;
    while (batch-- > 0 && !mainQueue.empty()) {
        Command c = mainQueue.top(); mainQueue.pop();
        switch (c.type) {
        case CommandType::MEDIC_AID:
            medicQueueFor(group).push(c);
            break;
        case CommandType::AMMO_SUPPLY:
            supplyQueue.push(c);
            supplyQueueFor(group).push(c);
            break;
        case CommandType::ATTACK_ORDER:
        case CommandType::DEFEND_ORDER:
        case CommandType::MOVE_ORDER:
            warriorQueue.push(c);
            break;
        }
    }
}

void Commander::tryDispatchBasics()
{
}

void Commander::Update(const int map[MSZ][MSZ],
    int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ])
{
    if (!IsAlive()) return;

    ++tickCounter;

    switch (state) {
    case State::AssessSafety:
        if (tickCounter % SAFETY_CHECK_EVERY == 0) {
            if (!isSafeHere(safety) && CanAct()) {
                state = State::Relocate;
            }
            else {
                state = State::ProcessQueue;
            }
        }
        break;

    case State::Relocate: {
        if (CanAct()) {
            int tx, ty;
            if (findSafeSpot(tx, ty, map, unitOcc, safety) && planPathTo(tx, ty, map, unitOcc, safety)) {
                state = State::Moving;
                break;
            }
        }
        state = State::ProcessQueue;
        break;
    }

    case State::Moving: {
        if (!CanAct()) { state = State::ProcessQueue; break; }
        if (path.empty() || pathIdx >= path.size()) {
            state = State::ProcessQueue;
        }
        else {
            stepMove(unitOcc);
            if (pathIdx >= path.size()) {
                state = State::ProcessQueue;
            }
        }
        break;
    }

    case State::ProcessQueue:
        processMainQueue();
        tryDispatchBasics();
        state = State::AssessSafety;
        break;
    }
}

void Commander::UpdateIntel(const std::vector<Soldier*>& friendlies,
    const std::vector<Soldier*>& hostiles,
    const int map[MSZ][MSZ])
{
    for (int yy = 0; yy < MSZ; ++yy)
        for (int xx = 0; xx < MSZ; ++xx)
            intel.visibleNow[yy][xx] = false;
    intel.seenEnemies.clear();

    for (const Soldier* s : friendlies) {
        if (!s->IsAlive()) continue;
        s->RecomputeFOVIfNeeded(map);
        for (const auto& cell : s->GetFOVCells()) {
            int cx = cell.first, cy = cell.second;
            intel.visibleNow[cy][cx] = true;
            intel.knownTerrain[cy][cx] = static_cast<unsigned char>(map[cy][cx]);
        }
    }

    for (const Soldier* e : hostiles) {
        if (!e->IsAlive()) continue;
        int ex = e->getX(), ey = e->getY();
        bool seen = false;
        for (const Soldier* s : friendlies) {
            if (!s->IsAlive()) continue;
            if (s->CanSeeCell(ex, ey)) { seen = true; break; }
        }
        if (seen) intel.seenEnemies.emplace_back(ex, ey);
    }
}

void Commander::BuildTeamAwareSafety(const int baseMap[MSZ][MSZ],
    double outCost[MSZ][MSZ],
    const TeamIntel& enemyIntel) const
{
    Soldier::BuildSafetyCostMap(baseMap, outCost);

    const double SEEN_BY_ENEMY_MULT = 3.0;
    const double NEAR_ENEMY_ADD = 2.0;
    const int    NEAR_ENEMY_R = 5;

    for (int yy = 0; yy < MSZ; ++yy) {
        for (int xx = 0; xx < MSZ; ++xx) {
            if (enemyIntel.visibleNow[yy][xx]) {
                outCost[yy][xx] *= SEEN_BY_ENEMY_MULT;
            }
        }
    }

    for (const auto& p : enemyIntel.seenEnemies) {
        int ex = p.first, ey = p.second;
        for (int dy = -NEAR_ENEMY_R; dy <= NEAR_ENEMY_R; ++dy) {
            int cy = ey + dy; if (cy < 0 || cy >= MSZ) continue;
            for (int dx = -NEAR_ENEMY_R; dx <= NEAR_ENEMY_R; ++dx) {
                int cx = ex + dx; if (cx < 0 || cx >= MSZ) continue;
                if (dx * dx + dy * dy > NEAR_ENEMY_R * NEAR_ENEMY_R) continue;
                outCost[cy][cx] += NEAR_ENEMY_ADD;
            }
        }
    }
}

Medic::Medic(int x, int y, Group g) : Soldier(x, y, g, 'M') {}

bool Medic::isSafeHere(const double safety[MSZ][MSZ]) const
{
    static constexpr double SAFE_THRESHOLD = 0.60;
    return safety[y][x] <= SAFE_THRESHOLD;
}

bool Medic::findSafeSpot(int& tx, int& ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    const std::vector<Soldier*>& enemies) const
{
    double enemyAvgX = -1, enemyAvgY = -1;
    double sumX = 0, sumY = 0;
    int count = 0;
    for (auto* e : enemies) {
        if (e && e->IsAlive()) {
            sumX += e->getX(); sumY += e->getY(); count++;
        }
    }
    if (count > 0) { enemyAvgX = sumX / count; enemyAvgY = sumY / count; }

    const int maxRadius = 50;
    double bestScore = std::numeric_limits<double>::infinity();
    int bestX = -1, bestY = -1;
    std::vector<std::pair<int, int>> tmp;

    for (int r = 1; r <= maxRadius; r += 2) {
        for (int dy = -r; dy <= r; dy += 2) {
            int yy = y + dy; if (yy < 0 || yy >= MSZ) continue;
            for (int dx = -r; dx <= r; dx += 2) {
                int xx = x + dx; if (xx < 0 || xx >= MSZ) continue;
                if (std::abs(dx) != r && std::abs(dy) != r) continue;

                if (group == Group::DEFENSE) {
                    if (!IsInsideFortress(xx, yy)) continue;
                }

                if (map[yy][xx] != 0 || unitOcc[yy][xx] != SOLDIER_EMPTY) continue;
                double sc = safety[yy][xx];
                if (sc > 0.60) continue;

                double distToEnemy = 0;
                if (count > 0) distToEnemy = std::abs(xx - enemyAvgX) + std::abs(yy - enemyAvgY);

                double score = (sc * 5000.0) - distToEnemy;
                score += (std::abs(dx) + std::abs(dy)) * 0.1;

                if (score < bestScore) {
                    if (FindPathTo(xx, yy, map, unitOcc, safety, tmp)) {
                        bestScore = score; bestX = xx; bestY = yy;
                    }
                }
            }
        }
    }
    if (bestX != -1) { tx = bestX; ty = bestY; return true; }
    return false;
}

bool Medic::findNearestWarehouseAdj(int& tx, int& ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ]) const
{
    int bestX = -1, bestY = -1;
    int bestDist = 1e9;

    static const int NBR[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (int yy = 0; yy < MSZ; ++yy)
        for (int xx = 0; xx < MSZ; ++xx)
            if (map[yy][xx] == WAREHOUSE) {

                bool isDefenseWarehouse = IsInsideFortress(xx, yy);
                if (group == Group::DEFENSE) {
                    if (!isDefenseWarehouse) continue;
                }
                else {
                    if (isDefenseWarehouse) continue;
                }

                for (int i = 0; i < 4; ++i) {
                    int ax = xx + NBR[i][0], ay = yy + NBR[i][1];
                    if (ax < 0 || ay < 0 || ax >= MSZ || ay >= MSZ) continue;
                    if (map[ay][ax] == 0 && unitOcc[ay][ax] == SOLDIER_EMPTY) {
                        int man = std::abs(ax - x) + std::abs(ay - y);
                        if (man < bestDist) { bestDist = man; bestX = ax; bestY = ay; }
                    }
                }
            }
    if (bestX != -1) { tx = bestX; ty = bestY; return true; }
    return false;
}

bool Medic::planPathTo(int tx, int ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ])
{
    path.clear(); pathIdx = 0;
    if (!CanAct()) return false;
    if (!FindPathTo(tx, ty, map, unitOcc, safety, path)) return false;
    return true;
}

void Medic::stepMove(int unitOcc[MSZ][MSZ])
{
    if (!CanAct()) return;
    if (path.empty() || pathIdx >= path.size()) return;

    std::pair<int, int> p = path[pathIdx];
    int cx = p.first;
    int cy = p.second;

    if (cx == x && cy == y) {
        ++pathIdx;
        if (pathIdx >= path.size()) return;
        p = path[pathIdx];
        cx = p.first;
        cy = p.second;
    }

    unitOcc[y][x] = SOLDIER_EMPTY;
    setPos(cx, cy);
    unitOcc[y][x] = 0;
    ++pathIdx;
}

bool Medic::findLowestHealthAllyAndApproachCell(const std::vector<Soldier*>& friendlies,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    int& approachX, int& approachY,
    Soldier*& woundedOut) const
{
    std::vector<Soldier*> candidates;
    for (size_t i = 0; i < friendlies.size(); ++i) {
        Soldier* s = const_cast<Soldier*>(friendlies[i]);
        if (s == this) continue;
        if (!s->IsAlive()) continue;
        if (s->getGroup() != group) continue;
        int h = s->GetHealth();
        if (h > 0 && h <= 25) candidates.push_back(s);
    }
    if (candidates.empty()) return false;

    std::sort(candidates.begin(), candidates.end(),
        [](Soldier* a, Soldier* b) { return a->GetHealth() < b->GetHealth(); });

    const int R = 3;
    for (size_t i = 0; i < candidates.size(); ++i) {
        Soldier* w = candidates[i];
        int wx = w->getX(), wy = w->getY();

        int bestX = -1, bestY = -1; double best = 1e18;
        for (int dy = -R; dy <= R; ++dy) {
            int yy = wy + dy; if (yy < 0 || yy >= MSZ) continue;
            for (int dx = -R; dx <= R; ++dx) {
                int xx = wx + dx; if (xx < 0 || xx >= MSZ) continue;
                if (dx * dx + dy * dy > R * R) continue;
                if (map[yy][xx] != 0 || unitOcc[yy][xx] != SOLDIER_EMPTY) continue;

                std::vector<std::pair<int, int>> tmp;
                if (FindPathTo(xx, yy, map, unitOcc, safety, tmp)) {
                    double score = (double)tmp.size();
                    if (score < best) { best = score; bestX = xx; bestY = yy; }
                }
            }
        }
        if (bestX != -1) {
            approachX = bestX; approachY = bestY; woundedOut = w;
            return true;
        }
    }
    return false;
}

void Medic::healAlly(Soldier* ally)
{
    if (ally == nullptr) return;
    if (!hasBandage) return;

    int need = 100 - ally->GetHealth();
    if (need > 0) ally->Heal(need);
    hasBandage = false;
}

void Medic::Update(const int map[MSZ][MSZ],
    int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    const std::vector<Soldier*>& friendlies,
    const std::vector<Soldier*>& enemies)
{
    if (!IsAlive()) return;

    auto& myMedQ = medicQueueFor(group);

    switch (state)
    {
    case State::IdleSafe:
        if (!myMedQ.empty()) {
            Command c = myMedQ.front(); myMedQ.pop();
            hasOrder = true; orderX = c.x; orderY = c.y;
            state = (!hasBandage ? State::GoGetBandage : State::GoToOrderLocation);
        }
        else if (!isSafeHere(safety) && CanAct()) {
            int tx, ty;
            if (findSafeSpot(tx, ty, map, unitOcc, safety, enemies) && planPathTo(tx, ty, map, unitOcc, safety))
                state = State::ReturnToSafe;
        }
        break;

    case State::GoGetBandage: {
        if (!CanAct()) { state = State::IdleSafe; break; }
        int tx, ty;
        if (findNearestWarehouseAdj(tx, ty, map, unitOcc)) {
            if (planPathTo(tx, ty, map, unitOcc, safety)) {
                stepMove(unitOcc);
                if (pathIdx >= path.size()) {
                    hasBandage = true;
                    state = hasOrder ? State::GoToOrderLocation : State::IdleSafe;
                }
            }
            else state = State::IdleSafe;
        }
        else state = State::IdleSafe;
        break;
    }

    case State::GoToOrderLocation: {
        if (!CanAct() || !hasOrder) { state = State::IdleSafe; break; }
        if (path.empty() || pathIdx >= path.size()) {
            if (!planPathTo(orderX, orderY, map, unitOcc, safety)) { state = State::IdleSafe; break; }
        }
        stepMove(unitOcc);
        if (pathIdx >= path.size()) state = State::SeekAndHeal;
        break;
    }

    case State::SeekAndHeal: {
        if (!CanAct()) { state = State::IdleSafe; break; }
        int ax, ay; Soldier* wounded = nullptr;
        if (findLowestHealthAllyAndApproachCell(friendlies, map, unitOcc, safety, ax, ay, wounded)) {
            if (planPathTo(ax, ay, map, unitOcc, safety)) {
                stepMove(unitOcc);
                if (pathIdx >= path.size()) {
                    healAlly(wounded);
                    if (!myMedQ.empty()) {
                        Command c = myMedQ.front(); myMedQ.pop();
                        hasOrder = true; orderX = c.x; orderY = c.y;
                        state = (!hasBandage ? State::GoGetBandage : State::GoToOrderLocation);
                    }
                    else {
                        hasOrder = false;
                        int tx, ty;
                        if (findSafeSpot(tx, ty, map, unitOcc, safety, enemies) && planPathTo(tx, ty, map, unitOcc, safety))
                            state = State::ReturnToSafe;
                        else state = State::IdleSafe;
                    }
                }
            }
            else state = State::IdleSafe;
        }
        else {
            if (!myMedQ.empty()) {
                Command c = myMedQ.front(); myMedQ.pop();
                hasOrder = true; orderX = c.x; orderY = c.y;
                state = (!hasBandage ? State::GoGetBandage : State::GoToOrderLocation);
            }
            else {
                hasOrder = false;
                int tx, ty;
                if (findSafeSpot(tx, ty, map, unitOcc, safety, enemies) && planPathTo(tx, ty, map, unitOcc, safety))
                    state = State::ReturnToSafe;
                else state = State::IdleSafe;
            }
        }
        break;
    }

    case State::ReturnToSafe:
        if (!CanAct()) { state = State::IdleSafe; break; }
        if (path.empty() || pathIdx >= path.size()) state = State::IdleSafe;
        else {
            stepMove(unitOcc);
            if (pathIdx >= path.size()) state = State::IdleSafe;
        }
        break;
    }
}

Provider::Provider(int x, int y, Group g) : Soldier(x, y, g, 'P') {}

bool Provider::isSafeHere(const double safety[MSZ][MSZ]) const
{
    static constexpr double SAFE_THRESHOLD = 0.60;
    return safety[y][x] <= SAFE_THRESHOLD;
}

bool Provider::findSafeSpot(int& tx, int& ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    const std::vector<Soldier*>& enemies) const
{
    double enemyAvgX = -1, enemyAvgY = -1;
    double sumX = 0, sumY = 0;
    int count = 0;
    for (auto* e : enemies) {
        if (e && e->IsAlive()) {
            sumX += e->getX(); sumY += e->getY(); count++;
        }
    }
    if (count > 0) { enemyAvgX = sumX / count; enemyAvgY = sumY / count; }

    const int maxRadius = 50;
    double bestScore = std::numeric_limits<double>::infinity();
    int bestX = -1, bestY = -1;
    std::vector<std::pair<int, int>> tmp;

    for (int r = 1; r <= maxRadius; r += 2) {
        for (int dy = -r; dy <= r; dy += 2) {
            int yy = y + dy; if (yy < 0 || yy >= MSZ) continue;
            for (int dx = -r; dx <= r; dx += 2) {
                int xx = x + dx; if (xx < 0 || xx >= MSZ) continue;
                if (std::abs(dx) != r && std::abs(dy) != r) continue;

                if (group == Group::DEFENSE) {
                    if (!IsInsideFortress(xx, yy)) continue;
                }

                if (map[yy][xx] != 0 || unitOcc[yy][xx] != SOLDIER_EMPTY) continue;
                double sc = safety[yy][xx];
                if (sc > 0.60) continue;

                double distToEnemy = 0;
                if (count > 0) distToEnemy = std::abs(xx - enemyAvgX) + std::abs(yy - enemyAvgY);

                double score = (sc * 5000.0) - distToEnemy;
                score += (std::abs(dx) + std::abs(dy)) * 0.1;

                if (score < bestScore) {
                    if (FindPathTo(xx, yy, map, unitOcc, safety, tmp)) {
                        bestScore = score; bestX = xx; bestY = yy;
                    }
                }
            }
        }
    }
    if (bestX != -1) { tx = bestX; ty = bestY; return true; }
    return false;
}

bool Provider::findNearestWarehouseAdj(int& tx, int& ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ]) const
{
    int bestX = -1, bestY = -1;
    int bestDist = 1e9;

    static const int NBR[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    for (int yy = 0; yy < MSZ; ++yy)
        for (int xx = 0; xx < MSZ; ++xx)
            if (map[yy][xx] == WAREHOUSE) {

                bool isDefenseWarehouse = IsInsideFortress(xx, yy);
                if (group == Group::DEFENSE) {
                    if (!isDefenseWarehouse) continue;
                }
                else {
                    if (isDefenseWarehouse) continue;
                }

                for (int i = 0; i < 4; ++i) {
                    int ax = xx + NBR[i][0], ay = yy + NBR[i][1];
                    if (ax < 0 || ay < 0 || ax >= MSZ || ay >= MSZ) continue;
                    if (map[ay][ax] == 0 && unitOcc[ay][ax] == SOLDIER_EMPTY) {
                        int man = std::abs(ax - x) + std::abs(ay - y);
                        if (man < bestDist) { bestDist = man; bestX = ax; bestY = ay; }
                    }
                }
            }
    if (bestX != -1) { tx = bestX; ty = bestY; return true; }
    return false;
}

bool Provider::planPathTo(int tx, int ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ])
{
    path.clear(); pathIdx = 0;
    if (!CanAct()) return false;
    if (!FindPathTo(tx, ty, map, unitOcc, safety, path)) return false;
    return true;
}

void Provider::stepMove(int unitOcc[MSZ][MSZ])
{
    if (!CanAct()) return;
    if (path.empty() || pathIdx >= path.size()) return;

    std::pair<int, int> p = path[pathIdx];
    int cx = p.first, cy = p.second;

    if (cx == x && cy == y) {
        ++pathIdx;
        if (pathIdx >= path.size()) return;
        p = path[pathIdx];
        cx = p.first; cy = p.second;
    }

    unitOcc[y][x] = SOLDIER_EMPTY;
    setPos(cx, cy);
    unitOcc[y][x] = 0;
    ++pathIdx;
}

bool Provider::findBestAmmoTargetAndApproachCell(const std::vector<Soldier*>& friendlies,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    int centerX, int centerY,
    int& approachX, int& approachY,
    Soldier*& targetOut) const
{
    const int Rsearch = 6;
    std::vector<Warrior*> cand;

    for (size_t i = 0; i < friendlies.size(); ++i) {
        Soldier* s = const_cast<Soldier*>(friendlies[i]);
        if (!s->IsAlive()) continue;
        if (s->getGroup() != group) continue;
        int dx = s->getX() - centerX;
        int dy = s->getY() - centerY;
        if (dx * dx + dy * dy > Rsearch * Rsearch) continue;

        Warrior* w = dynamic_cast<Warrior*>(s);
        if (w) cand.push_back(w);
    }
    if (cand.empty()) return false;

    std::sort(cand.begin(), cand.end(),
        [centerX, centerY](Warrior* a, Warrior* b) {
            if (a->GetGrenades() != b->GetGrenades())
                return a->GetGrenades() < b->GetGrenades();
            int da = std::abs(a->getX() - centerX) + std::abs(a->getY() - centerY);
            int db = std::abs(b->getX() - centerX) + std::abs(b->getY() - centerY);
            return da < db;
        });

    const int Rapproach = 3;
    for (size_t k = 0; k < cand.size(); ++k) {
        Warrior* t = cand[k];
        int tx = t->getX(), ty = t->getY();

        int bestX = -1, bestY = -1; double best = 1e18;
        for (int dy = -Rapproach; dy <= Rapproach; ++dy) {
            int yy = ty + dy; if (yy < 0 || yy >= MSZ) continue;
            for (int dx = -Rapproach; dx <= Rapproach; ++dx) {
                int xx = tx + dx; if (xx < 0 || xx >= MSZ) continue;
                if (dx * dx + dy * dy > Rapproach * Rapproach) continue;
                if (map[yy][xx] != 0 || unitOcc[yy][xx] != SOLDIER_EMPTY) continue;

                std::vector<std::pair<int, int>> tmp;
                if (FindPathTo(xx, yy, map, unitOcc, safety, tmp)) {
                    double score = (double)tmp.size();
                    if (score < best) { best = score; bestX = xx; bestY = yy; }
                }
            }
        }
        if (bestX != -1) { approachX = bestX; approachY = bestY; targetOut = t; return true; }
    }
    return false;
}

void Provider::giveAmmoTo(Soldier* ally)
{
    if (!hasAmmo || ally == nullptr) return;
    Warrior* w = dynamic_cast<Warrior*>(ally);
    if (!w) return;
    const int SUPPLY_AMOUNT = 2;
    w->OnAmmoSupplied(SUPPLY_AMOUNT);
    hasAmmo = false;
}

void Provider::Update(const int map[MSZ][MSZ],
    int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    const std::vector<Soldier*>& friendlies,
    const std::vector<Soldier*>& enemies)
{
    if (!IsAlive()) return;

    auto& mySupplyQ = supplyQueueFor(group);

    switch (state)
    {
    case State::IdleSafe:
        if (!mySupplyQ.empty()) {
            Command c = mySupplyQ.front(); mySupplyQ.pop();
            hasOrder = true; orderX = c.x; orderY = c.y;
            state = hasAmmo ? State::GoToOrderLocation : State::GoGetAmmo;
        }
        else if (!isSafeHere(safety) && CanAct()) {
            int tx, ty;
            if (findSafeSpot(tx, ty, map, unitOcc, safety, enemies) && planPathTo(tx, ty, map, unitOcc, safety))
                state = State::ReturnToSafe;
        }
        break;

    case State::GoGetAmmo: {
        if (!CanAct()) { state = State::IdleSafe; break; }
        int tx, ty;
        if (findNearestWarehouseAdj(tx, ty, map, unitOcc)) {
            if (planPathTo(tx, ty, map, unitOcc, safety)) {
                stepMove(unitOcc);
                if (pathIdx >= path.size()) {
                    hasAmmo = true;
                    state = hasOrder ? State::GoToOrderLocation : State::IdleSafe;
                }
            }
            else state = State::IdleSafe;
        }
        else state = State::IdleSafe;
        break;
    }

    case State::GoToOrderLocation: {
        if (!CanAct() || !hasOrder) { state = State::IdleSafe; break; }
        if (path.empty() || pathIdx >= path.size()) {
            if (!planPathTo(orderX, orderY, map, unitOcc, safety)) { state = State::IdleSafe; break; }
        }
        stepMove(unitOcc);
        if (pathIdx >= path.size()) state = State::SeekAndSupply;
        break;
    }

    case State::SeekAndSupply: {
        if (!CanAct()) { state = State::IdleSafe; break; }
        int ax, ay; Soldier* target = nullptr;
        if (findBestAmmoTargetAndApproachCell(friendlies, map, unitOcc, safety, orderX, orderY, ax, ay, target)) {
            if (planPathTo(ax, ay, map, unitOcc, safety)) {
                stepMove(unitOcc);
                if (pathIdx >= path.size()) {
                    giveAmmoTo(target);
                }
            }
            else state = State::IdleSafe;
        }
        else {
            state = State::IdleSafe;
        }
        break;
    }

    case State::ReturnToSafe:
        if (!CanAct()) { state = State::IdleSafe; break; }
        if (path.empty() || pathIdx >= path.size()) state = State::IdleSafe;
        else {
            stepMove(unitOcc);
            if (pathIdx >= path.size()) state = State::IdleSafe;
        }
        break;
    }
}

void Warrior::TickCooldown() {
    if (fireCooldown > 0) --fireCooldown;
}

void Warrior::OnAmmoSupplied(int n)
{
    if (n > 0) grenades += n;
    requestedAmmo = false;
}

void Warrior::StartSearchPattern()
{
    if (!CanAct()) return;
    if (state == State::GoToTarget || state == State::FortressPatrol) return;

    searchFinished = false;
    path.clear();
    pathIdx = 0;
    searchPhase = 0;

    if (group == Group::DEFENSE) {
        searchCycle = 1;
        state = State::Searching;
    }
    else {
        int f = frontIndex;
        if (f < 0 || f > 1) f = 0;
        bool atkHasCommander = (gAttackCommander && gAttackCommander->IsAlive());

        if (atkHasCommander && !gAttackInfiltrated[f]) {
            searchFinished = true;
            return;
        }

        searchCycle = 2;
        state = State::Searching;
    }
}

void Warrior::CancelSearchPattern()
{
    if (state == State::Searching)
        state = State::Hold;

    searchFinished = false;
    searchCycle = 0;
    searchPhase = 0;
    path.clear();
    pathIdx = 0;
}

void Warrior::TryShoot(const int map[MSZ][MSZ],
    const std::vector<Soldier*>& allies,
    const std::vector<Soldier*>& enemies)
{
    if (!IsAlive() || IsIncapacitated()) return;
    if (fireCooldown > 0) return;

    if (HasGrenades()) {
        std::vector<Soldier*> visibleEnemies;

        for (Soldier* e : enemies) {
            if (!e || !e->IsAlive()) continue;
            int ex = e->getX();
            int ey = e->getY();

            if (!CanSeeCell(ex, ey)) continue;

            int dx = ex - x;
            int dy = ey - y;
            if (dx * dx + dy * dy > VISION_RANGE_CELLS * VISION_RANGE_CELLS) continue;

            visibleEnemies.push_back(e);
        }

        if (visibleEnemies.size() >= 2) {
            const double CLUSTER_DIST = 5.0;
            const double CLUSTER_R2 = CLUSTER_DIST * CLUSTER_DIST;

            bool clusterFound = false;
            double cx = 0.0, cy = 0.0;

            for (size_t i = 0; i < visibleEnemies.size() && !clusterFound; ++i) {
                for (size_t j = i + 1; j < visibleEnemies.size(); ++j) {
                    double ex1 = visibleEnemies[i]->getX() + 0.5;
                    double ey1 = visibleEnemies[i]->getY() + 0.5;
                    double ex2 = visibleEnemies[j]->getX() + 0.5;
                    double ey2 = visibleEnemies[j]->getY() + 0.5;

                    double dx = ex1 - ex2;
                    double dy = ey1 - ey2;
                    double d2 = dx * dx + dy * dy;

                    if (d2 <= CLUSTER_R2) {
                        cx = (ex1 + ex2) * 0.5;
                        cy = (ey1 + ey2) * 0.5;
                        clusterFound = true;
                        break;
                    }
                }
            }

            if (clusterFound && ConsumeGrenade()) {
                ApplyGrenadeExplosion(cx, cy, enemies);
                fireCooldown = GRENADE_COOLDOWN_TICKS;
                return;
            }
        }
    }

    std::vector<const Soldier*> directTargets;
    std::vector<const Soldier*> obstructedTargets;

    for (const Soldier* e : enemies) {
        if (!e->IsAlive()) continue;
        int ex = e->getX(), ey = e->getY();
        int dx = ex - x, dy = ey - y;
        if (dx * dx + dy * dy > VISION_RANGE_CELLS * VISION_RANGE_CELLS) continue;

        if (IsCleanShot(x, y, ex, ey, map)) {
            directTargets.push_back(e);
        }
        else {
            obstructedTargets.push_back(e);
        }
    }

    auto pickClosest = [&](const std::vector<const Soldier*>& vec)->const Soldier* {
        const Soldier* best = nullptr; int bestD = 1e9;
        for (auto* s : vec) {
            int d = std::abs(s->getX() - x) + std::abs(s->getY() - y);
            if (d < bestD) { bestD = d; best = s; }
        }
        return best;
        };

    const Soldier* target = pickClosest(directTargets);

    if (target) {
        double ang = std::atan2(target->getY() - y, target->getX() - x);
        Bullet* b = new Bullet(x + 0.5, y + 0.5, ang);
        b->SetIsMoving(true);
        b->SetOwnerTeam(getGroup() == Group::DEFENSE ? Team::DEFENSE : Team::ATTACK);
        gBullets.push_back(b);
        fireCooldown = BULLET_COOLDOWN_TICKS;
    }
    else {
        if (state != State::GoToTarget && state != State::FortressPatrol && !obstructedTargets.empty()) {
            const Soldier* obsT = pickClosest(obstructedTargets);
            if (obsT) {
                int vx, vy;
                double dummySafety[MSZ][MSZ];
                Soldier::BuildSafetyCostMap(map, dummySafety);
                int dummyUnitOcc[MSZ][MSZ];
                std::memset(dummyUnitOcc, SOLDIER_EMPTY, sizeof(dummyUnitOcc));

                if (FindVantagePoint(obsT->getX(), obsT->getY(), map, dummyUnitOcc, dummySafety, vx, vy)) {
                    if (planPathTo(vx, vy, map, dummyUnitOcc, dummySafety)) {
                        state = State::GoToTarget;
                    }
                }
            }
        }
    }
}

bool Warrior::FindVantagePoint(int targetX, int targetY,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    int& outX, int& outY)
{
    const int searchRadius = 8;
    double bestDist = 1e9;
    int bx = -1, by = -1;

    for (int dy = -searchRadius; dy <= searchRadius; ++dy) {
        int cy = y + dy;
        if (cy < 0 || cy >= MSZ) continue;
        for (int dx = -searchRadius; dx <= searchRadius; ++dx) {
            int cx = x + dx;
            if (cx < 0 || cx >= MSZ) continue;

            if (map[cy][cx] != 0 || unitOcc[cy][cx] != SOLDIER_EMPTY) continue;

            if (IsCleanShot(cx, cy, targetX, targetY, map)) {
                double d = (double)(std::abs(cx - x) + std::abs(cy - y));
                if (d < bestDist) {
                    bestDist = d;
                    bx = cx;
                    by = cy;
                }
            }
        }
    }

    if (bx != -1) {
        outX = bx; outY = by;
        return true;
    }
    return false;
}

static const int gCornerTargets[4][2] = {
    { 1, 1 },
    { MSZ - 2, 1 },
    { MSZ - 2, MSZ - 2 },
    { 1, MSZ - 2 }
};

static bool BFSShortestPath(int sx, int sy, int tx, int ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    std::vector<std::pair<int, int>>& outPath)
{
    outPath.clear();

    auto inBounds = [](int cx, int cy) {
        return cx >= 0 && cx < MSZ && cy >= 0 && cy < MSZ;
        };

    if (!inBounds(sx, sy) || !inBounds(tx, ty))
        return false;

    struct Node { int x, y; };
    static const int DIRS[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };

    bool visited[MSZ][MSZ] = { false };
    std::pair<int, int> parent[MSZ][MSZ];

    std::queue<Node> q;
    visited[sy][sx] = true;
    parent[sy][sx] = std::make_pair(-1, -1);
    q.push({ sx, sy });

    bool found = false;

    while (!q.empty()) {
        Node cur = q.front(); q.pop();

        if (cur.x == tx && cur.y == ty) {
            found = true;
            break;
        }

        for (int i = 0; i < 4; ++i) {
            int nx = cur.x + DIRS[i][0];
            int ny = cur.y + DIRS[i][1];
            if (!inBounds(nx, ny)) continue;
            if (visited[ny][nx]) continue;

            bool walkable = (map[ny][nx] == EMPTY && unitOcc[ny][nx] == SOLDIER_EMPTY);
            if (!(nx == tx && ny == ty) && !walkable) continue;

            visited[ny][nx] = true;
            parent[ny][nx] = std::make_pair(cur.x, cur.y);
            q.push({ nx, ny });
        }
    }

    if (!found) return false;

    int cx = tx, cy = ty;
    while (cx != -1 && cy != -1) {
        outPath.emplace_back(cx, cy);
        std::pair<int, int> p = parent[cy][cx];
        cx = p.first;
        cy = p.second;
    }

    std::reverse(outPath.begin(), outPath.end());
    return true;
}

bool Warrior::FindSafeRetreatBFS(const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    int& outX, int& outY)
{
    struct Node { int x, y, d; };
    static const int DIRS[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };

    auto inBounds = [](int cx, int cy) {
        return cx >= 0 && cx < MSZ && cy >= 0 && cy < MSZ;
        };

    bool visited[MSZ][MSZ] = { false };

    int sx = x;
    int sy = y;

    std::queue<Node> q;
    visited[sy][sx] = true;
    q.push({ sx, sy, 0 });

    while (!q.empty()) {
        Node cur = q.front(); q.pop();

        if (cur.d > DEFEND_BFS_RADIUS)
            continue;

        if (!(cur.x == sx && cur.y == sy)) {
            if (map[cur.y][cur.x] == EMPTY &&
                unitOcc[cur.y][cur.x] == SOLDIER_EMPTY &&
                safety[cur.y][cur.x] <= DEFEND_SAFE_THRESHOLD)
            {
                outX = cur.x;
                outY = cur.y;
                return true;
            }
        }

        for (int i = 0; i < 4; ++i) {
            int nx = cur.x + DIRS[i][0];
            int ny = cur.y + DIRS[i][1];
            if (!inBounds(nx, ny)) continue;
            if (visited[ny][nx]) continue;

            if (map[ny][nx] != EMPTY) continue;

            visited[ny][nx] = true;
            q.push({ nx, ny, cur.d + 1 });
        }
    }

    return false;
}

bool Warrior::planPathTo(int tx, int ty,
    const int map[MSZ][MSZ],
    const int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ])
{
    path.clear(); pathIdx = 0;
    if (!CanAct()) return false;

    const int(*mapToUse)[MSZ] = map;

    if (getGroup() == Group::ATTACK) {
        int f = frontIndex;
        if (f < 0 || f > 1) f = 0;
        if (!gAttackInfiltrated[f]) {
            for (int yy = 0; yy < MSZ; ++yy)
                for (int xx = 0; xx < MSZ; ++xx)
                    gTempMap[yy][xx] = map[yy][xx];

            for (int yy = 0; yy < MSZ; ++yy) {
                for (int xx = 0; xx < MSZ; ++xx) {
                    if (IsInsideFortress(xx, yy) && gTempMap[yy][xx] == 0) {
                        gTempMap[yy][xx] = ROCK;
                    }
                }
            }
            mapToUse = gTempMap;

            if (IsInsideFortress(tx, ty)) {
                return false;
            }
        }
    }

    if (!FindPathTo(tx, ty, mapToUse, unitOcc, safety, path)) return false;
    return true;
}

void Warrior::stepMove(int unitOcc[MSZ][MSZ])
{
    if (!CanAct()) return;
    if (path.empty() || pathIdx >= path.size()) return;

    std::pair<int, int> p = path[pathIdx];
    int cx = p.first, cy = p.second;

    if (cx == x && cy == y) {
        ++pathIdx;
        if (pathIdx >= path.size()) return;
        p = path[pathIdx];
        cx = p.first; cy = p.second;
    }

    unitOcc[y][x] = SOLDIER_EMPTY;
    setPos(cx, cy);
    unitOcc[y][x] = 0;
    ++pathIdx;
}

void Warrior::StartFortressPatrol()
{
    if (!CanAct()) return;
    if (inFortressPatrol) return;

    if (fortressLoopsCount >= 3) {
        inFortressPatrol = false;
        state = State::Hold;
        return;
    }

    inFortressPatrol = true;
    state = State::FortressPatrol;

    path.clear();
    pathIdx = 0;

    const int WALL_W = 30;
    const int WALL_H = 20;
    int fx = (MSZ - WALL_W) / 2;
    int fy = (MSZ - WALL_H) / 2;
    int cx = fx + WALL_W / 2;
    int cy = fy + WALL_H / 2;

    fortressPatrolPoints[0][0] = cx;
    fortressPatrolPoints[0][1] = fy + 1;
    fortressPatrolPoints[1][0] = fx + WALL_W - 2;
    fortressPatrolPoints[1][1] = cy;
    fortressPatrolPoints[2][0] = cx;
    fortressPatrolPoints[2][1] = fy + WALL_H - 2;
    fortressPatrolPoints[3][0] = fx + 1;
    fortressPatrolPoints[3][1] = cy;

    int bestIdx = 0;
    int bestDist = 1e9;
    for (int i = 0; i < 4; ++i) {
        int px = fortressPatrolPoints[i][0];
        int py = fortressPatrolPoints[i][1];
        int d = std::abs(px - x) + std::abs(py - y);
        if (d < bestDist) {
            bestDist = d;
            bestIdx = i;
        }
    }
    fortressPatrolIndex = bestIdx;

    sawDefenseInCurrentLoop = false;
}

void Warrior::Update(const int map[MSZ][MSZ],
    int unitOcc[MSZ][MSZ],
    const double safety[MSZ][MSZ],
    bool hasCommander,
    const std::vector<Soldier*>& friendlies,
    const std::vector<Soldier*>& enemies)
{
    if (!IsAlive()) return;

    if (!requestedAmmo && NeedsAmmo()) {
        Command c;
        c.type = CommandType::AMMO_SUPPLY;
        c.priority = 0;
        c.x = x;
        c.y = y;
        c.requesterId = 0;
        c.timestamp = 0;

        bool sentViaCommander = false;
        Commander* cmdr = (getGroup() == Group::DEFENSE ? gDefenseCommander : gAttackCommander);
        if (cmdr && cmdr->IsAlive()) {
            cmdr->EnqueueRequest(c);
            sentViaCommander = true;
        }
        if (!sentViaCommander) {
            supplyQueueFor(getGroup()).push(c);
        }
        requestedAmmo = true;
    }

    bool enemyVisible = false;
    int nearbyEnemies = 0;
    int nearbyFriends = 0;

    const int R_NEAR = 6;

    for (const Soldier* e : enemies) {
        if (!e || !e->IsAlive()) continue;
        int ex = e->getX(), ey = e->getY();
        int dx = ex - x, dy = ey - y;
        if (dx * dx + dy * dy <= R_NEAR * R_NEAR) ++nearbyEnemies;
        if (CanSeeCell(ex, ey)) enemyVisible = true;
    }

    for (const Soldier* f : friendlies) {
        if (!f || !f->IsAlive()) continue;
        if (f == this) continue;
        int fx = f->getX(), fy = f->getY();
        int dx = fx - x, dy = fy - y;
        if (dx * dx + dy * dy <= R_NEAR * R_NEAR) ++nearbyFriends;
    }

    if (enemyVisible) noEnemySeenTicks = 0;
    else if (noEnemySeenTicks < 1000000) ++noEnemySeenTicks;

    if (inFortressPatrol && enemyVisible) {
        sawDefenseInCurrentLoop = true;
    }

    if (group == Group::ATTACK && !inFortressPatrol && fortressLoopsCount < 3) {
        int f = frontIndex;
        if (f < 0 || f > 1) f = 0;
        if (gEntranceClear[f] && gAttackInfiltrated[f]) {
            StartFortressPatrol();
        }
    }

    if (hasCommander && (state == State::Hold || state == State::Searching)) {
        auto& q = warriorQueueFor(getGroup(), frontIndex);
        if (!q.empty()) {
            Command c = q.front(); q.pop();

            if (c.type == CommandType::MOVE_ORDER) {
                searchFinished = false;
                if (state == State::Searching)
                    state = State::Hold;

                path.clear();
                pathIdx = 0;

                if (planPathTo(c.x, c.y, map, unitOcc, safety))
                    state = State::GoToTarget;
                else
                    state = State::Hold;
            }
            else if (c.type == CommandType::DEFEND_ORDER) {
                searchFinished = false;
                if (state == State::Searching)
                    state = State::Hold;

                path.clear();
                pathIdx = 0;

                int tx, ty;
                if (FindSafeRetreatBFS(map, unitOcc, safety, tx, ty)) {
                    if (planPathTo(tx, ty, map, unitOcc, safety))
                        state = State::GoToTarget;
                    else
                        state = State::Hold;
                }
                else {
                    state = State::Hold;
                }
            }
        }
    }

    if (!hasCommander && CanAct() && !inFortressPatrol) {
        int cx = x;
        int cy = y;

        bool shouldRetreat = false;

        if (GetHealth() <= LOW_HEALTH_THRESHOLD) {
            shouldRetreat = true;
        }
        else if (enemyVisible &&
            safety[cy][cx] > DANGER_SAFETY_THRESHOLD &&
            nearbyEnemies > nearbyFriends) {
            shouldRetreat = true;
        }

        if (shouldRetreat &&
            state == State::Hold &&
            (path.empty() || pathIdx >= path.size())) {

            int bestX = -1, bestY = -1;
            double bestScore = std::numeric_limits<double>::infinity();
            std::vector<std::pair<int, int>> tmpPath;

            const int maxRadius = 8;
            for (int dy = -maxRadius; dy <= maxRadius; ++dy) {
                int yy = cy + dy; if (yy < 0 || yy >= MSZ) continue;
                for (int dx = -maxRadius; dx <= maxRadius; ++dx) {
                    int xx = cx + dx; if (xx < 0 || xx >= MSZ) continue;
                    if (dx * dx + dy * dy > maxRadius * maxRadius) continue;
                    if (map[yy][xx] != 0 || unitOcc[yy][xx] != SOLDIER_EMPTY) continue;

                    double sc = safety[yy][xx];
                    if (sc >= safety[cy][cx]) continue;

                    if (!FindPathTo(xx, yy, map, unitOcc, safety, tmpPath)) continue;

                    double score = sc * 1000.0 + (double)tmpPath.size();
                    if (score < bestScore) {
                        bestScore = score;
                        bestX = xx; bestY = yy;
                    }
                }
            }

            if (bestX != -1) {
                if (planPathTo(bestX, bestY, map, unitOcc, safety)) {
                    state = State::GoToTarget;
                }
            }
        }

        if (!enemyVisible &&
            state == State::Hold &&
            noEnemySeenTicks >= NO_ENEMY_SEARCH_THRESHOLD &&
            IsAvailableForSearch()) {
            StartSearchPattern();
        }
    }

    if (gGlobalHuntMode) {
        if (!enemyVisible) {
            if (state != State::GoToTarget && state != State::FortressPatrol) {
                state = State::GlobalHunt;
            }
        }
        else {
            if (state == State::GlobalHunt) {
                state = State::Hold;
                path.clear();
                pathIdx = 0;
            }
        }
    }
    else {
        if (state == State::GlobalHunt) {
            state = State::Hold;
            path.clear();
            pathIdx = 0;
        }
    }

    switch (state)
    {
    case State::GoToTarget:
        if (path.empty() || pathIdx >= path.size()) {
            state = State::Hold;
        }
        else {
            stepMove(unitOcc);
            if (pathIdx >= path.size())
                state = State::Hold;
        }
        break;

    case State::Searching:
    {
        if (!CanAct()) {
            state = State::Hold;
            break;
        }

        if (group == Group::DEFENSE && searchCycle == 1) {
            if (enemyVisible) {
                state = State::Hold;
                searchCycle = 0;
                searchFinished = false;
                path.clear();
                pathIdx = 0;
                break;
            }

            if (searchPhase >= 4) {
                searchFinished = true;
                state = State::Hold;
                searchCycle = 0;
                break;
            }

            if (path.empty() || pathIdx >= path.size()) {
                int tx = gCornerTargets[searchPhase][0];
                int ty = gCornerTargets[searchPhase][1];

                if (!BFSShortestPath(x, y, tx, ty, map, unitOcc, path)) {
                    ++searchPhase;
                    if (searchPhase >= 4) {
                        searchFinished = true;
                        state = State::Hold;
                        searchCycle = 0;
                    }
                    break;
                }
                pathIdx = 0;
            }

            stepMove(unitOcc);

            if (pathIdx >= path.size()) {
                ++searchPhase;
                if (searchPhase >= 4) {
                    searchFinished = true;
                    state = State::Hold;
                    searchCycle = 0;
                }
            }
        }
        else if (group == Group::ATTACK && searchCycle == 2) {
            if (enemyVisible) {
                state = State::Hold;
                searchCycle = 0;
                searchFinished = false;
                path.clear();
                pathIdx = 0;
                break;
            }

            bool fortressHasDefense = false;
            for (const Soldier* e : enemies) {
                if (!e || !e->IsAlive()) continue;
                if (IsInsideFortress(e->getX(), e->getY())) {
                    fortressHasDefense = true;
                    break;
                }
            }

            if (fortressHasDefense) {
                state = State::Hold;
                searchCycle = 0;
                searchFinished = true;
                path.clear();
                pathIdx = 0;
                break;
            }

            if (searchPhase >= 4) {
                searchFinished = true;
                state = State::Hold;
                searchCycle = 0;
                break;
            }

            if (path.empty() || pathIdx >= path.size()) {
                static const int attackCornerOrder[4] = { 0, 3, 2, 1 };
                int cornerIdx = attackCornerOrder[searchPhase];

                int tx = gCornerTargets[cornerIdx][0];
                int ty = gCornerTargets[cornerIdx][1];

                if (!BFSShortestPath(x, y, tx, ty, map, unitOcc, path)) {
                    ++searchPhase;
                    if (searchPhase >= 4) {
                        searchFinished = true;
                        state = State::Hold;
                        searchCycle = 0;
                    }
                    break;
                }
                pathIdx = 0;
            }

            stepMove(unitOcc);

            if (pathIdx >= path.size()) {
                ++searchPhase;
                if (searchPhase >= 4) {
                    searchFinished = true;
                    state = State::Hold;
                    searchCycle = 0;
                }
            }
        }
        else {
            state = State::Hold;
        }
        break;
    }

    case State::FortressPatrol:
    {
        const int MAX_LOOPS = 3;

        if (!CanAct()) {
            inFortressPatrol = false;
            state = State::Hold;
            break;
        }

        if (fortressLoopsCount >= MAX_LOOPS) {
            inFortressPatrol = false;
            state = State::Hold;
            break;
        }

        if (path.empty() || pathIdx >= path.size()) {
            if (!path.empty()) {
                fortressPatrolIndex = (fortressPatrolIndex + 1) % 4;

                if (fortressPatrolIndex == 0) {
                    if (sawDefenseInCurrentLoop) {
                        fortressLoopsCount = 0;
                    }
                    else {
                        ++fortressLoopsCount;
                    }
                    sawDefenseInCurrentLoop = false;

                    if (fortressLoopsCount >= MAX_LOOPS) {
                        inFortressPatrol = false;
                        state = State::Hold;
                        break;
                    }
                }
            }

            int tx = fortressPatrolPoints[fortressPatrolIndex][0];
            int ty = fortressPatrolPoints[fortressPatrolIndex][1];

            if (!IsInsideFortress(tx, ty)) {
                inFortressPatrol = false;
                state = State::Hold;
                break;
            }

            if (!FindPathTo(tx, ty, map, unitOcc, safety, path)) {
                bool success = false;
                for (int k = 0; k < 4 && !success; ++k) {
                    fortressPatrolIndex = (fortressPatrolIndex + 1) % 4;
                    tx = fortressPatrolPoints[fortressPatrolIndex][0];
                    ty = fortressPatrolPoints[fortressPatrolIndex][1];
                    if (IsInsideFortress(tx, ty) && FindPathTo(tx, ty, map, unitOcc, safety, path)) {
                        success = true;
                        break;
                    }
                }
                if (!success) {
                    inFortressPatrol = false;
                    state = State::Hold;
                    break;
                }
            }
            pathIdx = 0;
        }

        stepMove(unitOcc);
        break;
    }

    case State::GlobalHunt:
    {
        if (!CanAct()) {
            state = State::Hold;
            break;
        }

        if (enemyVisible) {
            state = State::Hold;
            path.clear();
            pathIdx = 0;
            break;
        }

        if (path.empty() || pathIdx >= path.size()) {
            bool found = false;
            const int MAX_ATTEMPTS = 30;

            for (int attempt = 0; attempt < MAX_ATTEMPTS && !found; ++attempt) {
                int tx = rand() % MSZ;
                int ty = rand() % MSZ;

                if (tx == x && ty == y) continue;
                if (map[ty][tx] != EMPTY) continue;
                if (unitOcc[ty][tx] != SOLDIER_EMPTY) continue;

                if (BFSShortestPath(x, y, tx, ty, map, unitOcc, path)) {
                    pathIdx = 0;
                    found = true;
                }
            }

            if (!found) {
                state = State::Hold;
                path.clear();
                pathIdx = 0;
                break;
            }
        }

        stepMove(unitOcc);
        break;
    }

    case State::Hold:
    default:
        break;
    }
}
