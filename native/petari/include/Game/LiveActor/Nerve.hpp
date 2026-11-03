#pragma once

#include "Game/LiveActor/Spine.hpp"

/// @brief Used for executing states of a LiveActor.
class Nerve {
public:
    /// @brief Executes a state based on the host actor.
    /// @param pSpine The spine that contains the host LiveActor to execute the state for.
    virtual void execute(Spine* pSpine) const = 0;
    /// @brief Executes after the last iteration of a state before it executes another state.
    /// @param pSpine The spine that contains the host LiveActor to execute the state for.
    virtual void executeOnEnd(Spine* pSpine) const;
#ifdef TARGET_PC
    /// @brief Online co-op (docs/COOP_PLAN.md "Shared objects"): the nerve's id, its executor's class and its name
    /// hashed, the same in every player's game (0 for none), and its executor's class alone.
    virtual u32 nebulaId() const { return 0; }
    virtual u32 nebulaClass() const { return 0; }
#endif
};

#ifdef TARGET_PC
// Online co-op: every nerve by its id, for another game's object state to name one (NebulaCoopObjects).
namespace NebulaNerves {
    constexpr u32 hash(const char* pText, u32 hash = 2166136261u) {
        return *pText == '\0' ? hash : NebulaNerves::hash(pText + 1, (hash ^ static_cast< u8 >(*pText)) * 16777619u);
    }
    // Static initialization: the nerve is known by its id (two nerves with one id are neither).
    bool add(const Nerve* pNerve, u32 id);
    // The nerve with this id, or none.
    const Nerve* find(u32 id);
};  // namespace NebulaNerves

#define NEBULA_NERVE_ID(name, parent_class)                                                                                                          \
    virtual u32 nebulaId() const {                                                                                                                   \
        return NebulaNerves::hash(#parent_class "::" #name);                                                                                        \
    }                                                                                                                                                \
    virtual u32 nebulaClass() const {                                                                                                                \
        return NebulaNerves::hash(#parent_class);                                                                                                    \
    }
#define NEBULA_NERVE_ADD(name, parent_class)                                                                                                         \
    inline const bool name##NebulaAdded = NebulaNerves::add(&name::sInstance, NebulaNerves::hash(#parent_class "::" #name));
#else
#define NEBULA_NERVE_ID(name, parent_class)
#define NEBULA_NERVE_ADD(name, parent_class)
#endif

#define NEW_NERVE(name, parent_class, executor_name)                                                                                                 \
    class name : public Nerve {                                                                                                                      \
    public:                                                                                                                                          \
        virtual void execute(Spine* pSpine) const {                                                                                                  \
            parent_class* actor = reinterpret_cast< parent_class* >(pSpine->mExecutor);                                                              \
            actor->exe##executor_name();                                                                                                             \
        };                                                                                                                                           \
        NEBULA_NERVE_ID(name, parent_class)                                                                                                          \
        static name sInstance;                                                                                                                       \
    };                                                                                                                                               \
    ATTRIBUTE_WEAK_DEF(name name::sInstance);                                                                                                        \
    NEBULA_NERVE_ADD(name, parent_class)

#define NEW_NERVE_ONEND(name, parent_class, executor_name, executorOnEnd_name)                                                                       \
    class name : public Nerve {                                                                                                                      \
    public:                                                                                                                                          \
        virtual void execute(Spine* pSpine) const {                                                                                                  \
            parent_class* actor = reinterpret_cast< parent_class* >(pSpine->mExecutor);                                                              \
            actor->exe##executor_name();                                                                                                             \
        };                                                                                                                                           \
        virtual void executeOnEnd(Spine* pSpine) const {                                                                                             \
            parent_class* actor = reinterpret_cast< parent_class* >(pSpine->mExecutor);                                                              \
            actor->end##executorOnEnd_name();                                                                                                        \
        };                                                                                                                                           \
        NEBULA_NERVE_ID(name, parent_class)                                                                                                          \
        static name sInstance;                                                                                                                       \
    };                                                                                                                                               \
    ATTRIBUTE_WEAK_DEF(name name::sInstance);                                                                                                        \
    NEBULA_NERVE_ADD(name, parent_class)

/* easy alternative to get a nerve instance (in the standard format) */
#define GET_NERVE(cls, nerve) (&Nrv##cls::nerve::sInstance)

/* easy alternative to get a nerve instance (with an anonymous namespace) */
#define GET_NERVE_ANON(nerve) (&::nerve::sInstance)

/* easy alternative to get a nerve instance (with no namespace) */
#define GET_NERVE_GLOBAL(nerve) (&nerve::sInstance)

/* easy alternative to get a nerve instance (where namespace does not follow standard) */
#define GET_NERVE_DIRECT(nmspc, nerve) (&nmspc::nerve::sInstance)
