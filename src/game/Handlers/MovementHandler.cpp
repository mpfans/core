/*
 * Copyright (C) 2005-2011 MaNGOS <http://getmangos.com/>
 * Copyright (C) 2009-2011 MaNGOSZero <https://github.com/mangos/zero>
 * Copyright (C) 2011-2016 Nostalrius <https://nostalrius.org>
 * Copyright (C) 2016-2017 Elysium Project <https://github.com/elysium-project>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "Corpse.h"
#include "Player.h"
#include "MapManager.h"
#include "Transport.h"
#include "BattleGround.h"
#include "WaypointMovementGenerator.h"
#include "MapPersistentStateMgr.h"
#include "ObjectMgr.h"

#include "World.h"
#include "Anticheat.h"
#include "packet_builder.h"
#include "MoveSpline.h"
#include "MovementBroadcaster.h"
#include "MovementPacketSender.h"


void WorldSession::HandleMoveWorldportAckOpcode(WorldPacket & /*recvData*/)
{
    DEBUG_LOG("WORLD: got MSG_MOVE_WORLDPORT_ACK.");
    HandleMoveWorldportAckOpcode();
}

void WorldSession::HandleMoveWorldportAckOpcode()
{
    // ignore unexpected far teleports
    if (!GetPlayer()->IsBeingTeleportedFar())
        return;

    // get start teleport coordinates (will used later in fail case)
    WorldLocation old_loc;
    GetPlayer()->GetPosition(old_loc);

    // get the teleport destination
    WorldLocation &loc = GetPlayer()->GetTeleportDest();

    // possible errors in the coordinate validity check (only cheating case possible)
    if (!MapManager::IsValidMapCoord(loc.mapid, loc.coord_x, loc.coord_y, loc.coord_z, loc.orientation))
    {
        sLog.outError("WorldSession::HandleMoveWorldportAckOpcode: %s was teleported far to a not valid location "
                      "(map:%u, x:%f, y:%f, z:%f) We port him to his homebind instead..",
                      GetPlayer()->GetGuidStr().c_str(), loc.mapid, loc.coord_x, loc.coord_y, loc.coord_z);
        // stop teleportation else we would try this again and again in LogoutPlayer...
        GetPlayer()->SetSemaphoreTeleportFar(false);
        // and teleport the player to a valid place
        GetPlayer()->TeleportToHomebind();
        return;
    }

    // get the destination map entry, not the current one, this will fix homebind and reset greeting
    MapEntry const* mEntry = sMapStorage.LookupEntry<MapEntry>(loc.mapid);

    Map* map = nullptr;

    // prevent crash at attempt landing to not existed battleground instance
    if (mEntry->IsBattleGround())
    {
        if (GetPlayer()->GetBattleGroundId())
            map = sMapMgr.FindMap(loc.mapid, GetPlayer()->GetBattleGroundId());

        if (!map)
        {
            DETAIL_LOG("WorldSession::HandleMoveWorldportAckOpcode: %s was teleported far to nonexisten battleground instance "
                       " (map:%u, x:%f, y:%f, z:%f) Trying to port him to his previous place..",
                       GetPlayer()->GetGuidStr().c_str(), loc.mapid, loc.coord_x, loc.coord_y, loc.coord_z);

            GetPlayer()->SetSemaphoreTeleportFar(false);

            // Teleport to previous place, if cannot be ported back TP to homebind place
            if (!GetPlayer()->TeleportTo(old_loc))
            {
                DETAIL_LOG("WorldSession::HandleMoveWorldportAckOpcode: %s cannot be ported to his previous place, teleporting him to his homebind place...",
                           GetPlayer()->GetGuidStr().c_str());
                GetPlayer()->TeleportToHomebind();
            }
            return;
        }
    }

    // reset instance validity, except if going to an instance inside an instance
    if (GetPlayer()->m_InstanceValid == false && !mEntry->IsDungeon())
        GetPlayer()->m_InstanceValid = true;

    // relocate the player to the teleport destination
    if (!map)
    {
        if (loc.mapid <= 1)
            GetPlayer()->SetLocationInstanceId(sMapMgr.GetContinentInstanceId(loc.mapid, loc.coord_x, loc.coord_y));
        map = sMapMgr.CreateMap(loc.mapid, GetPlayer());
    }

    GetPlayer()->SetMap(map);
    if (Transport* t = GetPlayer()->GetTransport()) // Transport position may have changed while loading
        t->UpdatePassengerPosition(GetPlayer());
    else
        GetPlayer()->Relocate(loc.coord_x, loc.coord_y, loc.coord_z, loc.orientation);

    GetPlayer()->SendInitialPacketsBeforeAddToMap();
    // the CanEnter checks are done in TeleporTo but conditions may change
    // while the player is in transit, for example the map may get full
    if (!GetPlayer()->GetMap()->Add(GetPlayer()))
    {
        GetPlayer()->SetSemaphoreTeleportFar(false);
        // if player wasn't added to map, reset his map pointer!
        GetPlayer()->ResetMap();

        DETAIL_LOG("WorldSession::HandleMoveWorldportAckOpcode: %s was teleported far but couldn't be added to map "
                   " (map:%u, x:%f, y:%f, z:%f) Trying to port him to his previous place..",
                   GetPlayer()->GetGuidStr().c_str(), loc.mapid, loc.coord_x, loc.coord_y, loc.coord_z);

        // Teleport to previous place, if cannot be ported back TP to homebind place
        if (!GetPlayer()->TeleportTo(old_loc))
        {
            DETAIL_LOG("WorldSession::HandleMoveWorldportAckOpcode: %s cannot be ported to his previous place, teleporting him to his homebind place...",
                       GetPlayer()->GetGuidStr().c_str());
            GetPlayer()->TeleportToHomebind();
        }
        return;
    }
    GetPlayer()->SetSemaphoreTeleportFar(false);

    // battleground state prepare (in case join to BG), at relogin/tele player not invited
    // only add to bg group and object, if the player was invited (else he entered through command)
    if (_player->InBattleGround())
    {
        // cleanup seting if outdated
        if (!mEntry->IsBattleGround())
        {
            // We're not in BG
            _player->SetBattleGroundId(0, BATTLEGROUND_TYPE_NONE);
            // reset destination bg team
            _player->SetBGTeam(TEAM_NONE);
        }
        // join to bg case
        else if (BattleGround *bg = _player->GetBattleGround())
        {
            if (_player->IsInvitedForBattleGroundInstance(_player->GetBattleGroundId()))
                bg->AddPlayer(_player);
        }
    }

    GetPlayer()->SendInitialPacketsAfterAddToMap();

    // flight fast teleport case
    if (GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
    {
        // the movement could be expiring so check for destination (ritual of summoning / gm teleport)
        if (!_player->InBattleGround() && !_player->m_taxi.empty())
        {
            // short preparations to continue flight
            FlightPathMovementGenerator* flight = (FlightPathMovementGenerator*)(GetPlayer()->GetMotionMaster()->top());
            flight->Reset(*GetPlayer());
            return;
        }

        // battleground state prepare, stop flight
        GetPlayer()->GetMotionMaster()->MovementExpired();
        GetPlayer()->m_taxi.ClearTaxiDestinations();
    }

    if (mEntry->IsRaid())
    {
        if (time_t timeReset = sMapPersistentStateMgr.GetScheduler().GetResetTimeFor(mEntry->id))
        {
            uint32 timeleft = uint32(timeReset - time(nullptr));
            GetPlayer()->SendInstanceResetWarning(mEntry->id, timeleft);
        }
    }

    // mount allow check
    if (!mEntry->IsMountAllowed())
        _player->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);

    // honorless target
    if (!GetPlayer()->pvpInfo.inPvPEnforcedArea)
        GetPlayer()->RemoveDelayedOperation(DELAYED_CAST_HONORLESS_TARGET);

    // resummon pet
    GetPlayer()->ResummonPetTemporaryUnSummonedIfAny();

    //lets process all delayed operations on successful teleport
    GetPlayer()->ProcessDelayedOperations();

    // Let the client know its new position by sending a heartbeat!
    // The Windows client figures this out by itself, but the MacOS one does
    // not.
    //
    // On a successful port, the camera of the MacOS client is facing south and
    // ignores any movement from the transport object. Triggering
    // `SMSG_STANDSTATE_UPDATE' with its current state resets the camera
    // (implemented in `WorldSession::HandleZoneUpdateOpcode').
    if (_clientOS == CLIENT_OS_MAC && GetPlayer()->m_movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        GetPlayer()->SendHeartBeat(true);
    }
}

