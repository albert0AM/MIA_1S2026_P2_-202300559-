#include "mkdisk.h"
#include "../structures/mbr.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <cstdlib>
#include <ctime>

std::string cmdMkdisk(const std::map<std::string,std::string>& p) {

    // Validar parámetros permitidos
    for (const auto& kv : p) {
        if (kv.first == "cmd") continue;
        if (kv.first != "size" && kv.first != "unit" &&
            kv.first != "fit"  && kv.first != "path") {
            return "ERROR: parámetro desconocido: -" + kv.first;
        }
    }

    if (p.find("size") == p.end())
        return "ERROR: falta -size";
    int size = std::stoi(p.at("size"));
    if (size <= 0)
        return "ERROR: -size debe ser mayor que 0";

    if (p.find("path") == p.end())
        return "ERROR: falta -path";
    std::string path = expandPath(p.at("path"));
    // Crear directorios padres si no existen
    mkdirRecursive(path.substr(0, path.find_last_of('/')));

    char unit = 'M';
    if (p.find("unit") != p.end()) {
        std::string u = toLower(p.at("unit"));
        if      (u == "k") unit = 'K';
        else if (u == "m") unit = 'M';
        else return "ERROR: -unit solo acepta K o M";
    }

    char fit = 'F';
    if (p.find("fit") != p.end()) {
        std::string f = toLower(p.at("fit"));
        if      (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
        else return "ERROR: -fit solo acepta BF, FF o WF";
    }

    long long bytes = (unit == 'K')
        ? (long long)size * 1024
        : (long long)size * 1024 * 1024;

    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        if (!dir.empty() && !mkdirRecursive(dir))
            return "ERROR: no se pudo crear el directorio: " + dir;
    }

    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            return "ERROR: no se pudo crear el archivo: " + path;
        char buf[1024] = {};
        long long written = 0;
        while (written + 1024 <= bytes) { f.write(buf, 1024); written += 1024; }
        if (written < bytes) f.write(buf, bytes - written);
    }

    MBR mbr;
    mbr.mbr_tamano         = (int)bytes;
    mbr.mbr_dsk_signature  = rand();
    mbr.dsk_fit            = fit;

    {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f.is_open())
            return "ERROR: no se pudo escribir el MBR: " + path;
        f.seekp(0);
        f.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    }

    std::string fitStr  = (fit=='B') ? "BF" : (fit=='F') ? "FF" : "WF";
    std::string unitStr = (unit=='K') ? "KB" : "MB";

    return "SUCCESS: disco creado\n"
           "  path : " + path + "\n"
           "  size : " + std::to_string(size) + " " + unitStr +
                        " (" + std::to_string(bytes) + " bytes)\n"
           "  fit  : " + fitStr + "\n"
            "  date : " + std::string(mbr.mbr_fecha_creacion);
}