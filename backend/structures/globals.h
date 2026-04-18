#pragma once
#include <string>
#include <map>

struct MountedPartition {
    std::string id;
    std::string path;
    std::string name;
    int         correlative;
    char        letter;
};

struct Session {
    bool        active;
    std::string username;
    std::string partId;
    int         uid;
    int         gid;

    Session() : active(false), uid(0), gid(0) {}
};

// Particiones montadas: id → MountedPartition
extern std::map<std::string, MountedPartition> mountedPartitions;

// Letra por disco: path → 'A', 'B', 'C'...
extern std::map<std::string, char> diskLetter;

// Correlativo por disco+letra: "path:A" → 1, 2, 3...
extern std::map<std::string, int> diskCorrelative;

// Sesión activa
extern Session activeSession;