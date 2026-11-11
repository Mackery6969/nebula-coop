#include "Game/MapObj/ClipFieldSwitch.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaCoopWorld.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1000.0f);

ClipFieldSwitch::ClipFieldSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ClipFieldSwitch >);
}

void ClipFieldSwitch::control() {
    // Online co-op: it follows where the local player is, so its switches are his alone.
    NebulaCoopWorld::beginLocalSwitches();
    controlSwitches();
    NebulaCoopWorld::endLocalSwitches();
}

void ClipFieldSwitch::controlSwitches() {
    mPosition = *MR::getPlayerPos();
    const bool isInArea = MR::isInClipArea(mPosition, cNum_control_f1);

    if (MR::isValidSwitchA(this)) {
        if (isInArea) {
            MR::onSwitchA(this);
        } else {
            MR::offSwitchA(this);
        }
    }

    if (MR::isValidSwitchB(this)) {
        if (isInArea) {
            MR::offSwitchB(this);
        } else {
            MR::onSwitchB(this);
        }
    }
}

void ClipFieldSwitch::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    MR::useStageSwitchWriteA(this, rIter);
    MR::useStageSwitchWriteB(this, rIter);
    initHitSensor(1);
    MR::addHitSensorEye(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateClipping(this);
    appear();
}
