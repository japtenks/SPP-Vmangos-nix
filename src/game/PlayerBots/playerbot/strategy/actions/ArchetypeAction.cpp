#include "playerbot/playerbot.h"
#include "ArchetypeAction.h"

using namespace ai;

bool ArchetypeAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    if (!requester)
        requester = bot;

    const ArchetypeWeights& weights = ai->GetArchetypeWeights();
    const BotSession& session = ai->GetSession();

    std::ostringstream out;
    out << "Archetype: " << BotArchetypeToString(ai->GetArchetype())
        << ", Session: " << SessionStateToString(session.state)
        << ", Quest[w p/c/z/l]="
        << weights.progressWeight << "/" << weights.chainWeight << "/" << weights.zoneWeight << "/" << weights.levelWeight
        << ", Flee=" << uint32(weights.fleeHealthThreshold * 100.0f) << "%"
        << ", Hazard=" << weights.hazardSeverityThreshold
        << ", SessionMinMax=" << weights.minSessionMinutes << "-" << weights.maxSessionMinutes
        << ", Days/Wk=" << weights.daysPerWeek
        << ", Group[j/l]=" << weights.groupJoinChance << "/" << weights.groupLeaveChance;

    ai->TellPlayer(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    return true;
}
