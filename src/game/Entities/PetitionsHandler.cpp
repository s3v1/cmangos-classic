/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
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
#include "Tools/Language.h"
#include "Server/WorldPacket.h"
#include "Server/WorldSession.h"
#include "World/World.h"
#include "Globals/ObjectMgr.h"
#include "Log.h"
#include "Server/Opcodes.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Entities/GossipDef.h"
#include "Social/SocialMgr.h"
#include "Anticheat/Anticheat.hpp"

/*enum PetitionType // dbc data
{
    PETITION_TYPE_GUILD      = 1,
    PETITION_TYPE_ARENA_TEAM = 3
};*/

// Charters ID in item_template
#define GUILD_CHARTER               5863
#define GUILD_CHARTER_COST          1000                    // 10 S
#define CHARTER_DISPLAY_ID          16161

void WorldSession::HandlePetitionBuyOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_PETITION_BUY");
    recv_data.hexlike();

    ObjectGuid guidNPC;
    uint32 unk2;
    std::string name;

    recv_data >> guidNPC;                                   // NPC GUID
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint64>();                          // 0
    recv_data >> name;                                      // name
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint16>();                          // 0
    recv_data.read_skip<uint8>();                           // 0

    recv_data >> unk2;                                      // index
    recv_data.read_skip<uint32>();                          // 0

    DEBUG_LOG("Petitioner %s tried sell petition: name %s", guidNPC.GetString().c_str(), name.c_str());

    // prevent cheating
    Creature* pCreature = GetPlayer()->GetNPCIfCanInteractWith(guidNPC, UNIT_NPC_FLAG_PETITIONER);
    if (!pCreature)
    {
        DEBUG_LOG("WORLD: HandlePetitionBuyOpcode - %s not found or you can't interact with him.", guidNPC.GetString().c_str());
        return;
    }

    if (!pCreature->isTabardDesigner())
        return;

    // if tabard designer, then trying to buy a guild charter.
    // do not let if already in guild.
    if (_player->GetGuildId())
        return;

    uint32 charterid = GUILD_CHARTER;
    uint32 cost = GUILD_CHARTER_COST;

    if (sGuildMgr.GetGuildByName(name))
    {
        SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_EXISTS_S);
        return;
    }

    // Check guild petition name (use whisper type - 6)
    if (sAnticheatLib->ValidateGuildName(name))
    {
        sLog.outBasic("Attempt to create guild petition with spam name \"%s\"", name.c_str());
        SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_INVALID);
        return;
    }

    if (sObjectMgr.IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
    {
        SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_INVALID);
        return;
    }

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(charterid);
    if (!pProto)
    {
        _player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, nullptr, charterid, 0);
        return;
    }

    if (_player->GetMoney() < cost)
    {
        // player hasn't got enough money
        _player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, pCreature, charterid, 0);
        return;
    }

    ItemPosCountVec dest;
    InventoryResult msg = _player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, charterid, pProto->BuyCount);
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, nullptr, nullptr, charterid);
        return;
    }

    _player->ModifyMoney(-(int32)cost);
    Item* charter = _player->StoreNewItem(dest, charterid, true);
    if (!charter)
        return;

    charter->SetUInt32Value(ITEM_FIELD_ENCHANTMENT, charter->GetGUIDLow());
    // ITEM_FIELD_ENCHANTMENT is guild
    // ITEM_FIELD_ENCHANTMENT+1 is current signatures count (showed on item)
    charter->SetState(ITEM_CHANGED, _player);
    _player->SendNewItem(charter, 1, true, false);

    // a petition is invalid, if both the owner and the type matches
    // we checked above, if this player is in an arenateam, so this must be data corruption
    auto queryResult = CharacterDatabase.PQuery("SELECT petitionguid FROM petition WHERE ownerguid = '%u'", _player->GetGUIDLow());

    std::ostringstream ssInvalidPetitionGUIDs;

    if (queryResult)
    {
        do
        {
            Field* fields = queryResult->Fetch();
            ssInvalidPetitionGUIDs << "'" << fields[0].GetUInt32() << "' , ";
        }
        while (queryResult->NextRow());
    }

    // delete petitions with the same guid as this one
    ssInvalidPetitionGUIDs << "'" << charter->GetGUIDLow() << "'";

    DEBUG_LOG("Invalid petition GUIDs: %s", ssInvalidPetitionGUIDs.str().c_str());
    CharacterDatabase.escape_string(name);
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM petition WHERE petitionguid IN ( %s )",  ssInvalidPetitionGUIDs.str().c_str());
    CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid IN ( %s )", ssInvalidPetitionGUIDs.str().c_str());
    CharacterDatabase.PExecute("INSERT INTO petition (ownerguid, petitionguid, name) VALUES ('%u', '%u', '%s')",
                               _player->GetGUIDLow(), charter->GetGUIDLow(), name.c_str());
    CharacterDatabase.CommitTransaction();
}

