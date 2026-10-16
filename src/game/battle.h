#pragma once

namespace rm95 {

class Game;

// Battle system (FUN_00434332 .. FUN_0042d498), see spec/battle.md
class Battle {
public:
    explicit Battle(Game& g) : g_(g) {}
    static void loadAnimationTables(Game& g);   // FUN_0042a34b
    void run(int troop);                         // FUN_00434332
private:
    Game& g_;
};

}  // namespace rm95
