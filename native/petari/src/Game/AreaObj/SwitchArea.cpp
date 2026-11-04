#include "Game/AreaObj/SwitchArea.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/NebulaCoopWorld.hpp"

SwitchArea::SwitchArea(int formType, const char* pName) : AreaObj(formType, pName) {
}

void SwitchArea::init(const JMapInfoIter& rIter) {
    AreaObj::init(rIter);
    MR::connectToSceneAreaObj(this);
}

void SwitchArea::movement() {
    if (!isUpdate()) {
        return;
    }

    bool bVar1 = !isValidSwitchB() || isOnSwitchB();

    // Online co-op: an area that turns its switch back as the player leaves follows the local player alone; one that
    // turns it once is everyone's.
    const bool local = mObjArg0 != -1;
    if (local) {
        NebulaCoopWorld::beginLocalSwitches();
    }
    if (bVar1 && isInVolume(*MR::getPlayerPos())) {
        if (mObjArg1 == -1) {
            onSwitchA();
        } else {
            offSwitchA();
        }
    } else if (mObjArg0 != -1) {
        offSwitchA();
    }
    if (local) {
        NebulaCoopWorld::endLocalSwitches();
    }
}

bool SwitchArea::isUpdate() const {
    if (mObjArg2 != -1 && !MR::isOnGroundPlayer()) {
        return false;
    }

    if (mObjArg0 != -1) {
        return true;
    }

    if (mObjArg1 == -1) {
        return !isOnSwitchA();
    }

    return isOnSwitchA();
}

const char* SwitchArea::getManagerName() const {
    return "SwitchArea";
}
