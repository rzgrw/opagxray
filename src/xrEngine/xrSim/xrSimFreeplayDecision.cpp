#include "xrSim/xrSimFreeplayDecision.h"

#include <utility>

namespace xrSim
{
namespace
{
const char* kObservationHeader = "xrsim_freeplay_observation_v1";
constexpr size_t kMaxSites = 8;
constexpr size_t kMaxSightings = 6;
constexpr size_t kMaxLedgerEntries = 512;

FreeplayObservationParse FailObservation(const char* reason)
{
    FreeplayObservationParse result;
    result.reason = reason;
    return result;
}

FreeplayVerdict FailVerdict(const char* reason)
{
    FreeplayVerdict verdict;
    verdict.reason = reason;
    return verdict;
}

bool IsSeparator(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Same record separators as the freeplay snapshot: newline or ';' ends a record.
std::vector<std::vector<std::string>> SplitRecords(const std::string& text)
{
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> tokens;
    std::string token;
    for (size_t i = 0; i <= text.size(); ++i)
    {
        const char c = i < text.size() ? text[i] : '\n';
        if (c == '\n' || c == ';' || IsSeparator(c))
        {
            if (!token.empty())
                tokens.push_back(token);
            token.clear();
            if ((c == '\n' || c == ';') && !tokens.empty())
            {
                records.push_back(tokens);
                tokens.clear();
            }
            continue;
        }
        token += c;
    }
    return records;
}

bool ParseUnsigned(const std::string& text, uint64_t maxValue, uint64_t& value)
{
    if (text.empty() || text.size() > 20)
        return false;
    uint64_t parsed = 0;
    for (const char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (parsed > (maxValue - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

bool ParseU32(const std::string& text, uint32_t& value)
{
    uint64_t parsed = 0;
    if (!ParseUnsigned(text, 0xffffffffull, parsed))
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool IsMovementVerb(const std::string& verb) { return verb == "stay" || verb == "move_to" || verb == "retreat"; }

bool IsFreeplayTool(const std::string& verb) { return IsMovementVerb(verb) || verb == "author_memory"; }

bool IsObservedSite(const FreeplayObservation& observation, const std::string& name)
{
    for (const FreeplaySite& site : observation.sites)
    {
        if (site.name == name)
            return true;
    }
    return false;
}

// Ledger values stay single tokens so every entry is one line of key=value fields.
std::string LedgerValue(const std::string& value)
{
    if (value.empty())
        return "-";
    std::string out = value;
    for (char& c : out)
    {
        if (static_cast<unsigned char>(c) <= ' ' || c == ';' || c == 0x7f)
            c = '_';
    }
    return out;
}
} // namespace

FreeplayObservationParse ParseFreeplayObservation(const FreeplayPopulation& population, const std::string& text)
{
    const std::vector<std::vector<std::string>> records = SplitRecords(text);
    if (records.empty() || records[0].size() != 1 || records[0][0] != kObservationHeader)
        return FailObservation("malformed");

    FreeplayObservation observation;
    bool sawOwner = false, sawSituation = false, ended = false;
    for (size_t i = 1; i < records.size(); ++i)
    {
        const std::vector<std::string>& record = records[i];
        const std::string& type = record[0];
        if (ended)
            return FailObservation("malformed");
        if (type == "end")
        {
            if (record.size() != 1)
                return FailObservation("malformed");
            ended = true;
        }
        else if (type == "owner")
        {
            if (sawOwner || record.size() != 3 || !ParseUnsigned(record[1], ~0ull, observation.ownerId) ||
                !ParseU32(record[2], observation.membershipRevision))
                return FailObservation("malformed");
            sawOwner = true;
        }
        else if (type == "situation")
        {
            if (sawSituation || record.size() != 2)
                return FailObservation("malformed");
            observation.situation = record[1];
            sawSituation = true;
        }
        else if (type == "site")
        {
            FreeplaySite site;
            if (record.size() != 5 || !ParseU32(record[2], site.distanceMeters) || !ParseU32(record[4], site.occupants))
                return FailObservation("malformed");
            site.name = record[1];
            site.kind = record[3];
            observation.sites.push_back(site);
        }
        else if (type == "sighting")
        {
            FreeplaySighting sighting;
            if (record.size() != 7 || !ParseU32(record[4], sighting.count) ||
                !ParseU32(record[5], sighting.distanceMeters))
                return FailObservation("malformed");
            if (record[2] != "stalker" && record[2] != "mutant")
                return FailObservation("malformed");
            if (record[6] != "enemy" && record[6] != "neutral" && record[6] != "friend")
                return FailObservation("malformed");
            sighting.name = record[1];
            sighting.species = record[2];
            sighting.faction = record[3];
            sighting.relation = record[6];
            observation.sightings.push_back(sighting);
        }
        else
            return FailObservation("malformed");
    }
    if (!ended || !sawOwner || !sawSituation)
        return FailObservation("malformed");

    const FreeplayOwner* owner = population.FindOwner(observation.ownerId);
    if (owner == nullptr)
        return FailObservation("unknown_owner");
    if (owner->membershipRevision != observation.membershipRevision)
        return FailObservation("stale_membership");
    if (observation.sites.size() > kMaxSites)
        return FailObservation("too_many_sites");
    if (observation.sightings.size() > kMaxSightings)
        return FailObservation("too_many_sightings");

    observation.epoch = population.Epoch();
    FreeplayObservationParse result;
    result.ok = true;
    result.observation = std::move(observation);
    return result;
}

ActorWakeContext BuildFreeplayWake(const FreeplayOwner& owner, const FreeplayObservation& observation,
    const std::string& wakeReasons)
{
    ActorWakeContext context;
    context.actor.agentId = static_cast<uint32_t>(owner.ownerId & 0xffffffffull);
    context.actor.scope =
        owner.species == FreeplaySpecies::Mutant ? ActorAgentScope::MutantPack : ActorAgentScope::Squad;
    context.actor.name = owner.name;

    std::string seen = "situation " + observation.situation + "\n";
    for (const FreeplaySite& site : observation.sites)
        seen += "site " + site.name + " " + std::to_string(site.distanceMeters) + " " + site.kind + " " +
            std::to_string(site.occupants) + "\n";
    for (const FreeplaySighting& sighting : observation.sightings)
        seen += "sighting " + sighting.name + " " + sighting.species + " " + sighting.faction + " " +
            std::to_string(sighting.count) + " " + std::to_string(sighting.distanceMeters) + " " +
            sighting.relation + "\n";
    context.observation = seen;

    std::string out = "xrsim_freeplay_wake_v1\n";
    out += "owner_id " + std::to_string(owner.ownerId) + "\n";
    out += "name " + owner.name + "\n";
    out += std::string("kind ") + FreeplayKindName(owner.kind) + "\n";
    out += "faction " + owner.faction + "\n";
    out += "level " + owner.level + "\n";
    out += "members " + std::to_string(owner.members.size()) + "\n";
    out += "current_site " + owner.site + "\n";
    out += "objective " + owner.objective.verb + " " + owner.objective.site + " source " +
        FreeplaySourceName(owner.objective.source) + "\n";
    for (const std::string& memory : owner.memories)
        out += "memory " + memory + "\n";
    out += "wake_reasons " + (wakeReasons.empty() ? std::string("-") : wakeReasons) + "\n";
    out += seen;
    out += "rules\n";
    out += "return xrsim_actor_intent_v1\n";
    out += "response_first_line xrsim_actor_intent_v1\n";
    out += "response_record goal <single_token>\n";
    out += "response_record stance <single_token>\n";
    out += "response_record duration_ms <uint32>\n";
    out += "legal_tools stay move_to retreat author_memory\n";
    out += "response_record action stay\n";
    out += "response_record action move_to <site_name>\n";
    out += "response_record action retreat <site_name>\n";
    out += "site_name_must_be_listed\n";
    out += "response_record memory <single_token>\n";
    out += "response_alternative coast\n";
    out += "response_last_line end\n";
    out += "single_token_values_use_underscores\n";
    out += "output_schema_lines_only no_prose no_markdown no_code_fence\n";
    out += "do_not_repeat_observation_fields owner_id name kind faction level members current_site objective "
           "memory wake_reasons situation site sighting\n";
    out += "llm_owns_decisions cxx_owns_embodiment\n";
    out += "do_not_emit_unlisted_tools\n";
    out += "end\n";
    context.prompt = out;
    return context;
}

FreeplayVerdict ValidateFreeplayDecision(const FreeplayPopulation& population,
    const FreeplayObservation& observation, const ActorIntentPlan& plan)
{
    const FreeplayOwner* owner = population.FindOwner(observation.ownerId);
    if (owner == nullptr)
        return FailVerdict("unknown_owner");
    if (observation.epoch != population.Epoch())
        return FailVerdict("stale_world");
    if (observation.membershipRevision != owner->membershipRevision)
        return FailVerdict("stale_membership");
    if (plan.coast)
    {
        FreeplayVerdict verdict;
        verdict.ok = true;
        verdict.keep = true;
        return verdict;
    }

    const ActorAction* objective = nullptr;
    size_t movements = 0;
    for (const ActorAction& action : plan.actions)
    {
        if (!IsFreeplayTool(action.verb))
            return FailVerdict("illegal_tool");
        if (IsMovementVerb(action.verb))
        {
            ++movements;
            objective = &action;
        }
    }
    if (movements != 1)
        return FailVerdict("need_one_objective");
    if (!IsFreeplayTransferable(*owner) && objective->verb != "stay")
        return FailVerdict("constrained");
    if (objective->verb == "stay" && owner->site == "-")
        return FailVerdict("no_site");
    if (objective->verb != "stay" && !IsObservedSite(observation, objective->target))
        return FailVerdict("unknown_site");

    FreeplayVerdict verdict;
    verdict.ok = true;
    verdict.verb = objective->verb;
    verdict.site = objective->verb == "stay" ? owner->site : objective->target;
    verdict.memories = plan.memories;
    return verdict;
}

void FreeplayLedger::Append(FreeplayLedgerEntry entry)
{
    entry.serial = m_nextSerial++;
    m_entries.push_back(std::move(entry));
    while (m_entries.size() > kMaxLedgerEntries)
        m_entries.pop_front();
}

std::string FreeplayLedger::Format(size_t maxEntries) const
{
    std::string out;
    const size_t count = maxEntries < m_entries.size() ? maxEntries : m_entries.size();
    for (size_t i = m_entries.size() - count; i < m_entries.size(); ++i)
    {
        const FreeplayLedgerEntry& entry = m_entries[i];
        out += "serial=" + std::to_string(entry.serial) + " ownerId=" + std::to_string(entry.ownerId) +
            " epoch=" + std::to_string(entry.epoch) +
            " membershipRevision=" + std::to_string(entry.membershipRevision) +
            " wakeReason=" + LedgerValue(entry.wakeReason) + " source=" + LedgerValue(entry.source) +
            " verdict=" + LedgerValue(entry.verdict) + " verb=" + LedgerValue(entry.verb) +
            " site=" + LedgerValue(entry.site) + " provider=" + LedgerValue(entry.provider) +
            " model=" + LedgerValue(entry.model) + " queueAgeMs=" + std::to_string(entry.queueAgeMs) +
            " latencyMs=" + std::to_string(entry.latencyMs) + "\n";
    }
    return out;
}

size_t FreeplayLedger::Size() const { return m_entries.size(); }
} // namespace xrSim
