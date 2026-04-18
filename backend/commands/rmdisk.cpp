#include "rmdisk.h"
#include "../utils/utils.h"
#include <cstdio>

std::string cmdRmdisk(const std::map<std::string,std::string>& p) {

    if (p.find("path") == p.end())
        return "ERROR: falta -path";

    std::string path = expandPath(p.at("path"));

    if (path.empty())
        return "ERROR: -path no puede estar vacío";

    if (!fileExists(path))
        return "ERROR: el disco no existe: " + path;

    if (std::remove(path.c_str()) != 0)
        return "ERROR: no se pudo eliminar: " + path;

    return "SUCCESS: disco eliminado\n"
           "  path : " + path;
}