void WorldSession::HandleMoveTeleportAckOpcode(WorldPacket& recvData)
{
    DEBUG_LOG("MSG_MOVE_TELEPORT_ACK");

    ObjectGuid guid;
    recvData >> guid;

    uint32 counter = 0;
    uint32 time = 0;
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
    recvData >> counter >> time;
#else
    recvData >> time;
#endif
    DEBUG_LOG("Guid: %s", guid.GetString().c_str());
    DEBUG_LOG("Counter %u, time %u", counter, time / IN_MILLISECONDS);

    Unit *pMover = _player->GetMover();
    Player *pPlayerMover = pMover->GetTypeId() == TYPEID_PLAYER ? (Player*)pMover : NULL;

    if (!pPlayerMover || !pPlayerMover->IsBeingTeleportedNear())
        return;

    if (guid != pPlayerMover->GetObjectGuid())
        return;

    pPlayerMover->SetSemaphoreTeleportNear(false);

    WorldLocation const& dest = pPlayerMover->GetTeleportDest();
    pPlayerMover->TeleportPositionRelocation(dest.coord_x, dest.coord_y, dest.coord_z, dest.orientation);

    // resummon pet, if the destination is in another continent instance, let Player::SwitchInstance do it
    // because the client will request the name for the old pet guid and receive no answer
    // result would be a pet named "unknown"
    if (pPlayerMover->GetTemporaryUnsummonedPetNumber())
        if (sWorld.getConfig(CONFIG_BOOL_CONTINENTS_INSTANCIATE) && pPlayerMover->GetMap()->IsContinent())
        {
            bool transition = false;
            if (sMapMgr.GetContinentInstanceId(pPlayerMover->GetMap()->GetId(), dest.coord_x, dest.coord_y, &transition) == pPlayerMover->GetInstanceId())
                pPlayerMover->ResummonPetTemporaryUnSummonedIfAny();
        }
        else
            pPlayerMover->ResummonPetTemporaryUnSummonedIfAny();

    //lets process all delayed operations on successful teleport
    pPlayerMover->ProcessDelayedOperations();

    // Si le joueur est stun, il ne pourra pas envoyer sa position -> Fix desynchro ici.
    if (pPlayerMover->hasUnitState(UNIT_STAT_NO_FREE_MOVE))
    {
        pPlayerMover->m_movementInfo.moveFlags &= ~MOVEFLAG_MASK_MOVING_OR_TURN;
        pPlayerMover->SendHeartBeat(false);
    }
}

