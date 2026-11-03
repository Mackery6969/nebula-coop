#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct NebulaCoopChatParts;

// Online co-op's chat (NebulaHostServices::coop_chat) in the game's own look: the room's newest lines in the screen's
// bottom-left corner, each on a dimmed strip of the game's window with its sender's name in their character's colour,
// and under them the line the keyboard types. The host keeps the chat and takes the keys (T opens the line); this only
// shows it, and nothing shows outside a room's game. GameScene runs and draws it over its 2D, the achievement pop-up
// included, so play, menus and cutscenes alike show the chat.
class NebulaCoopChat : public LayoutActor {
public:
    NebulaCoopChat();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    // GameScene: one frame, and the drawing at the end of its 2D.
    void update();
    void drawOnTop() const;

private:
    NebulaCoopChatParts* mParts;
};
