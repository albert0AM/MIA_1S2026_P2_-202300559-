#pragma once
#include <string>
#include <map>
#include <ctime>

std::string trim(const std::string& s);
std::string toLower(const std::string& s);
std::map<std::string, std::string> parseCommand(const std::string& line);
bool mkdirRecursive(const std::string& path);
bool fileExists(const std::string& path);
std::string formatTime(time_t t);
std::string expandPath(const std::string& path);