void WorldSession::HandleMovementOpcodes(WorldPacket & recvData)
{
    uint32 opcode = recvData.GetOpcode();
    DEBUG_LOG("WORLD: Recvd %s (%u, 0x%X) opcode", LookupOpcodeName(opcode), opcode, opcode);

    Unit *pMover = _player->GetMover();

    if (pMover->GetObjectGuid() != _clientMoverGuid)
        return;
        
    Player *pPlayerMover = pMover->ToPlayer();

    // ignore, waiting processing in WorldSession::HandleMoveWorldportAckOpcode and WorldSession::HandleMoveTeleportAck
    if (pPlayerMover && pPlayerMover->IsBeingTeleported())
    {
        recvData.rpos(recvData.wpos());                   // prevent warnings spam
        return;
    }

    /* extract packet */
    MovementInfo movementInfo = pPlayerMover ? pPlayerMover->m_movementInfo : MovementInfo();
    recvData >> movementInfo;
    movementInfo.UpdateTime(recvData.GetPacketTime());

    /*----------------*/

    if (!VerifyMovementInfo(movementInfo))
        return;

    if (pPlayerMover)
    {
        if (!_player->GetCheatData()->CheckTeleport(pPlayerMover, movementInfo, opcode))
            return;

        if (!_player->GetCheatData()->HandleAnticheatTests(pPlayerMover, movementInfo, opcode))
            return;

        _player->GetCheatData()->CheckMovementFlags(pPlayerMover, movementInfo);
    }

    // Interrupt spell cast at move
    if (movementInfo.HasMovementFlag(MOVEFLAG_MASK_MOVING))
        pMover->InterruptSpellsWithInterruptFlags(SPELL_INTERRUPT_FLAG_MOVEMENT);

    HandleMoverRelocation(movementInfo);

    // fall damage generation (ignore in flight case that can be triggered also at lags in moment teleportation to another map).
    if (opcode == MSG_MOVE_FALL_LAND && pPlayerMover && !pPlayerMover->IsTaxiFlying())
        pPlayerMover->HandleFall(movementInfo);

    // TODO: remove it
    // reset knockback state when fall to ground or water
    if (pPlayerMover)
    {
        if ((opcode == MSG_MOVE_FALL_LAND || opcode == MSG_MOVE_START_SWIM) && pPlayerMover->IsLaunched())
        {
            pPlayerMover->SetLaunched(false);
            pPlayerMover->SetXYSpeed(0.0f);
        }
    }

    if (pPlayerMover)
        pPlayerMover->UpdateFallInformationIfNeed(movementInfo, opcode);

    WorldPacket data(opcode, recvData.size());

#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_8_4
    data << _clientMoverGuid.WriteAsPacked();
#else
    data << _clientMoverGuid.GetRawValue();
#endif
    movementInfo.Write(data);

    pMover->SendMovementMessageToSet(std::move(data), true, _player);

    // Fix movement issue on older clients where if the player jumps while running,
    // and then lets go of the key while in the air, he appears to continue moving
    // forward on other people's screen. Once he moves for real, they will see him
    // teleport back to where he was standing after he jumped.
#if SUPPORTED_CLIENT_BUILD == CLIENT_BUILD_1_9_4
    if (opcode == MSG_MOVE_FALL_LAND)
    {
        uint16 opcode2 = 0;
        if (!movementInfo.HasMovementFlag(MOVEFLAG_MASK_MOVING))
            opcode2 = MSG_MOVE_STOP;
        else if (movementInfo.HasMovementFlag(MOVEFLAG_BACKWARD))
            opcode2 = MSG_MOVE_START_BACKWARD;
        else if (movementInfo.HasMovementFlag(MOVEFLAG_FORWARD))
            opcode2 = MSG_MOVE_START_FORWARD;
        else if (movementInfo.HasMovementFlag(MOVEFLAG_STRAFE_LEFT))
            opcode2 = MSG_MOVE_START_STRAFE_LEFT;
        else if (movementInfo.HasMovementFlag(MOVEFLAG_STRAFE_RIGHT))
            opcode2 = MSG_MOVE_START_STRAFE_RIGHT;

        if (opcode2)
        {
            WorldPacket data(opcode2, recvData.size());
            data << _clientMoverGuid.WriteAsPacked();             // write guid
            movementInfo.Write(data);                             // write data

            pMover->SendMovementMessageToSet(std::move(data), true, _player);
        }
    }
#endif
}

