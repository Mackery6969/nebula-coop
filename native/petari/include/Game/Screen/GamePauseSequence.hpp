#pragma once

#include "Game/Screen/LayoutActor.hpp"

class PauseMenu;

namespace MR {
    class FunctorBase;
};  // namespace MR

class GamePauseSequence : public LayoutActor {
public:
    enum MenuType { ActivePause = 0, SceneInformation = 1 };

    GamePauseSequence();

    virtual void init(const TableIter&) override;

    void initWindowMenu(const MR::FunctorBase&);
    void startPause(MenuType);
    void deactivate();
    void exeDeactive();
    void exeActivePauseMenu();
    void exeActivePowerStarList();
    void exeSceneInformation();
    bool isActive() const;
    // The world needs the screen (a miss, a demo): the menu closed at once if it is only choosing (true); a question,
    // save or letter up finishes first (false).
    bool closeForWorld();

    MenuType mMenuType;
    PauseMenu* mPauseMenu;
    MR::FunctorBase* mWindowMenuFunc;
};
