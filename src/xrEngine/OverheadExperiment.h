#pragma once
#include <string>
namespace xray::overhead
{
std::string Control(const std::string& verb, const std::string& payload, bool& ok);
void Boundary();
void AwaitRunner(); // control iteration only, after its bridge response is queued
}
