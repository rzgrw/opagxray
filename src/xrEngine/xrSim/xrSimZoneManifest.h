#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xrSim
{
struct ManifestAnchor
{
    std::string name;
    int32_t xCm = 0;
    int32_t yCm = 0;
    int32_t zCm = 0;
};

struct ManifestFaction
{
    std::string name;
    uint32_t agentId = 0;
};

struct ManifestGroup
{
    std::string name;
    std::string kind;
    std::string ownerDomain;
    std::string section;
    uint32_t count = 0;
    std::string anchor;
    uint32_t agentId = 0;
};

struct ManifestResource
{
    std::string name;
    std::string kind;
    std::string anchor;
    int32_t quantity = 0;
    int32_t minimum = 0;
};

struct ManifestRoute
{
    std::string name;
    std::string from;
    std::string to;
};

struct ManifestAgent
{
    uint32_t agentId = 0;
    std::string scopeKind;
    uint32_t scopeId = 0;
    std::string name;
    std::string charterKey;
};

struct ZoneManifest
{
    std::string scenario;
    std::string saveName;
    std::string levelName;
    uint32_t seed = 0;
    std::string origin;
    std::vector<ManifestAnchor> anchors;
    std::vector<ManifestFaction> factions;
    std::vector<ManifestGroup> groups;
    std::vector<ManifestResource> resources;
    std::vector<ManifestRoute> routes;
    std::vector<ManifestAgent> agents;
};

struct ZoneManifestParseResult
{
    bool ok = false;
    std::string reason;
    ZoneManifest manifest;
};

struct ZoneManifestValidationResult
{
    bool ok = false;
    std::string reason;
};

// Shared strict field validation for manifests and dynamic canonical mutations.
// Lengths are byte limits; text excludes controls, names additionally exclude CSV commas.
bool IsValidZoneText(const std::string& value, size_t maxBytes, bool allowEmpty = false);
bool IsValidZoneName(const std::string& value);

// Shared by the text parser and Bootstrap: callers may construct manifests directly.
ZoneManifestValidationResult ValidateZoneManifest(const ZoneManifest& manifest);
ZoneManifestParseResult ParseZoneManifest(const std::string& text);
} // namespace xrSim