void WorldSession::HandlePetitionShowSignOpcode(WorldPacket& recv_data)
{
    // ok
    DEBUG_LOG("Received opcode CMSG_PETITION_SHOW_SIGNATURES");
    // recv_data.hexlike();

    uint8 signs = 0;
    ObjectGuid petitionguid;
    recv_data >> petitionguid;                              // petition guid

    // solve (possible) some strange compile problems with explicit use GUID_LOPART(petitionguid) at some GCC versions (wrong code optimization in compiler?)
    uint32 petitionguid_low = petitionguid.GetCounter();

    // if guild petition and has guild => error, return;
    if (_player->GetGuildId())
        return;

    auto queryResult = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitionguid_low);

    // queryResult==nullptr also correct in case no sign yet
    if (queryResult)
        signs = (uint8)queryResult->GetRowCount();

    DEBUG_LOG("CMSG_PETITION_SHOW_SIGNATURES petition: %s", petitionguid.GetString().c_str());

    WorldPacket data(SMSG_PETITION_SHOW_SIGNATURES, (8 + 8 + 4 + 1 + signs * 12));
    data << ObjectGuid(petitionguid);                       // petition guid
    data << _player->GetObjectGuid();                       // owner guid
    data << uint32(petitionguid_low);                       // guild guid (in mangos always same as GUID_LOPART(petitionguid)
    data << uint8(signs);                                   // sign's count

    for (uint8 i = 1; i <= signs; ++i)
    {
        Field* fields2 = queryResult->Fetch();
        ObjectGuid signerGuid = ObjectGuid(HIGHGUID_PLAYER, fields2[0].GetUInt32());

        data << ObjectGuid(signerGuid);                     // Player GUID
        data << uint32(0);                                  // there 0 ...

        queryResult->NextRow();
    }
    SendPacket(data);
}

void WorldSession::HandlePetitionQueryOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_PETITION_QUERY");
    // recv_data.hexlike();

    uint32 guildguid;
    ObjectGuid petitionguid;
    recv_data >> guildguid;                                 // in mangos always same as GUID_LOPART(petitionguid)
    recv_data >> petitionguid;                              // petition guid
    DEBUG_LOG("CMSG_PETITION_QUERY Petition %s Guild GUID %u", petitionguid.GetString().c_str(), guildguid);

    SendPetitionQueryOpcode(petitionguid);
}

void WorldSession::SendPetitionQueryOpcode(ObjectGuid petitionguid) const
{
    uint32 petitionLowGuid = petitionguid.GetCounter();

    ObjectGuid ownerGuid;
    std::string name = "NO_NAME_FOR_GUID";
    uint8 signs = 0;

    auto queryResult = CharacterDatabase.PQuery(
                              "SELECT ownerguid, name, "
                              "  (SELECT COUNT(playerguid) FROM petition_sign WHERE petition_sign.petitionguid = '%u') AS signs "
                              "FROM petition WHERE petitionguid = '%u'", petitionLowGuid, petitionLowGuid);

    if (queryResult)
    {
        Field* fields = queryResult->Fetch();
        ownerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
        name      = fields[1].GetCppString();
        signs     = fields[2].GetUInt8();
    }
    else
    {
        DEBUG_LOG("CMSG_PETITION_QUERY failed for petition (GUID: %u)", petitionLowGuid);
        return;
    }

    WorldPacket data(SMSG_PETITION_QUERY_RESPONSE, (4 + 8 + name.size() + 1 + 2 + 4 * 11));
    data << uint32(petitionLowGuid);                        // guild/team guid (in mangos always same as GUID_LOPART(petition guid)
    data << ObjectGuid(ownerGuid);                          // charter owner guid
    data << name;                                           // name (guild/arena team)
    data << uint8(0);                                       // 1
    data << uint32(1);
    data << uint32(9);
    data << uint32(9);                                      // bypass client - side limitation, a different value is needed here for each petition
    data << uint32(0);                                      // 5
    data << uint32(0);                                      // 6
    data << uint32(0);                                      // 7
    data << uint32(0);                                      // 8
    data << uint16(0);                                      // 9 2 bytes field
    data << uint32(0);                                      // 10
    data << uint32(0);                                      // 11
    data << uint32(0);                                      // 13 count of next strings?
    data << uint32(0);                                      // 14
    SendPacket(data);
}

