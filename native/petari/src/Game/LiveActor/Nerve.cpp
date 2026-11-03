#include "Game/LiveActor/Nerve.hpp"

void Nerve::executeOnEnd(Spine* pSpine) const {
}

namespace {
    // Every nerve by its id: an open table, filled during static initialization (zeroed before any of it runs). The
    // game has some 3,700 nerves.
    const u32 cNerveTableSize = 16384;
    struct NerveSlot {
        u32 mId;
        const Nerve* mNerve;  // nullptr with an id: two nerves share it, so it names neither
    };
    NerveSlot sNerves[cNerveTableSize];
};  // namespace

namespace NebulaNerves {
    bool add(const Nerve* pNerve, u32 id) {
        if (id == 0) {
            return false;
        }
        for (u32 i = 0, slot = id & (cNerveTableSize - 1); i < cNerveTableSize; i++, slot = (slot + 1) & (cNerveTableSize - 1)) {
            NerveSlot& rSlot = sNerves[slot];
            if (rSlot.mId == 0) {
                rSlot.mId = id;
                rSlot.mNerve = pNerve;
                return true;
            }
            if (rSlot.mId == id) {
                if (rSlot.mNerve != pNerve) {
                    rSlot.mNerve = nullptr;
                }
                return true;
            }
        }
        return false;
    }

    const Nerve* find(u32 id) {
        if (id == 0) {
            return nullptr;
        }
        for (u32 i = 0, slot = id & (cNerveTableSize - 1); i < cNerveTableSize; i++, slot = (slot + 1) & (cNerveTableSize - 1)) {
            const NerveSlot& rSlot = sNerves[slot];
            if (rSlot.mId == 0) {
                return nullptr;
            }
            if (rSlot.mId == id) {
                return rSlot.mNerve;
            }
        }
        return nullptr;
    }
};  // namespace NebulaNerves
