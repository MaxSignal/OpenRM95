// Temporary implementations (to be replaced)
#include "game.h"
#include "battle.h"
#include "../util/log.h"

namespace rm95 {

void Battle::loadAnimationTables(Game&) {}
void Battle::run(int) {}

void Game::startBattle(int troop) { LOG_INFO("battle %d (not implemented)", troop); }
void Game::runMenu() {
    menuRequested = false;
}
void Game::onSelect(int type, int lparam) {
    if (lparam >= 1000) { lparam -= 1000; mouseSelX = mouseSelY = -1; }
    if (type == 0 && win(6)) return;
    if (type == 10) return;
    selectedIndex = lparam;
    okPressed = true;
    flushKeys(true);
    okPressed = true;
}
void Game::onCancel(int type) {
    if (type == 0 && win(6)) return;
    if (type == 1) return;
    okPressed = true;
}
void Game::onPageChange(int, int) {}
void Game::inn(const EventCommand&) {}
void Game::shop(const EventCommand&) {}
void Game::saveDialog() {}
bool Game::loadDialog() { return false; }
void Game::toggleScreenMode() {}

}  // namespace rm95