void WorldSession::HandlePetitionRenameOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode MSG_PETITION_RENAME");   // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    std::string newname;

    recv_data >> petitionGuid;                              // guid
    recv_data >> newname;                                   // new name

    Item* item = _player->GetItemByGuid(petitionGuid);
    if (!item)
        return;

    if (sGuildMgr.GetGuildByName(newname))
    {
        SendGuildCommandResult(GUILD_CREATE_S, newname, ERR_GUILD_NAME_EXISTS_S);
        return;
    }
    if (sObjectMgr.IsReservedName(newname) || !ObjectMgr::IsValidCharterName(newname))
    {
        SendGuildCommandResult(GUILD_CREATE_S, newname, ERR_GUILD_NAME_INVALID);
        return;
    }

    std::string db_newname = newname;
    CharacterDatabase.escape_string(db_newname);
    CharacterDatabase.PExecute("UPDATE petition SET name = '%s' WHERE petitionguid = '%u'",
                               db_newname.c_str(), petitionGuid.GetCounter());

    DEBUG_LOG("Petition %s renamed to '%s'", petitionGuid.GetString().c_str(), newname.c_str());

    WorldPacket data(MSG_PETITION_RENAME, (8 + newname.size() + 1));
    data << ObjectGuid(petitionGuid);
    data << newname;
    SendPacket(data);
}

void WorldSession::HandlePetitionSignOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_PETITION_SIGN");    // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    uint8 unk;
    recv_data >> petitionGuid;                              // petition guid
    recv_data >> unk;

    uint32 petitionLowGuid = petitionGuid.GetCounter();

    auto queryResult = CharacterDatabase.PQuery(
                              "SELECT ownerguid, "
                              "  (SELECT COUNT(playerguid) FROM petition_sign WHERE petition_sign.petitionguid = '%u') AS signs "
                              "FROM petition WHERE petitionguid = '%u'", petitionLowGuid, petitionLowGuid);

    if (!queryResult)
    {
        sLog.outError("any petition on server...");
        return;
    }

    Field* fields = queryResult->Fetch();
    uint32 ownerLowGuid = fields[0].GetUInt32();
    ObjectGuid ownerGuid = ObjectGuid(HIGHGUID_PLAYER, ownerLowGuid);
    uint8 signs = fields[1].GetUInt8();

    if (ownerGuid == _player->GetObjectGuid())
        return;

    // not let enemies sign guild charter
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) &&
            GetPlayer()->GetTeam() != sObjectMgr.GetPlayerTeamByGUID(ownerGuid))
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (_player->GetGuildId())
    {
        SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_IN_GUILD_S);
        return;
    }
    if (_player->GetGuildIdInvited())
    {
        SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_INVITED_TO_GUILD_S);
        return;
    }

    if (++signs > 9)                                        // client signs maximum
        return;

    // client doesn't allow to sign petition two times by one character, but not check sign by another character from same account
    // not allow sign another player from already sign player account
    queryResult = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE player_account = '%u' AND petitionguid = '%u'", GetAccountId(), petitionLowGuid);

    if (queryResult)
    {
        WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8 + 8 + 4));
        data << ObjectGuid(petitionGuid);
        data << ObjectGuid(_player->GetObjectGuid());
        data << uint32(PETITION_SIGN_ALREADY_SIGNED);

        // close at signer side
        SendPacket(data);

        // update for owner if online
        if (Player* owner = sObjectMgr.GetPlayer(ownerGuid))
            owner->GetSession()->SendPacket(data);
        return;
    }

    CharacterDatabase.PExecute("INSERT INTO petition_sign (ownerguid,petitionguid, playerguid, player_account) VALUES ('%u', '%u', '%u','%u')",
                               ownerLowGuid, petitionLowGuid, _player->GetGUIDLow(), GetAccountId());

    DEBUG_LOG("PETITION SIGN: %s by %s", petitionGuid.GetString().c_str(), _player->GetGuidStr().c_str());

    WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8 + 8 + 4));
    data << ObjectGuid(petitionGuid);
    data << ObjectGuid(_player->GetObjectGuid());
    data << uint32(PETITION_SIGN_OK);

    // close at signer side
    SendPacket(data);

    // update signs count on charter, required testing...
    // Item *item = _player->GetItemByGuid(petitionguid));
    // if(item)
    //    item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+1, signs);

    // update for owner if online
    if (Player* owner = sObjectMgr.GetPlayer(ownerGuid))
        owner->GetSession()->SendPacket(data);
}