void WorldSession::HandleForceSpeedChangeAckOpcodes(WorldPacket &recvData)
{
    uint32 opcode = recvData.GetOpcode();
    DEBUG_LOG("WORLD: Recvd %s (%u, 0x%X) opcode", LookupOpcodeName(opcode), opcode, opcode);

    /* extract packet */
    ObjectGuid guid;
    recvData >> guid;
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
    uint32 movementCounter;
    recvData >> movementCounter;
#endif
    MovementInfo movementInfo;
    recvData >> movementInfo;
    float  speedReceived;
    recvData >> speedReceived;
    movementInfo.UpdateTime(recvData.GetPacketTime());

    // now can skip not our packet
    if (guid != _clientMoverGuid && guid != _player->GetObjectGuid() && guid != _player->GetMover()->GetObjectGuid())
        return;

    Unit* pMover = ObjectAccessor::GetUnit(*_player, guid);

    if (!pMover)
        return;

    Player* pPlayerMover = pMover->ToPlayer();

    if (!VerifyMovementInfo(movementInfo))
        return;

    if (pPlayerMover && !_player->GetCheatData()->CheckTeleport(pPlayerMover, movementInfo, opcode))
        return;

    /*----------------*/

    // client ACK send one packet for mounted/run case and need skip all except last from its
    // in other cases anti-cheat check can be fail in false case
    UnitMoveType move_type;
    switch (opcode)
    {
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:
            move_type = MOVE_WALK;
            break;
        case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:
            move_type = MOVE_RUN;
            break;
        case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:
            move_type = MOVE_RUN_BACK;
            break;
        case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:
            move_type = MOVE_SWIM;
            break;
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:
            move_type = MOVE_SWIM_BACK;
            break;
        case CMSG_FORCE_TURN_RATE_CHANGE_ACK:
            move_type = MOVE_TURN_RATE;
            break;
        default:
            sLog.outError("WorldSession::HandleForceSpeedChangeAck: Unknown move type opcode: %u", opcode);
            return;
    }

    // verify that indeed the client is replying with the changes that were send to him
    if (!pMover->HasPendingMovementChange())
    {
        sLog.outInfo("WorldSession::HandleForceSpeedChangeAck: Player %s from account id %u kicked because no movement change ack was expected from this player",
            _player->GetName(), _player->GetSession()->GetAccountId());
        _player->GetSession()->KickPlayer();
        return;
    }

    PlayerMovementPendingChange pendingChange = pMover->PopPendingMovementChange();

#if SUPPORTED_CLIENT_BUILD <= CLIENT_BUILD_1_9_4
    uint32 movementCounter = pendingChange.movementCounter;
#endif

    if (!_player->GetCheatData()->HandleSpeedChangeAck(pMover, movementInfo, speedReceived, movementCounter, move_type, pendingChange, opcode))
        return;

    float speedSent = pendingChange.newValue;

    // the client data has been verified. let's do the actual change now
    float newSpeedRate = speedSent / baseMoveSpeed[move_type];
    HandleMoverRelocation(movementInfo);
    if (pPlayerMover)
        pPlayerMover->UpdateFallInformationIfNeed(movementInfo, opcode);
    pMover->SetSpeedRateReal(move_type, newSpeedRate);

    MovementPacketSender::SendSpeedChangeToObservers(pMover, move_type, speedSent);
}

