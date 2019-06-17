/**
* Contains interface and calls that have to be implemented by the anticheat lib.
*/

#ifndef ANTICHEAT_H
#define ANTICHEAT_H

#include "Common.h"
#include "Unit.h"

enum CheatAction
{
    CHEAT_ACTION_NONE           = 0x00,
    CHEAT_ACTION_LOG            = 0x01,
    CHEAT_ACTION_REPORT_GMS     = 0x02,
    CHEAT_ACTION_KICK           = 0x04,
    CHEAT_ACTION_BAN_ACCOUNT    = 0x08,
    CHEAT_ACTION_BAN_IP_ACCOUNT = 0x10,
    CHEAT_ACTION_TELEPORT_BACK  = 0x20,
    CHEAT_ACTION_MUTE_PUB_CHANS = 0x40, // Mutes the account from public channels
    CHEAT_MAX_ACTIONS,
};

class WorldSession;
class ChatHandler;
class WardenInterface;
class MovementAnticheatInterface;
class AccountPersistentData;
struct AreaEntry;

class WardenInterface
{
    public:
        WardenInterface() {}
        virtual ~WardenInterface() {}
        virtual void HandleWardenDataOpcode(WorldPacket & recv_data) {}
        virtual void Update() {}
        virtual WorldSession* GetSession() { return nullptr; }
};

class MovementAnticheatInterface
{
    public:
        MovementAnticheatInterface() {}
        virtual ~MovementAnticheatInterface() {}

        virtual void Init() {}
        virtual void InitNewPlayer(Player* pPlayer) {}
        virtual uint32 Update(uint32 diff, std::stringstream& reason) { return CHEAT_ACTION_NONE; }
        virtual uint32 Finalize(std::stringstream& reason) { return CHEAT_ACTION_NONE; }
        virtual bool IsInKnockBack() const { return false; }
        virtual void KnockBack(float speedxy, float speedz, float cos, float sin) {}

        virtual void AddCheats(uint32 cheats, uint32 count = 1) {}
        virtual void HandleCommand(ChatHandler* handler) const {}
        virtual void OnUnreachable(Unit* attacker) {}
        virtual void OnExplore(AreaEntry const* pArea) {}
        virtual void OnTransport(Player* plMover, ObjectGuid transportGuid) {}

        /*
            pPlayer - player who is being moved
            movementInfo - new movement info that was just received
            packet - the packet we are checking
        */
        virtual bool HandleAnticheatTests(Player* pPlayer, MovementInfo& movementInfo, WorldPacket* packet) { return true; }
        virtual bool HandleSpeedChangeAck(Player* pPlayer, MovementInfo& movementInfo, WorldPacket* packet, float newSpeed) { return true; }
        virtual bool CheckTeleport(Player* pPlayer, MovementInfo& movementInfo, uint32 opcode) { return true; }
        virtual void CheckMovementFlags(Player* pPlayer, MovementInfo& movementInfo) { }

        virtual void InitSpeeds(Unit* unit) {}
        virtual bool InterpolateMovement(MovementInfo const& mi, uint32 diffMs, float &x, float &y, float &z, float &o) { return true; }  
};

class AntispamInterface
{
public:
    virtual ~AntispamInterface() {}

    virtual void loadData() {}
    virtual void loadConfig() {}

    virtual std::string normalizeMessage(const std::string &msg, uint32 mask = 0) { return msg; }
    virtual bool filterMessage(const std::string &msg) { return 0; }

    virtual void addMessage(const std::string& msg, uint32 type, PlayerPointer from, PlayerPointer to) {}

    virtual bool isMuted(uint32 accountId, bool checkChatType = false, uint32 chatType = 0) const { return false; }
    virtual void mute(uint32 accountId) {}
    virtual void unmute(uint32 accountId) {}
    virtual void showMuted(WorldSession* session) {}
};

class AnticheatLibInterface
{
public:
    virtual ~AnticheatLibInterface() {}
    virtual WardenInterface* CreateWardenFor(WorldSession* client, BigNumber* K)
    {
        return new WardenInterface();
    }
    virtual MovementAnticheatInterface* CreateAnticheatFor(Player* player)
    {
        return new MovementAnticheatInterface();
    }
    virtual void LoadAnticheatData() {}
    virtual void LoadConfig() {}
    virtual void MapAccountUpdate(WorldSession* sess) {}
    virtual void SessionAdded(WorldSession* sess) {}
    virtual void OnPlayerLevelUp(Player* player) {}
    virtual void OnClientHashComputed(WorldSession* sess) {}

    // Antispam wrappers
    virtual AntispamInterface* GetAntispam() const { return nullptr; }
    virtual bool CanWhisper(AccountPersistentData const& data, MasterPlayer* player) { return true; }
};

#ifdef USE_ANTICHEAT

class MangosAnticheatLib: public AnticheatLibInterface
{
    public:
        MangosAnticheatLib() {}
        
        void LoadAnticheatData();
        
        WardenInterface * CreateWardenFor(WorldSession* client, BigNumber* K);
        MovementAnticheatInterface* CreateAnticheatFor(Player* player);

        static MangosAnticheatLib* instance();
};

#endif // USE_ANTICHEAT

AnticheatLibInterface* GetAnticheatLib();

#define sAnticheatLib (GetAnticheatLib())

#endif // ANTICHEAT_H