void WorldSession::HandlePetitionDeclineOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode MSG_PETITION_DECLINE");  // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    recv_data >> petitionGuid;                              // petition guid

    DEBUG_LOG("Petition %s declined by %s", petitionGuid.GetString().c_str(), _player->GetGuidStr().c_str());

    uint32 petitionLowGuid = petitionGuid.GetCounter();

    auto queryResult = CharacterDatabase.PQuery("SELECT ownerguid FROM petition WHERE petitionguid = '%u'", petitionLowGuid);
    if (!queryResult)
        return;

    Field* fields = queryResult->Fetch();
    ObjectGuid ownerguid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());

    Player* owner = sObjectMgr.GetPlayer(ownerguid);
    if (owner)                                              // petition owner online
    {
        WorldPacket data(MSG_PETITION_DECLINE, 8);
        data << _player->GetObjectGuid();
        owner->GetSession()->SendPacket(data);
    }
}

void WorldSession::HandleOfferPetitionOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_OFFER_PETITION");   // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    ObjectGuid playerGuid;
    recv_data >> petitionGuid;                              // petition guid
    recv_data >> playerGuid;                                // player guid

    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player)
        return;

    DEBUG_LOG("OFFER PETITION: petition %s to %s", petitionGuid.GetString().c_str(), playerGuid.GetString().c_str());

    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && GetPlayer()->GetTeam() != player->GetTeam())
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (player->GetGuildId())
    {
        SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_IN_GUILD_S);
        return;
    }

    if (player->GetGuildIdInvited())
    {
        SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_INVITED_TO_GUILD_S);
        return;
    }

    /// Get petition signs count
    uint8 signs = 0;
    auto queryResult = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    // queryResult==nullptr also correct charter without signs
    if (queryResult)
        signs = (uint8)queryResult->GetRowCount();

    /// Send response
    WorldPacket data(SMSG_PETITION_SHOW_SIGNATURES, (8 + 8 + 4 + signs + signs * 12));
    data << ObjectGuid(petitionGuid);                       // petition guid
    data << ObjectGuid(_player->GetObjectGuid());           // owner guid
    data << uint32(petitionGuid.GetCounter());              // guild guid (in mangos always same as low part of petition guid)
    data << uint8(signs);                                   // sign's count

    for (uint8 i = 1; i <= signs; ++i)
    {
        Field* fields2 = queryResult->Fetch();
        ObjectGuid signerGuid = ObjectGuid(HIGHGUID_PLAYER, fields2[0].GetUInt32());

        data << ObjectGuid(signerGuid);                     // Player GUID
        data << uint32(0);                                  // there 0 ...

        queryResult->NextRow();
    }

    player->GetSession()->SendPacket(data);
}

