#pragma once
#include "../data/event.h"
#include <cstdint>
#include <vector>

namespace rm95 {

enum Display_ { kDispNormal = 0x19, kDispInvisible = 0x1a, kDispRemoved = 0x1b };
enum Route_ { kRouteNone = 0x1c, kRouteActive = 0, kRouteRepeat = 0x1d, kRouteBack = 0x1e, kRoutePingPong = 0x1f };

// Map character (Chara, 0x6e bytes in GAME.EXE) and event character (EChara, 0xdf bytes).
struct Chara {
    // --- Chara ---
    int32_t x = 0, y = 0;            // +0x04 +0x08
    int32_t destX = 0, destY = 0;    // +0x0c +0x10
    int32_t onScreen = 0;            // +0x14
    uint16_t id = 0;                 // +0x18 event id
    int32_t dirOverride = 2;         // +0x1a one-shot display direction (-1 none)
    int32_t facing = 2;              // +0x1e
    int32_t moveType = 5;            // +0x22
    int32_t motion = -1;             // +0x26
    int32_t graphic = 0;             // +0x2a
    int32_t unk2e = 0;               // +0x2e
    int32_t step = 0;                // +0x32 sub-step 0..4
    int32_t animTimer = 0;           // +0x36
    int32_t animFrame = 0;           // +0x3a
    int32_t layer = 1;               // +0x3e
    int32_t speed = 2;               // +0x42
    int32_t speedCounter = 0;        // +0x46
    int32_t vehicle = 0;             // +0x4a
    int32_t anim = 0x12;             // +0x4e
    int32_t display = kDispNormal;   // +0x52
    int32_t cooldown = 1;            // +0x56
    int32_t routeLen = 0;            // +0x5a
    int32_t routePos = 0;            // +0x5e
    int32_t routeMode = kRouteNone;  // +0x62
    int32_t routeStep = 1;           // +0x66
    std::vector<uint8_t> route;      // +0x6a

    // --- EChara ---
    bool isEvent = false;
    Chara* next = nullptr;           // +0x6e
    int32_t resumeIndex = 0;         // +0x72
    int8_t page = -1;                // +0x76
    EventPageAttrs attrs;            // +0x77
    MoveRoute pageRoutes[4];         // +0xcf

    bool hasRoute() const { return routeMode != kRouteNone; }
    int trigger() const { return page >= 0 ? attrs.trigger(page) : 0; }
    void setRoute(const MoveRoute& r) {
        route = r;
        routeLen = (int)r.size();
        routeMode = kRouteActive;
        routePos = 0;
        routeStep = 1;
    }
    void clearRoute() { route.clear(); routeLen = 0; routeMode = kRouteNone; routePos = 0; routeStep = 1; }
    void setGraphicFromPage(uint16_t g) {
        if (g < 0x100) { graphic = g & 0x3f; facing = g >> 6; dirOverride = g >> 6; }
        else graphic = g;
    }
};

}  // namespace rm95
