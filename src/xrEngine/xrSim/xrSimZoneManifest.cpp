#include "xrSim/xrSimZoneManifest.h"

#include <charconv>
#include <set>
#include <sstream>

namespace xrSim
{
namespace
{
constexpr size_t MaxNameLength = 96;
constexpr size_t MaxAnchors = 64;
constexpr size_t MaxFactions = 64;
constexpr size_t MaxGroups = 32;
constexpr size_t MaxResources = 64;
constexpr size_t MaxRoutes = 64;
constexpr size_t MaxAgents = 32;
constexpr uint32_t MaxGroupCount = 8;

template <typename Integer>
bool ParseManifestInteger(const std::string& text, Integer& value)
{
    if (text.empty())
        return false;
    // from_chars is locale-independent, accepts no whitespace/plus and never throws.
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool ParseManifestInt32(const std::string& text, int32_t& value) { return ParseManifestInteger(text, value); }
bool ParseManifestUint32(const std::string& text, uint32_t& value) { return ParseManifestInteger(text, value); }

std::vector<std::string> SplitCsv(const std::string& line)
{
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : line)
    {
        if (ch == ',')
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current += ch;
        }
    }
    parts.push_back(current);
    return parts;
}

bool IsValidUtf8AndNoControl(const std::string& text)
{
    for (size_t i = 0; i < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80)
        {
            if (lead == '\r')
            {
                if (i == text.size() || text[i] != '\n') return false;
            }
            else if ((lead < 0x20 && lead != '\n') || lead == 0x7f)
                return false;
            continue;
        }
        uint32_t codepoint = 0;
        size_t remaining = 0;
        uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { remaining = 1; codepoint = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { remaining = 2; codepoint = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { remaining = 3; codepoint = lead & 0x07; minimum = 0x10000; }
        else return false;
        if (remaining > text.size() - i) return false;
        while (remaining--)
        {
            const unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint <= 0x9f)
            return false;
    }
    return true;
}

bool ValidField(const std::string& value, bool csv = true)
{
    return csv ? IsValidZoneName(value) : IsValidZoneText(value, MaxNameLength);
}

template <typename Record>
bool ValidNames(const std::vector<Record>& records)
{
    std::set<std::string> names;
    for (const auto& record : records)
        if (!ValidField(record.name) || !names.insert(record.name).second) return false;
    return true;
}

template <typename Record>
bool HasName(const std::vector<Record>& records, const std::string& name)
{
    for (const auto& record : records)
        if (record.name == name) return true;
    return false;
}

bool IsValidCharterKey(const std::string& key)
{
    return key == "zone_director_v1" || key == "stalker_faction_v1" || key == "bandit_faction_v1" ||
        key == "ecology_v1" || key == "economy_v1" || key == "region_v1" || key == "squad_v1" || key == "pack_v1";
}

bool IsValidScopeKind(const std::string& kind)
{
    return kind == "zone" || kind == "faction" || kind == "ecology" || kind == "economy" ||
        kind == "region" || kind == "squad" || kind == "mutant_pack";
}
} // namespace

bool IsValidZoneText(const std::string& value, size_t maxBytes, bool allowEmpty)
{
    return (allowEmpty || !value.empty()) && value.size() <= maxBytes &&
        value.find_first_of("\r\n") == std::string::npos && IsValidUtf8AndNoControl(value);
}

bool IsValidZoneName(const std::string& value)
{
    return IsValidZoneText(value, MaxNameLength) && value.find(',') == std::string::npos;
}

ZoneManifestValidationResult ValidateZoneManifest(const ZoneManifest& manifest)
{
    if (!ValidField(manifest.scenario, false) || !ValidField(manifest.saveName, false) ||
        !ValidField(manifest.levelName, false) || manifest.origin != "player")
        return {false, "invalid manifest metadata"};
    if (manifest.anchors.size() > MaxAnchors || manifest.factions.size() > MaxFactions ||
        manifest.groups.size() > MaxGroups || manifest.resources.size() > MaxResources ||
        manifest.routes.size() > MaxRoutes || manifest.agents.size() > MaxAgents)
        return {false, "manifest record limit exceeded"};
    if (!ValidNames(manifest.anchors) || !ValidNames(manifest.factions) || !ValidNames(manifest.groups) ||
        !ValidNames(manifest.resources) || !ValidNames(manifest.routes) || !ValidNames(manifest.agents))
        return {false, "duplicate or invalid record name"};

    std::set<uint32_t> ids;
    std::set<std::pair<std::string, uint32_t>> scopes;
    bool hasZone = false;
    bool hasEcology = false;
    for (const auto& agent : manifest.agents)
    {
        if (agent.agentId == 0 || agent.scopeId == 0 || !ids.insert(agent.agentId).second ||
            !scopes.insert({agent.scopeKind, agent.scopeId}).second)
            return {false, "duplicate or invalid agent ID/scope"};
        if (!IsValidScopeKind(agent.scopeKind) || !IsValidCharterKey(agent.charterKey))
            return {false, "unknown scope kind or charter"};
        const auto& kind = agent.scopeKind;
        const auto& charter = agent.charterKey;
        const bool compatible =
            (kind == "zone" && charter == "zone_director_v1") ||
            (kind == "faction" && (charter == "stalker_faction_v1" || charter == "bandit_faction_v1")) ||
            (kind == "ecology" && charter == "ecology_v1") ||
            (kind == "economy" && charter == "economy_v1") ||
            (kind == "region" && charter == "region_v1") ||
            (kind == "squad" && charter == "squad_v1") ||
            (kind == "mutant_pack" && charter == "pack_v1");
        if (!compatible) return {false, "charter does not match scope kind"};
        // Scope IDs are one-based manifest ordinals, never packed Handle values.
        if (kind == "faction")
        {
            if (agent.scopeId > manifest.factions.size() ||
                manifest.factions[agent.scopeId - 1].agentId != agent.agentId)
                return {false, "faction scope ownership mismatch"};
        }
        else if (kind == "squad" || kind == "mutant_pack")
        {
            if (agent.scopeId > manifest.groups.size()) return {false, "undefined group scope"};
            const auto& group = manifest.groups[agent.scopeId - 1];
            if (group.kind != kind || group.agentId != agent.agentId)
                return {false, "group scope ownership mismatch"};
        }
        else if (agent.scopeId != 1)
            return {false, "singleton scope ID must be one"};
        hasZone = hasZone || kind == "zone";
        hasEcology = hasEcology || kind == "ecology";
    }
    if (!hasZone) return {false, "missing Zone Director"};
    const auto owns = [&](uint32_t id, const std::string& kind, size_t ordinal)
    {
        for (const auto& agent : manifest.agents)
            if (agent.agentId == id && agent.scopeKind == kind && agent.scopeId == ordinal) return true;
        return false;
    };
    for (size_t i = 0; i < manifest.factions.size(); ++i)
        if (!owns(manifest.factions[i].agentId, "faction", i + 1))
            return {false, "missing faction owner"};
    for (size_t i = 0; i < manifest.groups.size(); ++i)
    {
        const auto& group = manifest.groups[i];
        if (!ValidField(group.section) || !ValidField(group.ownerDomain) || !ValidField(group.anchor) ||
            group.count == 0 || group.count > MaxGroupCount || !HasName(manifest.anchors, group.anchor))
            return {false, "invalid group fields or anchor"};
        if (group.kind == "squad")
        {
            if (!HasName(manifest.factions, group.ownerDomain)) return {false, "undefined squad faction"};
        }
        else if (group.kind == "mutant_pack")
        {
            if (group.ownerDomain != "ecology" || !hasEcology) return {false, "undefined pack ecology owner"};
        }
        else return {false, "unknown group kind"};
        if (!owns(group.agentId, group.kind, i + 1)) return {false, "missing group owner"};
    }
    for (const auto& resource : manifest.resources)
        if (!ValidField(resource.kind) || !ValidField(resource.anchor) ||
            !HasName(manifest.anchors, resource.anchor) || resource.minimum < 0 ||
            resource.quantity < resource.minimum || resource.quantity > 1000)
            return {false, "invalid resource fields, range or anchor"};
    for (const auto& route : manifest.routes)
        if (!ValidField(route.from) || !ValidField(route.to) || !HasName(manifest.anchors, route.from) ||
            !HasName(manifest.anchors, route.to) || route.from == route.to)
            return {false, "invalid route references"};
    return {true, ""};
}

ZoneManifestParseResult ParseZoneManifest(const std::string& text)
{
    ZoneManifestParseResult result;

    if (text.size() > 256 * 1024)
        return {false, "manifest byte limit exceeded", {}};
    if (!IsValidUtf8AndNoControl(text))
    {
        result.reason = "invalid UTF-8 or control bytes in manifest";
        return result;
    }

    std::istringstream stream(text);
    std::string line;

    bool sawHeader = false;
    bool sawScenario = false;
    bool sawSave = false;
    bool sawLevel = false;
    bool sawSeed = false;
    bool sawOrigin = false;

    std::set<std::string> anchorNames;
    std::set<std::string> factionNames;
    std::set<std::string> groupNames;
    std::set<std::string> resourceNames;
    std::set<std::string> routeNames;
    std::set<uint32_t> agentIds;
    std::set<std::pair<std::string, uint32_t>> agentScopes;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        if (!sawHeader)
        {
            if (line != "xrsim_zone_manifest_v1")
            {
                result.reason = "unsupported manifest header: " + line;
                return result;
            }
            sawHeader = true;
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            result.reason = "malformed manifest line: " + line;
            return result;
        }

        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "scenario")
        {
            if (sawScenario)
            {
                result.reason = "duplicate singleton: scenario";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid scenario name length";
                return result;
            }
            result.manifest.scenario = value;
            sawScenario = true;
        }
        else if (key == "save")
        {
            if (sawSave)
            {
                result.reason = "duplicate singleton: save";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid save name length";
                return result;
            }
            result.manifest.saveName = value;
            sawSave = true;
        }
        else if (key == "level")
        {
            if (sawLevel)
            {
                result.reason = "duplicate singleton: level";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid level name length";
                return result;
            }
            result.manifest.levelName = value;
            sawLevel = true;
        }
        else if (key == "seed")
        {
            if (sawSeed)
            {
                result.reason = "duplicate singleton: seed";
                return result;
            }
            if (!ParseManifestUint32(value, result.manifest.seed))
            {
                result.reason = "invalid seed value: " + value;
                return result;
            }
            sawSeed = true;
        }
        else if (key == "origin")
        {
            if (sawOrigin)
            {
                result.reason = "duplicate singleton: origin";
                return result;
            }
            if (value.empty() || value.size() > MaxNameLength)
            {
                result.reason = "invalid origin name length";
                return result;
            }
            result.manifest.origin = value;
            sawOrigin = true;
        }
        else if (key == "anchor")
        {
            if (result.manifest.anchors.size() >= MaxAnchors)
            {
                result.reason = "anchor limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 4)
            {
                result.reason = "invalid anchor format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !anchorNames.insert(name).second)
            {
                result.reason = "duplicate or invalid anchor name: " + name;
                return result;
            }
            ManifestAnchor anchor;
            anchor.name = name;
            if (!ParseManifestInt32(parts[1], anchor.xCm) || !ParseManifestInt32(parts[2], anchor.yCm) ||
                !ParseManifestInt32(parts[3], anchor.zCm))
            {
                result.reason = "invalid anchor coordinates: " + value;
                return result;
            }
            result.manifest.anchors.push_back(anchor);
        }
        else if (key == "faction")
        {
            if (result.manifest.factions.size() >= MaxFactions)
            {
                result.reason = "faction limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 2)
            {
                result.reason = "invalid faction format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !factionNames.insert(name).second)
            {
                result.reason = "duplicate or invalid faction name: " + name;
                return result;
            }
            ManifestFaction faction;
            faction.name = name;
            if (!ParseManifestUint32(parts[1], faction.agentId) || faction.agentId == 0)
            {
                result.reason = "invalid faction agent id: " + parts[1];
                return result;
            }
            result.manifest.factions.push_back(faction);
        }
        else if (key == "group")
        {
            if (result.manifest.groups.size() >= MaxGroups)
            {
                result.reason = "group limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 7)
            {
                result.reason = "invalid group format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !groupNames.insert(name).second)
            {
                result.reason = "duplicate or invalid group name: " + name;
                return result;
            }
            ManifestGroup group;
            group.name = name;
            group.kind = parts[1];
            group.ownerDomain = parts[2];
            group.section = parts[3];
            group.anchor = parts[5];
            if (group.kind != "squad" && group.kind != "mutant_pack")
            {
                result.reason = "unknown group kind: " + group.kind;
                return result;
            }
            if (group.section.empty() || group.section.size() > MaxNameLength)
            {
                result.reason = "invalid group section: " + group.section;
                return result;
            }
            if (!ParseManifestUint32(parts[4], group.count) || group.count == 0 || group.count > MaxGroupCount)
            {
                result.reason = "invalid group count: " + parts[4];
                return result;
            }
            if (!ParseManifestUint32(parts[6], group.agentId) || group.agentId == 0)
            {
                result.reason = "invalid group agent id: " + parts[6];
                return result;
            }
            result.manifest.groups.push_back(group);
        }
        else if (key == "resource")
        {
            if (result.manifest.resources.size() >= MaxResources)
            {
                result.reason = "resource limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 5)
            {
                result.reason = "invalid resource format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !resourceNames.insert(name).second)
            {
                result.reason = "duplicate or invalid resource name: " + name;
                return result;
            }
            ManifestResource resource;
            resource.name = name;
            resource.kind = parts[1];
            resource.anchor = parts[2];
            if (!ParseManifestInt32(parts[3], resource.quantity) || !ParseManifestInt32(parts[4], resource.minimum))
            {
                result.reason = "invalid resource quantity/minimum: " + value;
                return result;
            }
            result.manifest.resources.push_back(resource);
        }
        else if (key == "route")
        {
            if (result.manifest.routes.size() >= MaxRoutes)
            {
                result.reason = "route limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 3)
            {
                result.reason = "invalid route format: " + value;
                return result;
            }
            const std::string& name = parts[0];
            if (name.empty() || name.size() > MaxNameLength || !routeNames.insert(name).second)
            {
                result.reason = "duplicate or invalid route name: " + name;
                return result;
            }
            ManifestRoute route;
            route.name = name;
            route.from = parts[1];
            route.to = parts[2];
            result.manifest.routes.push_back(route);
        }
        else if (key == "agent")
        {
            if (result.manifest.agents.size() >= MaxAgents)
            {
                result.reason = "agent limit exceeded";
                return result;
            }
            const std::vector<std::string> parts = SplitCsv(value);
            if (parts.size() != 5)
            {
                result.reason = "invalid agent format: " + value;
                return result;
            }
            ManifestAgent agent;
            if (!ParseManifestUint32(parts[0], agent.agentId) || agent.agentId == 0)
            {
                result.reason = "invalid agent id: " + parts[0];
                return result;
            }
            if (!agentIds.insert(agent.agentId).second)
            {
                result.reason = "duplicate agent id: " + parts[0];
                return result;
            }
            agent.scopeKind = parts[1];
            if (!IsValidScopeKind(agent.scopeKind))
            {
                result.reason = "unknown scope kind: " + agent.scopeKind;
                return result;
            }
            if (!ParseManifestUint32(parts[2], agent.scopeId))
            {
                result.reason = "invalid scope id: " + parts[2];
                return result;
            }
            if (!agentScopes.insert({ agent.scopeKind, agent.scopeId }).second)
            {
                result.reason = "duplicate agent scope: " + agent.scopeKind + "," + parts[2];
                return result;
            }
            agent.name = parts[3];
            agent.charterKey = parts[4];
            if (!IsValidCharterKey(agent.charterKey))
            {
                result.reason = "unknown charter key: " + agent.charterKey;
                return result;
            }
            result.manifest.agents.push_back(agent);
        }
        else
        {
            result.reason = "unknown key: " + key;
            return result;
        }
    }

    if (!sawHeader)
    {
        result.reason = "missing manifest header";
        return result;
    }
    if (!sawScenario || !sawSave || !sawLevel || !sawSeed || !sawOrigin)
    {
        result.reason = "missing required singleton fields";
        return result;
    }

    const auto validation = ValidateZoneManifest(result.manifest);
    if (!validation.ok)
    {
        result.reason = validation.reason;
        return result;
    }

    result.ok = true;
    return result;
}
} // namespace xrSim