void WorldSession::HandleTurnInPetitionOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_TURN_IN_PETITION"); // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;

    recv_data >> petitionGuid;

    DEBUG_LOG("Petition %s turned in by %s", petitionGuid.GetString().c_str(), _player->GetGuidStr().c_str());

    /// Collect petition info data
    ObjectGuid ownerGuid;
    std::string name;

    // data
    auto queryResult = CharacterDatabase.PQuery("SELECT ownerguid, name FROM petition WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    if (queryResult)
    {
        Field* fields = queryResult->Fetch();
        ownerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
        name = fields[1].GetCppString();
    }
    else
    {
        sLog.outError("CMSG_TURN_IN_PETITION: petition table not have data for guid %u!", petitionGuid.GetCounter());
        return;
    }

    if (_player->GetGuildId())
    {
        WorldPacket data(SMSG_TURN_IN_PETITION_RESULTS, 4);
        data << uint32(PETITION_TURN_ALREADY_IN_GUILD); // already in guild
        _player->GetSession()->SendPacket(data);
        return;
    }

    if (_player->GetObjectGuid() != ownerGuid)
        return;

    // signs
    queryResult = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    uint8 signs = queryResult ? (uint8)queryResult->GetRowCount() : 0;

    uint32 count = sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS);
    if (signs < count)
    {
        WorldPacket data(SMSG_TURN_IN_PETITION_RESULTS, 4);
        data << uint32(PETITION_TURN_NEED_MORE_SIGNATURES); // need more signatures...
        SendPacket(data);
        return;
    }

    if (sGuildMgr.GetGuildByName(name))
    {
        SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_EXISTS_S);
        return;
    }

    // and at last charter item check
    Item* item = _player->GetItemByGuid(petitionGuid);
    if (!item)
    {
        return;
    }

    // OK!

    // delete charter item
    _player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

    Guild* guild = new Guild;
    if (!guild->Create(_player, name))
    {
        delete guild;
        return;
    }

    // register guild and add guildmaster
    sGuildMgr.AddGuild(guild);

    // Send result to GM
    if (WorldSession* session = _player->GetSession())
        session->SendGuildCommandResult(GUILD_CREATE_S, name, 0);

    // add members
    for (uint8 i = 0; i < signs; ++i)
    {
        Field* fields = queryResult->Fetch();

        ObjectGuid signGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
        if (signGuid.IsEmpty())
            continue;

        guild->AddMember(signGuid, guild->GetLowestRank());

        // Put record into guild log
        guild->LogGuildEvent(GUILD_EVENT_LOG_JOIN_GUILD, signGuid);

        // Send result to online signees
        if (Player* signee = sObjectMgr.GetPlayer(signGuid))
        {
            if (WorldSession* session = signee->GetSession())
                session->SendGuildCommandResult(GUILD_FOUNDER_S, name, 0);
        }

        queryResult->NextRow();
    }

    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM petition WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    CharacterDatabase.CommitTransaction();

    // created
    DEBUG_LOG("TURN IN PETITION %s", petitionGuid.GetString().c_str());

    WorldPacket data(SMSG_TURN_IN_PETITION_RESULTS, 4);
    data << uint32(PETITION_TURN_OK);
    SendPacket(data);
}

void WorldSession::HandlePetitionShowListOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received CMSG_PETITION_SHOWLIST");
    // recv_data.hexlike();

    ObjectGuid guid;
    recv_data >> guid;

    SendPetitionShowList(guid);
}

void WorldSession::SendPetitionShowList(ObjectGuid guid) const
{
    Creature* pCreature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_PETITIONER);
    if (!pCreature)
    {
        DEBUG_LOG("WORLD: HandlePetitionShowListOpcode - %s not found or you can't interact with him.", guid.GetString().c_str());
        return;
    }

    uint8 count = 1;

    WorldPacket data(SMSG_PETITION_SHOWLIST, 8 + 1 + 4 * 6);
    data << ObjectGuid(guid);                               // npc guid
    data << count;                                          // count
    data << uint32(1);                                      // index
    data << uint32(GUILD_CHARTER);                          // charter entry
    data << uint32(CHARTER_DISPLAY_ID);                     // charter display id
    data << uint32(GUILD_CHARTER_COST);                     // charter cost
    data << uint32(1);                                      // unknown
    data << uint32(9);                                      // required signs?
    // for(uint8 i = 0; i < count; ++i)
    //{
    //    data << uint32(i);                        // index
    //    data << uint32(GUILD_CHARTER);            // charter entry
    //    data << uint32(CHARTER_DISPLAY_ID);       // charter display id
    //    data << uint32(GUILD_CHARTER_COST+i);     // charter cost
    //    data << uint32(0);                        // unknown
    //    data << uint32(9);                        // required signs?
    //}
    SendPacket(data);
    DEBUG_LOG("Sent SMSG_PETITION_SHOWLIST");
}
