
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotDbStore.h"
#include <cstdlib>
#include <iostream>
#include <unordered_map>

#include "LootObjectStack.h"
#include "strategy/values/Formations.h"
#include "strategy/values/PositionValue.h"
INSTANTIATE_SINGLETON_1(PlayerbotDbStore);

using namespace ai;

namespace
{
    bool IsControlFrameworkAliasKey(const std::string& key)
    {
        return key == "authority_mode" ||
               key == "combat_profile" ||
               key == "movement_profile" ||
               key == "route_profile" ||
               key == "reaction_profile" ||
               key == "rtsc_overlay_active" ||
               key == "rtsc_overlay_label" ||
               key == "rtsc_overlay_anchor";
    }

    bool IsLegacyMovementKey(const std::string& key)
    {
        return key == "follow" ||
               key == "guard" ||
               key == "free" ||
               key == "wander" ||
               key == "rtsc";
    }
}

void PlayerbotDbStore::Load(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();

    auto results = CharacterDatabase.PQuery("SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
    if (results)
    {
        ai->ClearStrategies(BotState::BOT_STATE_COMBAT);
        ai->ClearStrategies(BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+chat", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("+chat", BotState::BOT_STATE_NON_COMBAT);

        std::list<std::string> values;
        std::unordered_map<std::string, std::string> frameworkValues;
        do
        {
            Field* fields = results->Fetch();
            std::string key = fields[0].GetString();
            std::string value = fields[1].GetString();
            if (key == "value") values.push_back(value);
            else if (key == "co") ai->ChangeStrategy(value, BotState::BOT_STATE_COMBAT);
            else if (key == "nc") ai->ChangeStrategy(value, BotState::BOT_STATE_NON_COMBAT);
            else if (key == "dead") ai->ChangeStrategy(value, BotState::BOT_STATE_DEAD);
            else if (key == "react") ai->ChangeStrategy(value, BotState::BOT_STATE_REACTION);
            else if (IsLegacyMovementKey(key)) ai->ChangeStrategy(value, BotState::BOT_STATE_NON_COMBAT);
            else if (key.find("framework.") == 0 || IsControlFrameworkAliasKey(key)) frameworkValues[key] = value;
        } while (results->NextRow());

        ai->GetAiObjectContext()->Load(values);
        ai->LoadFrameworkState(frameworkValues);
    }
}

void PlayerbotDbStore::Save(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();

    Reset(ai, preset);
    ai->SyncQuestLogState();

    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    for (std::list<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        SaveValue(guid, preset, "value", *i);
    }

    for (const auto& [key, value] : ai->SaveFrameworkState())
    {
        SaveValue(guid, preset, key, value);
        if (key == "framework.authority_mode")
            SaveValue(guid, preset, "authority_mode", value);
        else if (key == "framework.combat_profile")
            SaveValue(guid, preset, "combat_profile", value);
        else if (key == "framework.movement_profile")
            SaveValue(guid, preset, "movement_profile", value);
        else if (key == "framework.route_profile")
            SaveValue(guid, preset, "route_profile", value);
        else if (key == "framework.reaction_profile")
            SaveValue(guid, preset, "reaction_profile", value);
        else if (key == "framework.rtsc_overlay_active")
            SaveValue(guid, preset, "rtsc_overlay_active", value);
        else if (key == "framework.rtsc_overlay_label")
            SaveValue(guid, preset, "rtsc_overlay_label", value);
        else if (key == "framework.rtsc_overlay_anchor")
            SaveValue(guid, preset, "rtsc_overlay_anchor", value);
    }

    std::string combatStrategies = FormatStrategies("co", ai->GetStrategies(BotState::BOT_STATE_COMBAT));
    if (!combatStrategies.empty())
        SaveValue(guid, preset, "co", combatStrategies);

    std::string nonCombatStrategies = FormatStrategies("nc", ai->GetStrategies(BotState::BOT_STATE_NON_COMBAT));
    if (!nonCombatStrategies.empty())
        SaveValue(guid, preset, "nc", nonCombatStrategies);

    std::string deadStrategies = FormatStrategies("dead", ai->GetStrategies(BotState::BOT_STATE_DEAD));
    if (!deadStrategies.empty())
        SaveValue(guid, preset, "dead", deadStrategies);

    std::string reactStrategies = FormatStrategies("react", ai->GetStrategies(BotState::BOT_STATE_REACTION));
    if (!reactStrategies.empty())
        SaveValue(guid, preset, "react", reactStrategies);
}

std::string PlayerbotDbStore::FormatStrategies(std::string type, std::list<std::string> strategies)
{
    if (strategies.empty())
        return "";

    std::ostringstream out;
    for(const auto& strategy : strategies)
        out << "+" << strategy << ",";

    std::string res = out.str();
    return res.substr(0, res.size() - 1);
}

void PlayerbotDbStore::Reset(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();
    uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(guid));

    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
}

void PlayerbotDbStore::SaveValue(uint64 guid, std::string preset, std::string key, std::string value)
{
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES ('%lu', '%s', '%s', '%s')", guid, preset.c_str(), key.c_str(), value.c_str());
}