/*
handles those packets:
CMSG_MOVE_WATER_WALK_ACK
CMSG_MOVE_HOVER_ACK
CMSG_MOVE_FEATHER_FALL_ACK
*/
void WorldSession::HandleMovementFlagChangeToggleAck(WorldPacket& recvData)
{
    /* extract packet */
    ObjectGuid guid;
    recvData >> guid;
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
    uint32 movementCounter;
    recvData >> movementCounter;
#endif
    MovementInfo movementInfo;
    recvData >> movementInfo;
    movementInfo.UpdateTime(recvData.GetPacketTime());
    uint32 applyInt;
    recvData >> applyInt;
    bool applyReceived = applyInt == 0u ? false : true;

    // make sure this client is allowed to control the unit which guid is provided
    if (guid != _clientMoverGuid && guid != _player->GetObjectGuid() && guid != _player->GetMover()->GetObjectGuid())
        return;

    Unit* pMover = ObjectAccessor::GetUnit(*_player, guid);

    if (!pMover)
        return;

    Player* pPlayerMover = pMover->ToPlayer();

    if (!VerifyMovementInfo(movementInfo))
        return;

    if (pPlayerMover)
    {
        if (!_player->GetCheatData()->CheckTeleport(pPlayerMover, movementInfo, recvData.GetOpcode()))
            return;

        if (!_player->GetCheatData()->HandleAnticheatTests(pPlayerMover, movementInfo, recvData.GetOpcode()))
            return;
    }

    // verify that indeed the client is replying with the changes that were send to him
    if (!pMover->HasPendingMovementChange())
    {
        sLog.outInfo("WorldSession::HandleMovementFlagChangeToggleAck: Player %s from account id %u kicked because no movement change ack was expected from this player",
            _player->GetName(), _player->GetSession()->GetAccountId());
        _player->GetSession()->KickPlayer();
        return;
    }

    PlayerMovementPendingChange pendingChange = pMover->PopPendingMovementChange();
    MovementFlags mFlag = MOVEFLAG_NONE;
    MovementChangeType changeTypeReceived;

    switch (recvData.GetOpcode())
    {
        case CMSG_MOVE_WATER_WALK_ACK:      changeTypeReceived = WATER_WALK; mFlag = MOVEFLAG_WATERWALKING; break;
        case CMSG_MOVE_HOVER_ACK:           changeTypeReceived = SET_HOVER; mFlag = MOVEFLAG_HOVER; break;
        case CMSG_MOVE_FEATHER_FALL_ACK:    changeTypeReceived = FEATHER_FALL; mFlag = MOVEFLAG_SAFE_FALL; break;
        default:
            sLog.outInfo("WorldSession::HandleMovementFlagChangeToggleAck: Unknown move type opcode: %u", recvData.GetOpcode());
            return;
    }

#if SUPPORTED_CLIENT_BUILD <= CLIENT_BUILD_1_9_4
    uint32 movementCounter = pendingChange.movementCounter;
#endif

    if (!_player->GetCheatData()->HandleMovementFlagChangeAck(pMover, movementInfo, movementCounter, applyReceived, changeTypeReceived, pendingChange))
        return;

    // Position change
    HandleMoverRelocation(movementInfo);
    if (pPlayerMover)
        pPlayerMover->UpdateFallInformationIfNeed(movementInfo, recvData.GetOpcode());

    switch (changeTypeReceived)
    {
        case WATER_WALK:            pMover->SetWaterWalkingReal(applyReceived); break;
        case SET_HOVER:             pMover->SetHoverReal(applyReceived); break;
        case FEATHER_FALL:          pMover->SetFeatherFallReal(applyReceived); break;
        default:
            sLog.outInfo("WorldSession::HandleMovementFlagChangeToggleAck: Unknown move type opcode: %u", recvData.GetOpcode());
            return;
    }

    if(mFlag != MOVEFLAG_NONE)
        MovementPacketSender::SendMovementFlagChangeToObservers(pMover, mFlag, pendingChange.apply);
}

/*
handles those packets:
CMSG_FORCE_MOVE_ROOT_ACK
CMSG_FORCE_MOVE_UNROOT_ACK
*/
void WorldSession::HandleMoveRootAck(WorldPacket& recvData)
{
    DEBUG_LOG("HandleMoveRootAck");
    
    /* extract packet */
    ObjectGuid guid;
    recvData >> guid;
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
    uint32 movementCounter;
    recvData >> movementCounter;
#endif
    MovementInfo movementInfo;
    recvData >> movementInfo;
    movementInfo.UpdateTime(recvData.GetPacketTime());

    // make sure this client is allowed to control the unit which guid is provided
    if (guid != _clientMoverGuid && guid != _player->GetObjectGuid() && guid != _player->GetMover()->GetObjectGuid())
        return;

    Unit* pMover = ObjectAccessor::GetUnit(*_player, guid);

    if (!pMover)
        return;

    if (!VerifyMovementInfo(movementInfo))
        return;

    Player* pPlayerMover = pMover->ToPlayer();

    if (pPlayerMover)
    {
        if (!_player->GetCheatData()->CheckTeleport(pPlayerMover, movementInfo, recvData.GetOpcode()))
            return;

        if (!_player->GetCheatData()->HandleAnticheatTests(pPlayerMover, movementInfo, recvData.GetOpcode()))
            return;
    }

    // verify that indeed the client is replying with the changes that were send to him
    if (!pMover->HasPendingMovementChange())
    {
        sLog.outInfo("WorldSession::HandleMoveRootAck: Player %s from account id %u kicked because no movement change ack was expected from this player",
            _player->GetName(), _player->GetSession()->GetAccountId());
        _player->GetSession()->KickPlayer();
        return;
    }

#if SUPPORTED_CLIENT_BUILD <= CLIENT_BUILD_1_9_4
    uint32 movementCounter = pendingChange.movementCounter;
#endif

    PlayerMovementPendingChange pendingChange = pMover->PopPendingMovementChange();
    bool applyReceived = (recvData.GetOpcode() == CMSG_FORCE_MOVE_ROOT_ACK);

    if (!_player->GetCheatData()->HandleRootUnrootAck(pMover, movementInfo, movementCounter, applyReceived, pendingChange))
        return;

    // Update position if it has changed
    HandleMoverRelocation(movementInfo);
    if (pPlayerMover)
        pPlayerMover->UpdateFallInformationIfNeed(movementInfo, recvData.GetOpcode());
    pMover->SetRootedReal(applyReceived);

    MovementPacketSender::SendMovementFlagChangeToObservers(pMover, MOVEFLAG_ROOT, applyReceived);

    // Set unit client state for brevity, though it should not be used
    if (applyReceived)
        pMover->addUnitState(UNIT_STAT_CLIENT_ROOT);
    else
        pMover->clearUnitState(UNIT_STAT_CLIENT_ROOT);
}

