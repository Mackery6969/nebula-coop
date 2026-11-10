#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class PlacementId;
class TableIter;

// A zone's switches, as many as its numbers reach: off until set.
class ZoneSwitch {
public:
    ZoneSwitch();

    bool get(int index) const;
    void set(int index, bool isOn);

private:
    u32* mFlags;
    u32 mWords;
};

class SwitchIdInfo {
public:
    SwitchIdInfo(s32 switchID, const TableIter& rIter);

    s32 getSwitchNo() const;

    PlacementId* mIDInfo;
    bool mIsGlobal;
};

class StageSwitchContainer : public NameObj {
public:
    struct ContainerSwitch {
        s32 mZoneId;
        ZoneSwitch* mSwitch;
    };

    StageSwitchContainer();

    void createAndAddZone(const SwitchIdInfo& rInfo);
    ZoneSwitch* getZoneSwitch(const SwitchIdInfo& rInfo);
    ZoneSwitch* findZoneSwitchFromTable(const SwitchIdInfo& rInfo);
    // Online co-op: a zone's switches by its id, none when the scene has no switch there.
    ZoneSwitch* nebulaFindZone(s32 zoneId);
    friend class StageSwitchFunction;

private:
    MR::InlineVector< ContainerSwitch, 20 > mSwitches;
    ZoneSwitch* mGlobalSwitches;
};

class StageSwitchFunction {
public:
    static SwitchIdInfo* createSwitchIdInfo(const char* pSwitchName, const TableIter& rIter, bool);
    static void onSwitchBySwitchIdInfo(const SwitchIdInfo& rInfo);
    static void offSwitchBySwitchIdInfo(const SwitchIdInfo& rInfo);
    static bool isOnSwitchBySwitchIdInfo(const SwitchIdInfo& rInfo);
    // Online co-op (NebulaCoopWorld): a switch's id across games (its zone << 16 | its number, the zone 0xffff for
    // the global ones), and setting one by it.
    static u32 nebulaSwitchId(const SwitchIdInfo& rInfo);
    static void nebulaSetSwitch(u32 id, bool on);
};

class StageSwitchCtrl {
public:
    StageSwitchCtrl(const TableIter& rIter);

    void onSwitchA();
    void offSwitchA();
    bool isOnSwitchA() const;
    bool isValidSwitchA() const;
    void onSwitchB();
    void offSwitchB();
    bool isOnSwitchB() const;
    bool isValidSwitchB() const;
    bool isOnSwitchAppear() const;
    bool isValidSwitchAppear() const;
    void onSwitchDead();
    void offSwitchDead();
    bool isValidSwitchDead() const;
    bool isOnAllSwitchAfterB(int idx) const;
    bool isOnAnyOneSwitchAfterB(int idx) const;

    SwitchIdInfo* mSwitchA;
    SwitchIdInfo* mSwitchB;
    SwitchIdInfo* mSwitchAppear;
    SwitchIdInfo* mSwitchDead;
};

namespace MR {
    StageSwitchCtrl* createStageSwitchCtrl(NameObj* pObj, const TableIter& rIter);
};  // namespace MR
