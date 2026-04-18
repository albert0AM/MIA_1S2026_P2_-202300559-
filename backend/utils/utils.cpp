#include "utils.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <vector>
#include <cstdlib>

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string expandPath(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

std::map<std::string, std::string> parseCommand(const std::string& line) {
    std::map<std::string, std::string> params;
    std::vector<std::string> tokens;

    std::string token;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"')                   { inQuotes = !inQuotes; }
        else if (c == ' ' && !inQuotes) { if (!token.empty()) { tokens.push_back(token); token.clear(); } }
        else                            { token += c; }
    }
    if (!token.empty()) tokens.push_back(token);
    if (tokens.empty()) return params;

    params["cmd"] = toLower(tokens[0]);

    for (size_t i = 1; i < tokens.size(); i++) {
        std::string t = tokens[i];
        if (t.empty() || t[0] != '-') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos)
            params[toLower(t.substr(1))] = "";
        else
            params[toLower(t.substr(1, eq-1))] = t.substr(eq+1);
    }
    return params;
}

bool mkdirRecursive(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if ((path[i] == '/' || i == path.size()-1) && cur != "/") {
            struct stat st;
            if (stat(cur.c_str(), &st) != 0)
                if (mkdir(cur.c_str(), 0755) != 0) return false;
        }
    }
    return true;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string formatTime(time_t t) {
    struct tm* tm_info = localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}