void WorldSession::HandleMoveKnockBackAck(WorldPacket & recvData)
{
    DEBUG_LOG("CMSG_MOVE_KNOCK_BACK_ACK");

    ObjectGuid guid;
    recvData >> guid;
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
    uint32 movementCounter;
    recvData >> movementCounter;
#endif
    MovementInfo movementInfo;
    recvData >> movementInfo;
    movementInfo.UpdateTime(recvData.GetPacketTime());

    if (guid != _clientMoverGuid && guid != _player->GetObjectGuid() && guid != _player->GetMover()->GetObjectGuid())
        return;

    Unit* pMover = ObjectAccessor::GetUnit(*_player, guid);

    if (!pMover)
        return;

    Player *pPlayerMover = pMover->ToPlayer();

    // ignore, waiting processing in WorldSession::HandleMoveWorldportAckOpcode and WorldSession::HandleMoveTeleportAck
    if (pPlayerMover && pPlayerMover->IsBeingTeleported())
    {
        pMover->PopPendingMovementChange();
        recvData.rpos(recvData.wpos());                   // prevent warnings spam
        return;
    }

    if (!VerifyMovementInfo(movementInfo))
        return;

    // verify that indeed the client is replying with the changes that were send to him
    if (!pMover->HasPendingMovementChange())
    {
        sLog.outInfo("WorldSession::HandleMoveKnockBackAck: Player %s from account id %u kicked because no movement change ack was expected from this player",
            _player->GetName(), _player->GetSession()->GetAccountId());
        _player->GetSession()->KickPlayer();
        return;
    }

    PlayerMovementPendingChange pendingChange = pMover->PopPendingMovementChange();

#if SUPPORTED_CLIENT_BUILD <= CLIENT_BUILD_1_9_4
    uint32 movementCounter = pendingChange.movementCounter;
#endif

    if (!_player->GetCheatData()->HandleKnockbackAck(pMover, movementInfo, movementCounter, pendingChange))
        return;

    HandleMoverRelocation(movementInfo);

    MovementPacketSender::SendKnockBackToObservers(pMover, movementInfo.jump.cosAngle, movementInfo.jump.sinAngle, movementInfo.jump.xyspeed, movementInfo.jump.velocity);
}

void WorldSession::HandleMoveSplineDoneOpcode(WorldPacket& recvData)
{
    DEBUG_LOG("WORLD: Received CMSG_MOVE_SPLINE_DONE");

    MovementInfo movementInfo;                              // used only for proper packet read

    recvData >> movementInfo;
    recvData >> Unused<uint32>();                          // unk
    recvData >> Unused<uint32>();                          // unk2

    // Forward packet to near players
    recvData.SetOpcode(MSG_MOVE_STOP);
    recvData.rpos(0);
    HandleMovementOpcodes(recvData);
}

void WorldSession::HandleSetActiveMoverOpcode(WorldPacket &recvData)
{
    DEBUG_LOG("WORLD: Recvd CMSG_SET_ACTIVE_MOVER");

    ObjectGuid guid;
    recvData >> guid;

    ObjectGuid serverMoverGuid = _player->GetMover()->GetObjectGuid();

    // Before 1.10, client sends 0 as guid if it has no control.
#if SUPPORTED_CLIENT_BUILD <= CLIENT_BUILD_1_9_4
    if ((serverMoverGuid == _player->GetObjectGuid()) && !_player->HasSelfMovementControl())
        serverMoverGuid = ObjectGuid();
#endif

    if (serverMoverGuid != guid)
    {
        sLog.outError("HandleSetActiveMoverOpcode: incorrect pMover guid: pMover is %s and should be %s",
                      _player->GetMover()->GetGuidStr().c_str(), guid.GetString().c_str());
        _clientMoverGuid = _player->GetMover()->GetObjectGuid();
        return;
    }

    // pMover swap after Eyes of the Beast, PetAI::UpdateAI handle the pet's return
    // Check if we actually have a pet before looking up
    if (_player->GetPetGuid() && _player->GetPetGuid() == _clientMoverGuid)
    {
        if (Pet* pet = _player->GetPet())
        {
            pet->clearUnitState(UNIT_STAT_POSSESSED);
            pet->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_POSSESSED);
            // out of range pet dismissed
            if (!pet->IsWithinDistInMap(_player, pet->GetMap()->GetGridActivationDistance()))
                _player->RemovePet(PET_SAVE_REAGENTS);
        }
    }

    _clientMoverGuid = guid;
}

void WorldSession::HandleMoveNotActiveMoverOpcode(WorldPacket &recvData)
{
    DEBUG_LOG("WORLD: Recvd CMSG_MOVE_NOT_ACTIVE_MOVER");
    recvData.hexlike();

    MovementInfo mi;

#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_9_4
    ObjectGuid old_mover_guid;
    recvData >> old_mover_guid;
    recvData >> mi;
    _clientMoverGuid = ObjectGuid();

    // Client sent not active pMover, but maybe the pMover is actually set?
    if (_player->GetMover() && _player->GetMover()->GetObjectGuid() == old_mover_guid)
    {
        DETAIL_LOG("HandleMoveNotActiveMover: incorrect pMover guid: pMover is %s and should be %s instead of %s",
                       _player->GetMover()->GetGuidStr().c_str(),
                       _player->GetGuidStr().c_str(),
                       old_mover_guid.GetString().c_str());
        recvData.rpos(recvData.wpos());                   // prevent warnings spam
        return;
    }
#else
    recvData >> mi;
    _clientMoverGuid = ObjectGuid();
#endif

    // Prevent client from removing root flag.
    if (_player->HasUnitMovementFlag(MOVEFLAG_ROOT) && !mi.HasMovementFlag(MOVEFLAG_ROOT))
        mi.AddMovementFlag(MOVEFLAG_ROOT);

    _player->m_movementInfo = mi;
}

