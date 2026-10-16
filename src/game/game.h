#pragma once
#include "chara.h"
#include "window.h"
#include "../data/rawrec.h"
#include "../data/rpgdata.h"
#include "../data/strings.h"
#include "../data/event.h"
#include "../gfx/display.h"
#include "../gfx/image.h"
#include "../platform/platform.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace rm95 {

#pragma pack(push, 1)
// Raw block G+0x1080e .. G+0x1194e (0x1140 bytes), saved verbatim in save files.
struct PartyBlock {
    int32_t unk0[3];
    int32_t fightingSize;          // G+0x1081a
    int32_t unk1[8];
    Battler members[8];            // G+0x1083e (party order, then non-members)
    uint32_t itemPtr;              // G+0x10dfe (pointer in the original)
    int8_t equip[8][5];            // G+0x10e02 (by actor id)
    uint8_t learn[8][100];         // G+0x10e2a (by actor id)
    uint8_t unk2[44];
    int32_t money;                 // G+0x11176
    uint8_t tail[2004];
};
#pragma pack(pop)
static_assert(sizeof(PartyBlock) == 0x1140, "PartyBlock layout");

struct VehicleSlot {
    uint16_t id = 0;
    int32_t map = 0;
    Chara* ch = nullptr;
};

struct PostedMessage {
    int msg, wparam, lparam;
};

struct BgmState {
    std::string name;
    int32_t a = 0, b = 0;
};

// Battle animation tables (FUN_0042a34b)
struct AnmFile {              // G+0x2c0a + f*0x18
    int frames = 0;           // +0x00 (set when the file is loaded)
    int rep = 1;              // +0x04
    int posX = -2, posY = -1; // +0x08 / +0x0c
    int start = 0;            // +0x10
    int sound = 0;            // +0x14 index into the system SE table
};
struct BattleAnim {           // G+0x4fa + A*100
    int count = 1;            // +0x00 number of sub animations
    int minLen = 3;           // +0x04
    int delay = 100;          // +0x08 ms per frame
    int file[10] = {};        // +0x0c
    int flashMode = 6;        // +0x5c
    uint8_t r = 0, g = 0, b = 0, gray = 0;  // +0x60..
};
// Party action record (G+0xb87a + i*16)
struct BattleAction { int cmd = 0, target = 0, item = 0, spell = 0; };

class Game {
public:
    Game();
    ~Game();
    bool load(const std::string& gameDir, std::string& error);
    void run();   // whole program: logos, title, main loop

    // ---------------- data ----------------
    std::string gameDir;
    std::string rpgPath;
    RpgData rpg;
    StringTable S;
    ItemData items[100];
    MagicData magics[100];
    Battler monsters[100];             // CHARA.DAT (G+0xb8fa)
    TroopData troops[250];             // EPARTY.DAT (G+0x32e6)
    std::vector<uint8_t> monstpic;     // MONSTPIC.DAT
    std::vector<size_t> monstpicOffsets;
    AudioEntry audioSlots[12];         // DAT_0046eb0c (changeable by events)
    int32_t transitions[3] = {5, 12, 14};
    int msgSpeed = 30;                 // DAT_0046a268
    bool transparentWindows = false;   // DAT_0046a24c
    bool savingEnabled = false;        // DAT_0046a260

