#pragma once

// Nebula's services, asked directly by the game (nebula_game_api.h): the host's options and
// its settings menu and button icons, achievements and play stats, presence, the galaxy select's
// names, scene and light signals for the renderer, art export, test requests.


#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NebulaHost {
    inline u32 option(u32 what) {
        return nebula_host()->option(nebula_host()->user, what);
    }

    inline void gameContext(u32 what, s32 value) {
        nebula_host()->game_context(nebula_host()->user, what, value);
    }

    inline int testSceneFrame(NebulaTestAction* pAction) {
        return nebula_host()->test_scene_frame(nebula_host()->user, pAction);
    }


    // An object as the host names it (markers, lights, names): its address.
    inline u64 objectId(const void* pObject) {
        return static_cast< u64 >(reinterpret_cast< uintptr_t >(pObject));
    }

    inline void gameLight(u32 kind, u64 id, const f32* pValues) {
        nebula_host()->game_light(nebula_host()->user, kind, id, pValues);
    }

    inline void gameStage(const char* pName) {
        nebula_host()->game_stage(nebula_host()->user, pName);
    }

    inline void presence(const u16* pGalaxy, const u16* pStar, s32 powerStars) {
        nebula_host()->presence(nebula_host()->user, pGalaxy, pStar, powerStars);
    }

    inline s32 achievement(const char* pId, u16* pName, u32 capacity) {
        return nebula_host()->achievement(nebula_host()->user, pId, pName, capacity);
    }

    inline s32 achievementDate(const char* pId) {
        return nebula_host()->achievement_date(nebula_host()->user, pId);
    }

    inline void statAdd(const char* pName, u64 delta) {
        nebula_host()->stat_add(nebula_host()->user, pName, delta);
    }

    inline u64 statGet(const char* pName) {
        return nebula_host()->stat_get(nebula_host()->user, pName);
    }

    inline void settingsView(NebulaSettingsView* pView) {
        nebula_host()->settings_view(nebula_host()->user, pView);
    }

    inline void settingsInput(u32 action, s32 arg) {
        nebula_host()->settings_input(nebula_host()->user, action, arg);
    }

    inline void sharp2D(u32 on) {
        nebula_host()->sharp_2d(nebula_host()->user, on);
    }

    inline void buttonIcons(NebulaButtonIcons* pIcons) {
        nebula_host()->button_icons(nebula_host()->user, pIcons);
    }

    inline void textureHd(const void* pImage, const void* pHd) {
        nebula_host()->texture_hd(nebula_host()->user, pImage, pHd);
    }

    inline u32 artExport(const NebulaArtExport* pArt) {
        return nebula_host()->art_export(nebula_host()->user, pArt);
    }

    inline u32 layer2DLines() {
        return nebula_host()->layer_2d_lines(nebula_host()->user);
    }

    inline s32 pointerWheel() {
        return nebula_host()->pointer_wheel(nebula_host()->user);
    }

    inline int galaxySelectStage(u32 index, char* pId, u32 capacity, u32* pStars) {
        return nebula_host()->galaxy_select_stage(nebula_host()->user, index, pId, capacity, pStars);
    }

    inline int galaxySelectNamesWanted() {
        return nebula_host()->galaxy_select_names_wanted(nebula_host()->user);
    }

    inline void galaxySelectName(u32 index, u32 star, const u16* pName) {
        nebula_host()->galaxy_select_name(nebula_host()->user, index, star, pName);
    }
    // Online co-op (docs/COOP_PLAN.md): the local player once per game scene frame, the main menu
    // (NEBULA_COOP_MENU_*), the chat, the room's players and the other players in this instance, the shared world's
    // events, the local player's Power Stars in the room, the room's collision rules, the achievements other
    // players unlocked in the room's save and the shared objects' states.
    inline void coopPlayer(const NebulaCoopPlayer* pPlayer) {
        nebula_host()->coop_player(nebula_host()->user, pPlayer);
    }

    inline s32 coopMenu(u32 what) {
        return nebula_host()->coop_menu(nebula_host()->user, what);
    }

    inline void coopChat(NebulaCoopChatView* pView) {
        nebula_host()->coop_chat(nebula_host()->user, pView);
    }

    inline u32 coopList(NebulaCoopListPlayer* pPlayers, u32 capacity, u32* pRoomSize) {
        return nebula_host()->coop_list(nebula_host()->user, pPlayers, capacity, pRoomSize);
    }

    inline u32 coopRemote(NebulaCoopRemotePlayer* pPlayers, u32 capacity) {
        return nebula_host()->coop_remote(nebula_host()->user, pPlayers, capacity);
    }

    inline void coopWorldReport(const NebulaCoopWorldEvent* pEvent) {
        nebula_host()->coop_world_report(nebula_host()->user, pEvent);
    }

    inline u32 coopWorldTake(NebulaCoopWorldEvent* pEvents, u32 capacity) {
        return nebula_host()->coop_world_take(nebula_host()->user, pEvents, capacity);
    }

    inline s32 coopStarCount() {
        return nebula_host()->coop_star_count(nebula_host()->user);
    }

    inline void coopRules(NebulaCoopRules* pRules) {
        nebula_host()->coop_rules(nebula_host()->user, pRules);
    }

    inline s32 coopAchievementTake(char* pId, u32 capacity) {
        return nebula_host()->coop_achievement_take(nebula_host()->user, pId, capacity);
    }

    // Shared objects (docs/COOP_PLAN.md "Shared objects"): the states of the enemies this game moves, and the ones
    // the other players' games move.
    inline void coopObjectsReport(const NebulaCoopObjectState* pStates, u32 count) {
        nebula_host()->coop_objects_report(nebula_host()->user, pStates, count);
    }

    inline u32 coopObjectsTake(NebulaCoopObjectState* pStates, u32 capacity) {
        return nebula_host()->coop_objects_take(nebula_host()->user, pStates, capacity);
    }
}  // namespace NebulaHost