void WorldSession::HandleMountSpecialAnimOpcode(WorldPacket& /*recvdata*/)
{
    //DEBUG_LOG("WORLD: Recvd CMSG_MOUNTSPECIAL_ANIM");

    WorldPacket data(SMSG_MOUNTSPECIAL_ANIM, 8);
    data << GetPlayer()->GetObjectGuid();

    GetPlayer()->SendMovementMessageToSet(std::move(data), false);
}

void WorldSession::HandleSummonResponseOpcode(WorldPacket& recvData)
{
    if (!_player->isAlive() || _player->isInCombat())
        return;

    ObjectGuid summonerGuid;
    recvData >> summonerGuid;

    _player->SummonIfPossible(true);
}

void WorldSession::HandleMoveTimeSkippedOpcode(WorldPacket & recvData)
{
    DEBUG_LOG("WORLD: Time Lag/Synchronization Resent/Update");

    ObjectGuid g;
    recvData >> g;
    uint32 lag;
    recvData >> lag;

    Player* pl = GetPlayer();

    pl->m_movementInfo.time += lag;
    pl->m_movementInfo.ctime += lag;

    // fix an 1.12 client problem with transports
    Transport* tr = pl->GetTransport();
    if (pl->HasJustBoarded() && tr)
    {
        pl->SetJustBoarded(false);
        tr->SendOutOfRangeUpdateToPlayer(pl);
        tr->SendCreateUpdateToPlayer(pl);
    }
#if SUPPORTED_CLIENT_BUILD > CLIENT_BUILD_1_8_4
    else
    {
        WorldPacket data(MSG_MOVE_TIME_SKIPPED, 12);
        data << pl->GetPackGUID();
        data << lag;
        pl->SendMovementMessageToSet(std::move(data), false);
    }
#endif
}

bool WorldSession::VerifyMovementInfo(MovementInfo const& movementInfo, ObjectGuid const& guid) const
{
    // ignore wrong guid (player attempt cheating own session for not own guid possible...)
    if (guid != _player->GetMover()->GetObjectGuid())
        return false;

    return VerifyMovementInfo(movementInfo);
}

bool WorldSession::VerifyMovementInfo(MovementInfo const& movementInfo) const
{
    if (!MaNGOS::IsValidMapCoord(movementInfo.GetPos()->x, movementInfo.GetPos()->y, movementInfo.GetPos()->z, movementInfo.GetPos()->o))
        return false;

    if (movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        // transports size limited
        // (also received at zeppelin/lift leave by some reason with t_* as absolute in continent coordinates, can be safely skipped)
        if (std::fabs(movementInfo.GetTransportPos()->x) > 250 || std::fabs(movementInfo.GetTransportPos()->y) > 250 || std::fabs(movementInfo.GetTransportPos()->z) > 100)
            return false;

        if (!MaNGOS::IsValidMapCoord(movementInfo.GetPos()->x + movementInfo.GetTransportPos()->x, movementInfo.GetPos()->y + movementInfo.GetTransportPos()->y,
                                     movementInfo.GetPos()->z + movementInfo.GetTransportPos()->z, movementInfo.GetPos()->o + movementInfo.GetTransportPos()->o))
            return false;
    }

    return true;
}