    // ---------------- party ----------------
    alignas(8) PartyBlock pb{};
    int16_t itemCounts[100] = {};      // *(G+0x10dfe)
    Battler enemies[10];               // G+0x100de
    int enemyCount = 0;                // G+0x100da
    Chara partyChara[8];               // G+0x3036, by actor id
    Chara* mapParty[8] = {};           // G+0xb77e, by party slot
    int8_t partyActor[8] = {};         // G+0xb776
    int partySize = 0;                 // G+0xb772
    int mapCharaCount = 0;             // G+0xb76e
    uint8_t partyMask = 0;             // DAT_0046dc28
    int trail[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // G+0x32c2
    Battler& member(int k) { return pb.members[k]; }
    int32_t& money() { return pb.money; }
    int partySlotOfActor(int actor);   // FUN_0043585d (8 if not found)
    Chara* hero() { return mapParty[0]; }
    Chara* heroChara = nullptr;        // DAT_00461ae4

    // ---------------- map ----------------
    int curMap = -1;                   // DAT_0046a5e4
    MapData* map = nullptr;
    int mapW = 0, mapH = 0;            // DAT_0046a29c / 2a0
    int camX = 0, camY = 0;            // DAT_0046a2a4 / 2b0
    int camPxX = 0, camPxY = 0;        // DAT_0046a2ac / 2b8
    bool scrollFlag[4] = {};           // DAT_0046a5e8
    uint8_t atr[1024] = {};            // DAT_0046a604
    std::vector<uint16_t> areaIds;     // G+0x32e2
    std::vector<std::unique_ptr<Chara>> events;  // DAT_00461ae8 list (row-major order)
    std::vector<std::unique_ptr<Chara>> vehicleStore;
    VehicleSlot vehicles[10];          // G+0xb496
    Chara* grid[3][17][22] = {};       // DAT_0046ca04
    Surface chipsets[4];               // DAT_0046ebd0
    Surface charaSurf;                 // DAT_0046ebe0
    bool riding = false;               // G+0x1de

    uint32_t tileCell(int x, int y) const { return map->cells[(size_t)y * mapW + x]; }
    uint16_t tileAt(int x, int y) const { return (uint16_t)(tileCell(x, y) & 0xffff); }
    bool screenCell(int mx, int my, int& c, int& r) const;
    Chara*& gridAt(int layer, int c, int r) { return grid[layer][r + 1][c + 1]; }
    bool gridValid(int c, int r) const { return c >= -1 && c <= 20 && r >= -1 && r <= 15; }

    // ---------------- switches & flags ----------------
    uint8_t switches[128] = {};        // DAT_0046dba8
    bool getSwitch(int n) const { return n >= 0 && n < 1024 && (switches[n >> 3] >> (n & 7)) & 1; }
    void setSwitch(int n, bool v) { if (n < 0 || n >= 1024) return; if (v) switches[n >> 3] |= 1 << (n & 7); else switches[n >> 3] &= ~(1 << (n & 7)); }
    int16_t teleportList[100] = {};    // G+0x326
    int16_t teleportDisabled = 0;      // G+0x3ee
    int16_t escapeMap = 0;             // G+0x3f0
    int moveControl = 0;               // DAT_0046dc2c
    bool pageRefresh = false;          // DAT_0046dba4

    // ---------------- game object flags ----------------
    bool quit = false;                 // +0x86
    bool abortTick = false;            // +0x8a
    bool busy96 = false;               // +0x96
    bool midiOnce = false;             // +0x9a
    int runningEvent = 0;              // +0xa2
    int encounterArea = 0;             // +0xa6
    int encounterSteps = 0;            // +0xaa
    bool skipTick = false;             // +0xae
    int msgWait = -1;                  // +0xb2
    bool suspend = false;              // +0xb6
    int screenState = -1;              // +0x2ffa
    int lastKey = -1;                  // +0x2ffe
    int waitingId = -1, waitingPage = 0, waitingIndex = 0;  // +0x3002..
    Chara* waitingChara = nullptr;     // +0x300e
    uint32_t lastTickTime = 0;         // +0x301a
    int scrollStep = 0;                // +0x302e
    int escapeForbidden = 0;           // +0x322
    bool inBattle = false;             // +0xb79e
    bool autoBattle = false;           // +0xb7a2
    bool okPressed = false;            // +0xb86a
    bool menuRequested = false;        // +0xb872
    bool eventRunning = false;         // +0x11c72
    int selectedIndex = -1;            // +0x11c52
    int postAction = -1;               // +0x12552
    bool battleLost = false;           // +0x125b2
    int battleExit = 0;                // +0x125b6
    bool keyReleased = true;           // +0x12572
    int focusedType = 0;               // DAT_0046e338
    int menuY = 250;                   // DAT_0046a288
    bool haveSavedPalette = false;     // G+0x9e
    Palette savedPalette{};
    int mouseSelX = -1, mouseSelY = -1;  // +0x11c56 / 5a

    // ---------------- windows ----------------
    std::unique_ptr<RpgWindow> windows[13];  // G+0xb7b2 + 4*type
    std::deque<PostedMessage> posted;
    void post(int msg, int w, int l) { posted.push_back({msg, w, l}); }
    RpgWindow* win(int type) { return (type >= 0 && type < 13) ? windows[type].get() : nullptr; }
    void closeWindow(int type, bool redraw);         // FUN_004276aa
    void closeAllWindows(bool closeMenu);            // FUN_004340c4
    int lineH() const;                               // G+0xb83e
    int charW() const;                               // G+0xb842
    RpgWindow* createWindow(int type, int x, int y, int w, int h, const WindowLayout* layout, int itemCount);
    // FUN_0042dd9b
    RpgWindow* menuWindow(int x, int y, int w, int rows, const std::string& text, int maxLen, unsigned fmt, int l0, int type,
                          unsigned disableMask, bool colourByState);
    // FUN_0042e016 (party list with HP/MP)
    RpgWindow* partyWindow(int x, int y, int w, int rows, const std::string& text, int maxLen, unsigned fmt, int l0, int type,
                           unsigned disableMask, bool mp);
    void popup(int ownerType, const std::string& text);  // FUN_00425120
    void sysMessage(const std::string& text, double factor, bool bottom);  // FUN_0042b2f1

    // ---------------- message loop ----------------
    void pumpMessages();                    // FUN_00440e73
    void onKeyDown(int vk);                 // FUN_0042ecd0
    void onKeyUp(int vk);                   // FUN_00434744
    void onMouseDown(int x, int y);         // FUN_0042f1bf
    void onMouseMove(int x, int y);         // FUN_0042f298
    void onRightDown(bool leftHeld);        // FUN_0042f468
    void onMouseDblClick();                 // FUN_004346f5
    bool mouseDrag = false;                 // DAT_0046dba0
    int mouseX0 = 0, mouseY0 = 0;           // DAT_0046db8c / 90
    int mouseDir = 5;                       // DAT_0046db94
    int mouseCount = 0;                     // DAT_0046db9c
    void handlePosted(const PostedMessage& m);
    void onSelect(int type, int lparam);    // FUN_00433d2e
    void onCancel(int type);                // FUN_00434102
    void waitTenths(int n, bool movieWait = false);  // FUN_00428135
    void flushKeys(bool wait);              // FUN_0042368f
    bool keyHeldAsync(int vk);              // FUN_0041f719
    void delayMs(int ms);                   // FUN_0041c70e

    // ---------------- map / movement ----------------
    void loadMapGraphics();                 // FUN_0042eaff(-1)
    void loadAtr();                         // LoadATR
    void initPartyRecords();                // FUN_004225b6
    void initPartyCharas();                 // FUN_004217f7
    bool teleport(int mapId, int x, int y); // FUN_0041f179
    void showMap();                         // FUN_0041f4cd
    void createMapEvents();                 // FUN_00421ac8
    void deleteMapEvents();                 // FUN_00421892
    void buildAreaIds();                    // FUN_0041f57e
    void refreshPages();                    // FUN_0041f00d
    bool selectPage(Chara* ev, const int16_t* counts, uint32_t money);  // FUN_0041e083
    void applyPage(Chara* ev);              // FUN_0041e206
    void computeScrollFlags();              // FUN_004224db
    void clearGrid();                       // FUN_0042e7f4
    void redrawTiles();                     // FUN_00421cb1
    void drawScrollStrip(int dir, int s);   // FUN_00421d52
    void redraw(int dir, bool present);     // FUN_00422128
    void drawCharas(bool freeze);           // FUN_0042e642
    void drawChara(Chara* c, bool freeze);  // FUN_0041c973
    void drawTile(int sx, int sy, uint16_t t);  // FUN_0041c64d
    void startMotion(Chara* c, int m);      // FUN_0041d55c
    Chara* advanceMotion(Chara* c);         // FUN_0041d865
    int nextMove(Chara* c, int tx, int ty); // FUN_0041d239
    void applyRouteDisplay(Chara* c);       // FUN_0041d1f2
    void updateEvent(Chara* ev);            // FUN_00420914
    bool passable(int vehicle, int attr) const;
    int occupantTile(int mx, int my);       // FUN_0041c58d
    void actionButton(int mode);            // FUN_00421279
    void touch(Chara* e);                   // FUN_0042166d
    void faceEachOther(Chara* e, bool checkLayer);
    void pushTrail();                       // FUN_00420f03
    void startFollowers();                  // FUN_00420f2d
    void poisonStep();                      // FUN_00420805
    void encounterStep(int cell);           // FUN_00420d28
    bool partyDown();                       // FUN_0043575a
    void playVehicleBgm(int slot, bool asSe);  // FUN_004209f7
    void boardVehicle(Chara* ev);           // FUN_00421957 + cmd 47
    bool leaveVehicle();                    // FUN_00420abe
    void finishBoarding();
    void finishAlighting();
    void restoreVehicleGraphic(Chara* v);   // FUN_0041db93
    void removeVehicles();                  // FUN_00435b9b
    bool vehicleHasId(uint16_t id);         // FUN_00421910
    void mainLoop();                        // FUN_0041f744 map part
    void mouseWalk();                       // FUN_0041f6dd

    // ---------------- events ----------------
    EventScript script;
    bool scriptLoaded = false;
    int runEventId = 0;                     // G+0x12536
    void runEvent(int id, Chara* ev);       // FUN_0042fb0e
    int execCommand(EventCommand* c, Chara*& ev, std::vector<EventCommand>& list, size_t index);  // FUN_00432393
    std::string expandText(const std::string& in, bool& ok);  // FUN_0043051b
    void cmdMessage(const EventCommand& c, const EventCommand* next);  // FUN_004308c7
    int cmdChoice(const EventCommand& c);    // FUN_00430aba
    void changeValue(int32_t* v, int a1, int a2, int rel, int lo, int hi);   // FUN_0043217c
    void changeValue16(int16_t* v, int a1, int a2, int rel, int lo, int hi); // FUN_00432205
    int rnd(int a, int b);
    void changeLevel(const EventCommand& c);       // FUN_0042d216
    void changeParty(uint8_t mask, int mode);      // FUN_00425b21
    void showPicture(const EventCommand& c);       // FUN_00430ec0
    void erasePicture(int transition);             // FUN_00431455
    void inn(const EventCommand& c);               // FUN_00431583
    void shop(const EventCommand& c);              // FUN_004318f5
    void screenTone(const EventCommand& c);        // FUN_004320cb
    void fadeToBlack(int mode);                    // FUN_00432042
    void teleportListChange(int map, int add);     // FUN_004322aa
    void playMovie(const EventCommand& c);         // FUN_00435377
    void deriveStat(int which, int k);             // FUN_0042d11a
    void changeCondition(int k, int op);           // FUN_0042c03c
    void reloadPalette(bool fromFile);             // FUN_004352a6

    // ---------------- audio ----------------
    BgmState currentBgm{"演奏停止", 0, 0};
    BgmState savedBgm;
    std::string resolveMedia(const std::string& name);   // FUN_00434890
    void playBgm(const std::string& name, const AudioEntry* params, bool force);  // FUN_0042f898
    void playSe(const std::string& name, const AudioEntry* params);              // FUN_00430c07
    void playSystemSe(int index);                         // FUN_0042a720
    void stopMusic();                                     // FUN_0042f860
    void saveBgm();                                       // FUN_004358b7
    void restoreBgm(bool force);                          // FUN_00435923
    void waitJingle(bool midi);                           // FUN_00427ff2
    void applyAudioParams(int channel, int a, int b);     // FUN_0042f6fc / 0042f780
    void pollAudio();                                     // MM_MCINOTIFY
    int seChannelBusy[4] = {};

    // ---------------- menus ----------------
    int menuMode = 0;           // G+0x1253e
    int menuActor = 0;          // G+0xb866 (member whose command is entered)
    BattleAction actions[8];    // G+0xb87a
    int listPage = 0;           // G+0x3266
    int listIndex[100] = {};    // G+0x326a
    int listCount = 0;          // G+0x11c7e
    int listTotal[13] = {};     // DAT_0046ec0c.. (count remembered by each list filler)
    int menuMember = 0;         // G+0x326e
    int equipMember = 0;        // G+0x4f6
    int equipSlot = 0;          // G+0x4f2
    int enemyList[10] = {};     // G+0xb6be (living enemies sorted by x)
    int enemyListCount = 0;     // G+0xb6f6
    int partyList[4] = {};      // G+0xb6e6
    int partyListCount = 0;     // G+0xb6fa
    int lastMouseX = -1, lastMouseY = -1;
    void runMenu();             // FUN_0042dba9
    void openSubmenu(int idx);  // FUN_0042739a
    std::string buildText(int mode, bool all);   // FUN_00423709
    int buildItemList(int mode, int member);      // FUN_00426b6e
    int buildMagicList(int mode);                 // FUN_00426cb4
    int buildShopList(const std::string& s);      // FUN_00426b38
    RpgWindow* listWindow(int type, int x, int y, int count);
    void fillMagicList(int page, int count);      // FUN_00426dad
    void fillItemList(int page, int count);       // FUN_0042723d
    void fillTeleportList(int page, int count);   // FUN_0042713e
    void fillShopList(int page, int count, bool sell);  // FUN_00426fb9
    void onPageChange(int type, int page);        // FUN_0042f63c
    int firstActiveMember();                      // FUN_00435797
    bool menuLocked();                            // FUN_00435881
    void itemMenu();                      // FUN_0043300e
    void itemSelected(int idx);           // FUN_004331d7
    void magicMenu();                     // FUN_0043387a
    void magicSelected(int idx);          // FUN_00433a62
    void teleportMenu(int owner);         // FUN_00433682
    void equipSlots();                    // FUN_0042669d
    void equipCandidates();               // FUN_00426710
    void equipItem();                     // FUN_00426913
    void statusWindow(int k);             // FUN_004252a3
    void orderMenu();                     // FUN_0042600b
    void systemMenu();                    // FUN_00427595
    void useItem(int item, int user, int target);           // FUN_004283b1
    void itemEffect(int type, int target, int power);       // FUN_004285e3
    void magicEffect(int spell, int caster, int target, bool isItem);   // FUN_00428a9c
    void cureStatus(Battler& b);          // FUN_00429b7b
    bool escapeTo(int area);              // FUN_0043475f
    void gatherParty();                   // FUN_004356f4
    void versionDialog();                 // FUN_00434975

    // ---------------- battle ----------------
    BattleAnim anims[100];                // G+0x4fa
    AnmFile anmFiles[42];                 // G+0x2c0a
    void loadAnimationTables();           // FUN_0042a34b
    TroopData* troop = nullptr;           // G+0x1194e
    int expTotal = 0, goldTotal = 0, dropItem = -1;  // G+0x11c5e..
    int16_t savedStats[8][3] = {};        // G+0x12576
    Surface enemyPic[10];                 // DAT_0046ebe4
    bool enemyPicValid[10] = {};
    Rect battleRect;                      // G+0x12556
    bool commandsDone = false;            // G+0xb86e
    Battler& combatant(int i) { return i < 100 ? pb.members[i] : enemies[i - 100]; }
    void startBattle(int troop);          // FUN_00434332
    bool setupBattle(int troop);          // FUN_00422b13
    bool loadBattleGraphics();            // FUN_00423188 + FUN_0042298f
    void freeBattle();                    // FUN_00423604 + FUN_0042292e
    Rect enemyRect(int i) const;
    void drawEnemies(int skip, int lowered, bool flip);  // background + living enemies
    bool allEnemiesDead();                // FUN_00423ee2 == 2
    bool partyDefeated();                 // FUN_00423f48 == 2
    void battleStatus();                  // FUN_0042c656
    void battleCursor();                  // FUN_0042788f
    bool nextActor(bool back);            // FUN_004357c9
    void battleNext();                    // FUN_00427747
    void battleCommand(int sel);          // FUN_004240ce
    void autoBattleRun();                 // FUN_00423fac
    int pickTarget(int side, int mode);   // FUN_00423c17
    void executeTurn();                   // FUN_0042cbbb
    int turnOrder(int* ids);              // FUN_004282b5
    void partyAct(int i);                 // FUN_0042b8bf
    void enemyAct(int e);                 // FUN_0042c114
    void endOfTurn();                     // FUN_0042c96a
    int escapeChance();                   // FUN_0042b4e6
    int randomKnownSpell(int i);          // FUN_0042b455
    int autoHeal(int i, int& target);     // FUN_0042b616
    int maxLuck();                        // FUN_00427d90
    void physicalAttack(int a, int t, int mode);  // FUN_00429ccc
    void applyDamage(int t, int dmg, int attacker);  // FUN_00427976
    void playAnimation(int a, int target, int caster);  // FUN_0042a760
    void jumpEnemy(int e, int phase);     // FUN_0042a0f2
    void removeEnemy(int e, int transition);  // FUN_00427dd3
    void victory();
    void gainExp(int who, bool add);      // FUN_0042d498
    int statusIndex(int status) const;    // FUN_0042c0b5

    // ---------------- save ----------------
    bool saveGame(const std::string& path);
    bool loadGame(const std::string& path);
    void saveDialog();                    // FUN_004247e5
    bool loadDialog();                    // FUN_00424957
    std::string chooseSaveFile(bool save);
    std::string startSave;                // --load
    std::string pendingSave;              // --save-at (testing)
    int testBattle = 0;                   // --test-battle (testing)
    int testEvent = -1;                   // --test-event (testing)

    // misc
    int titleSequence();                  // returns 0 new game, 1 loaded
    void logo(int which);                 // FUN_00434a5c
    void toggleScreenMode();              // FUN_00420fb3
    std::string sjisFmt(const char* fmt, ...);
    static std::string sjisFmtS(const char* fmt, ...);
};


Game& game();

}  // namespace rm95
