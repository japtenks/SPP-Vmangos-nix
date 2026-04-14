
#include "playerbot/playerbot.h"
#include "GuildAcceptAction.h"
#include "playerbot/ServerSocialMgr.h"
#include "playerbot/ServerFacade.h"
#include "GuildMgr.h"

using namespace ai;

namespace
{
    constexpr char kDecisionTracePrefix[] = "[PBTRACE]";

    void TellGuildInviteTrace(PlayerbotAI* ai, Player* requester, const std::string& text)
    {
        if (!ai)
            return;

        ai->TellDebug(requester ? requester : ai->GetMaster(), std::string(kDecisionTracePrefix) + " " + text, "debug travel");
    }
}

bool GuildAcceptAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    WorldPacket p(event.getPacket());
    p.rpos(0);
    Player* inviter = nullptr;
    std::string Invitedname;
    p >> Invitedname;

    if (normalizePlayerName(Invitedname))
        inviter = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    if (!inviter)
        return false;

    std::map<std::string, std::string> placeholders;
    placeholders["%name"] = inviter->GetName();
    std::string inviteContext = std::string("inviter=") + inviter->GetName() +
        " guild_id=" + std::to_string(inviter->GetGuildId()) +
        " archetype=" + BotArchetypeToString(ai->GetArchetype());

    bool accept = true;
    uint32 guildId = inviter->GetGuildId();
    if (!guildId)
    {
        TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=inviter_no_guild " + inviteContext);
        ai->TellDebug(requester, "Declining guild invite because inviter has no guild.", "debug travel");
        ai->TellError(requester, "You are not in a guild!");

        if(sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && inviter->GetPlayerbotAI())
            bot->Say(BOT_TEXT2("You are not in a guild %name!", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }
    else if (bot->GetGuildId())
    {
        TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=already_in_guild " + inviteContext);
        ai->TellDebug(requester, "Declining guild invite because bot is already guilded.", "debug travel");
        ai->TellError(requester, "Sorry, I am in a guild already");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && inviter->GetPlayerbotAI())
            bot->Say(BOT_TEXT2("Sorry, I am in a guild already %name.", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }
    else if (!ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_GUILD, false, inviter, true))
    {
        TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=security_check " + inviteContext);
        ai->TellDebug(requester, "Declining guild invite because security policy rejected inviter.", "debug travel");
        ai->TellError(requester, "Sorry, I don't want to join your guild :(");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && inviter->GetPlayerbotAI())
            bot->Say(BOT_TEXT2("Sorry, I don't want to join your guild %name :(.", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }

    Guild* guild = sGuildMgr.GetGuildById(guildId);
    const float guildJoinBias = guild ? sServerSocialMgr.GetGuildJoinBias(bot, guild, inviter->GetObjectGuid().GetRawValue()) : 0.0f;

    if(guild && guild->GetMemberSize() > 1000)
    {
        TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=oversized social_bias=" + std::to_string(guildJoinBias) + " " + inviteContext);
        ai->TellDebug(requester, std::string("Declining guild invite from ") + guild->GetName() + " because guild is oversized.", "debug travel");
        ai->TellError(requester, "This guild has over 1000 members. To stop it from reaching the 1064 member limit I refuse to join it.");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && inviter->GetPlayerbotAI())
            bot->Say(BOT_TEXT2("%name, your guild has over 1000 members. To stop it from reaching the 1064 member limit I refuse to join it.", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }
    else if (guild && guildJoinBias < -0.25f)
    {
        TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=poor_fit social_bias=" + std::to_string(guildJoinBias) + " " + inviteContext);
        ai->TellDebug(requester, std::string("Declining guild invite from ") + guild->GetName() + " due to poor fit bias " + std::to_string(guildJoinBias), "debug travel");
        ai->TellError(requester, "This guild does not feel like a good fit right now.");

        if (sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && inviter->GetPlayerbotAI())
            bot->Say(BOT_TEXT2("I don't think your guild is a good fit for me right now %name.", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));

        accept = false;
    }
    else if (guild && guildJoinBias < 0.10f && urand(0, 99) < 45)
    {
        TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=weak_social_bias social_bias=" + std::to_string(guildJoinBias) + " " + inviteContext);
        ai->TellDebug(requester, std::string("Declining guild invite from ") + guild->GetName() + " due to weak social bias " + std::to_string(guildJoinBias), "debug travel");
        accept = false;
    }

    if (accept && sPlayerbotAIConfig.inviteChat && sServerFacade.GetDistance2d(bot, inviter) < sPlayerbotAIConfig.spellDistance * 1.5 && inviter->GetPlayerbotAI() && (sRandomPlayerbotMgr.IsFreeBot(bot) || !ai->HasActivePlayerMaster()))
    {
        if (urand(0, 3))
            bot->Say(BOT_TEXT2("Sounds good %name sign me up!", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
        else
            bot->Say(BOT_TEXT2("I would love to join!", placeholders).c_str(), (bot->GetTeam() == ALLIANCE ? LANG_COMMON : LANG_ORCISH));
    }

    WorldPacket packet;
    if (accept)
    {
        bot->GetSession()->HandleGuildAcceptOpcode(MakeNullPacket(packet));

        sServerSocialMgr.AddAffinity(bot->GetObjectGuid().GetRawValue(), inviter->GetObjectGuid().GetRawValue(), 0.12f,
            SOCIAL_RELATIONSHIP_KNOWN | SOCIAL_RELATIONSHIP_GUILD_FRIENDLY, "guild_accept_inviter");
        sServerSocialMgr.AddAffinity(inviter->GetObjectGuid().GetRawValue(), bot->GetObjectGuid().GetRawValue(), 0.06f,
            SOCIAL_RELATIONSHIP_KNOWN | SOCIAL_RELATIONSHIP_GUILD_FRIENDLY, "guild_accept_invitee");
        sServerSocialMgr.ObserveGuildArea(bot, sServerSocialMgr.NormalizeAreaId(bot), 1.8f);
        TellGuildInviteTrace(ai, requester, "guild_invite result=accept social_bias=" + std::to_string(guildJoinBias) + " " + inviteContext);
        ai->TellDebug(requester, std::string("Accepted guild invite to ") + guild->GetName() + " with social bias " + std::to_string(guildJoinBias), "debug travel");

        TalentSpec::SetPublicNote(bot);

        sPlayerbotAIConfig.logEvent(ai, "GuildAcceptAction", guild->GetName(), std::to_string(guild->GetMemberSize()));
    }
    else
    {
        bot->GetSession()->HandleGuildDeclineOpcode(MakeNullPacket(packet));
        if (guild)
        {
            TellGuildInviteTrace(ai, requester, "guild_invite result=decline reason=final_decline social_bias=" + std::to_string(guildJoinBias) + " " + inviteContext);
            ai->TellDebug(requester, std::string("Declined guild invite to ") + guild->GetName() + " with social bias " + std::to_string(guildJoinBias), "debug travel");
        }
    }
    return true;
}