void WorldSession::HandleMoverRelocation(MovementInfo& movementInfo)
{
    Unit *pMover = _player->GetMover();
    movementInfo.CorrectData(pMover);

    // Prevent client from removing root flag.
    if (pMover->HasUnitMovementFlag(MOVEFLAG_ROOT) && !movementInfo.HasMovementFlag(MOVEFLAG_ROOT))
        movementInfo.AddMovementFlag(MOVEFLAG_ROOT);

    if (Player* pPlayerMover = pMover->ToPlayer())
    {
        // ignore current relocation if needed
        if (pPlayerMover->IsNextRelocationIgnored())
        {
            pPlayerMover->DoIgnoreRelocation();
            return;
        }

        if (movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
        {
            GetPlayer()->GetCheatData()->OnTransport(pPlayerMover, movementInfo.GetTransportGuid());
            Unit* loadPetOnTransport = nullptr;
            if (!pPlayerMover->GetTransport())
            {
                if (Transport* t = pPlayerMover->GetMap()->GetTransport(movementInfo.GetTransportGuid()))
                {
                    t->AddPassenger(pPlayerMover);
                    if (Pet* pet = pPlayerMover->GetPet())
                        if (pet->GetTransport() != t)
                            loadPetOnTransport = pet;
                }
                // fix an 1.12 client problem with transports
                pPlayerMover->SetJustBoarded(true);
            }
            else
                pPlayerMover->SetJustBoarded(false);
            if (pPlayerMover->GetTransport())
            {
                movementInfo.pos.x = movementInfo.GetTransportPos()->x;
                movementInfo.pos.y = movementInfo.GetTransportPos()->y;
                movementInfo.pos.z = movementInfo.GetTransportPos()->z;
                movementInfo.pos.o = movementInfo.GetTransportPos()->o;
                pPlayerMover->GetTransport()->CalculatePassengerPosition(movementInfo.pos.x, movementInfo.pos.y, movementInfo.pos.z, &movementInfo.pos.o);
                if (loadPetOnTransport)
                {
                    loadPetOnTransport->NearTeleportTo(movementInfo.pos.x, movementInfo.pos.y, movementInfo.pos.z, movementInfo.pos.o);
                    pPlayerMover->GetTransport()->AddPassenger(loadPetOnTransport);
                }
            }
        }
        else if (pPlayerMover->GetTransport())
        {
            pPlayerMover->GetTransport()->RemovePassenger(pPlayerMover);
            if (Pet* pet = pPlayerMover->GetPet())
            {
                // If moving on transport, stop it.
                pet->DisableSpline();
                if (pet->GetTransport())
                {
                    pet->GetTransport()->RemovePassenger(pet);
                    pet->NearTeleportTo(movementInfo.pos.x, movementInfo.pos.y, movementInfo.pos.z, movementInfo.pos.o);
                }
            }
        }

        if (movementInfo.HasMovementFlag(MOVEFLAG_SWIMMING) != pPlayerMover->IsInWater())
        {
            // now client not include swimming flag in case jumping under water
            pPlayerMover->SetInWater(!pPlayerMover->IsInWater() || pPlayerMover->GetTerrain()->IsUnderWater(movementInfo.GetPos()->x, movementInfo.GetPos()->y, movementInfo.GetPos()->z));
        }

        pPlayerMover->SetPosition(movementInfo.GetPos()->x, movementInfo.GetPos()->y, movementInfo.GetPos()->z, movementInfo.GetPos()->o);
        pPlayerMover->m_movementInfo = movementInfo;

        // super smart decision; rework required
        if (ObjectGuid lootGuid = pPlayerMover->GetLootGuid() && !lootGuid.IsItem())
            pPlayerMover->SendLootRelease(lootGuid);

        // Nostalrius - antiundermap1
        if (movementInfo.HasMovementFlag(MOVEFLAG_FALLINGFAR))
        {
            float hauteur = pPlayerMover->GetMap()->GetHeight(pPlayerMover->GetPositionX(), pPlayerMover->GetPositionY(), pPlayerMover->GetPositionZ(), true);
            bool undermap = false;
            // Undermap
            if ((pPlayerMover->GetPositionZ() + 100.0f) < hauteur)
                undermap = true;
            if (pPlayerMover->GetPositionZ() < 250.0f && pPlayerMover->GetMapId() == 489)
                undermap = true;

            if (undermap)
                if (pPlayerMover->UndermapRecall())
                    sLog.outInfo("[UNDERMAP] %s [GUID %u]. MapId:%u %f %f %f", pPlayerMover->GetName(), pPlayerMover->GetGUIDLow(), pPlayerMover->GetMapId(), pPlayerMover->GetPositionX(), pPlayerMover->GetPositionY(), pPlayerMover->GetPositionZ());
        }
        else if (pPlayerMover->CanFreeMove())
            pPlayerMover->SaveNoUndermapPosition(movementInfo.GetPos()->x, movementInfo.GetPos()->y, movementInfo.GetPos()->z + 3.0f);
        // Antiundermap2: teleportation au cimetiere
        if (movementInfo.GetPos()->z < -500.0f)
        {
            // NOTE: this is actually called many times while falling
            // even after the player has been teleported away
            // TODO: discard movement packets after the player is rooted
            if (pPlayerMover->isAlive())
            {
                // Nostalrius : pas mort quand on chute
                if (pPlayerMover->InBattleGround())
                    pPlayerMover->EnvironmentalDamage(DAMAGE_FALL_TO_VOID, pPlayerMover->GetHealth());
                else
                    pPlayerMover->EnvironmentalDamage(DAMAGE_FALL_TO_VOID, pPlayerMover->GetHealth() / 2);
                // pl can be alive if GM/etc
                if (!pPlayerMover->isAlive())
                {
                    // change the death state to CORPSE to prevent the death timer from
                    // starting in the next player update
                    pPlayerMover->KillPlayer();
                    pPlayerMover->BuildPlayerRepop();
                }
            }

            // cancel the death timer here if started
            sLog.outInfo("[UNDERMAP/Teleport] Player %s teleported.", pPlayerMover->GetName(), pPlayerMover->GetGUIDLow(), pPlayerMover->GetMapId(), pPlayerMover->GetPositionX(), pPlayerMover->GetPositionY(), pPlayerMover->GetPositionZ());
            pPlayerMover->RepopAtGraveyard();
        }
    }
    else                                                    // creature charmed
    {
        if (pMover->IsInWorld())
            pMover->GetMap()->CreatureRelocation((Creature*)pMover, movementInfo.GetPos()->x, movementInfo.GetPos()->y, movementInfo.GetPos()->z, movementInfo.GetPos()->o);
    }
}

