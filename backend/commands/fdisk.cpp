#include "fdisk.h"
#include "../structures/mbr.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <vector>

struct FreeSpace {
    int start;
    int size;
};

static std::vector<FreeSpace> getFreeSpaces(MBR& mbr, int diskSize) {
    std::vector<int> usedStarts;

    for (int i = 0; i < 4; i++)
        if (mbr.mbr_partitions[i].part_start != -1)
            usedStarts.push_back(mbr.mbr_partitions[i].part_start);

    for (int i = 0; i < (int)usedStarts.size()-1; i++)
        for (int j = i+1; j < (int)usedStarts.size(); j++)
            if (usedStarts[i] > usedStarts[j])
                std::swap(usedStarts[i], usedStarts[j]);

    std::vector<FreeSpace> spaces;
    int current = sizeof(MBR);

    for (int start : usedStarts) {
        if (start > current)
            spaces.push_back({current, start - current});
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start == start) {
                current = start + mbr.mbr_partitions[i].part_s;
                break;
            }
        }
    }

    if (current < diskSize)
        spaces.push_back({current, diskSize - current});

    return spaces;
}

static int chooseSpace(std::vector<FreeSpace>& spaces, int needed, char fit) {
    int chosen = -1;

    if (fit == 'F') {
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed) { chosen = i; break; }
        }
    } else if (fit == 'B') {
        int best = -1;
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed)
                if (best == -1 || spaces[i].size < spaces[best].size)
                    best = i;
        }
        chosen = best;
    } else if (fit == 'W') {
        int worst = -1;
        for (int i = 0; i < (int)spaces.size(); i++) {
            if (spaces[i].size >= needed)
                if (worst == -1 || spaces[i].size > spaces[worst].size)
                    worst = i;
        }
        chosen = worst;
    }

    return chosen;
}

std::string cmdFdisk(const std::map<std::string,std::string>& p) {

    // ── Detectar operación especial: -delete o -add ───────────
    bool hasDelete = (p.find("delete") != p.end());
    bool hasAdd    = (p.find("add")    != p.end());

    if (hasDelete || hasAdd) {
        // Ambas requieren -path y -name
        if (p.find("path") == p.end()) return "ERROR: falta -path";
        if (p.find("name") == p.end()) return "ERROR: falta -name";

        std::string path = expandPath(p.at("path"));
        std::string name = p.at("name");

        if (!fileExists(path))
            return "ERROR: el disco no existe: " + path;

        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open())
            return "ERROR: no se pudo abrir el disco: " + path;

        MBR mbr;
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

        // ── Buscar la partición por nombre en MBR ─────────────
        int slot = -1;
        for (int i = 0; i < 4; i++) {
            std::string pname(mbr.mbr_partitions[i].part_name, 16);
            pname = pname.substr(0, pname.find('\0'));
            if (pname == name) { slot = i; break; }
        }

        if (slot == -1) {
            file.close();
            return "ERROR: no existe partición con nombre: " + name;
        }

        // ════════════════════════════════════════════════════════
        //  DELETE
        // ════════════════════════════════════════════════════════
        if (hasDelete) {
            std::string mode = toLower(p.at("delete"));
            if (mode != "fast" && mode != "full")
                return "ERROR: -delete solo acepta 'fast' o 'full'";

            int pStart = mbr.mbr_partitions[slot].part_start;
            int pSize  = mbr.mbr_partitions[slot].part_s;
            char pType = mbr.mbr_partitions[slot].part_type;

            // Si es extendida, borrar también las lógicas (EBRs)
            if (pType == 'E' && mode == "full") {
                // Limpiar todo el espacio extendido con \0
                std::vector<char> zeros(pSize, '\0');
                file.seekp(pStart);
                file.write(zeros.data(), pSize);
            } else if (pType == 'E' && mode == "fast") {
                // Solo limpiar el primer EBR
                EBR emptyEBR;
                memset(&emptyEBR, 0, sizeof(EBR));
                emptyEBR.part_start = -1;
                emptyEBR.part_s     = -1;
                emptyEBR.part_next  = -1;
                file.seekp(pStart);
                file.write(reinterpret_cast<char*>(&emptyEBR), sizeof(EBR));
            }

            if (mode == "full") {
                // Rellenar el espacio de la partición con \0
                std::vector<char> zeros(pSize, '\0');
                file.seekp(pStart);
                file.write(zeros.data(), pSize);
            }

            // Limpiar entrada en MBR
            mbr.mbr_partitions[slot].part_status      = '0';
            mbr.mbr_partitions[slot].part_type        = '\0';
            mbr.mbr_partitions[slot].part_fit         = '\0';
            mbr.mbr_partitions[slot].part_start       = -1;
            mbr.mbr_partitions[slot].part_s           = -1;
            mbr.mbr_partitions[slot].part_correlative = -1;
            memset(mbr.mbr_partitions[slot].part_name, 0, 16);
            memset(mbr.mbr_partitions[slot].part_id,   0, 4);

            file.seekp(0);
            file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            file.close();

            return "SUCCESS: partición eliminada (" + mode + ")\n"
                   "  nombre : " + name;
        }

        // ════════════════════════════════════════════════════════
        //  ADD
        // ════════════════════════════════════════════════════════
        if (hasAdd) {
            int addAmount = std::stoi(p.at("add"));
            // -add siempre trabaja en bytes
            int addBytes = addAmount;

            int currentSize  = mbr.mbr_partitions[slot].part_s;
            int currentStart = mbr.mbr_partitions[slot].part_start;
            int newSize      = currentSize + addBytes;

            if (newSize <= 0) {
                file.close();
                return "ERROR: no se puede reducir la partición a tamaño negativo o cero";
            }

            if (addBytes > 0) {
                // Verificar que hay espacio libre después de la partición
                int endOfPart = currentStart + currentSize;
                bool spaceAvailable = true;

                for (int i = 0; i < 4; i++) {
                    if (i == slot) continue;
                    int otherStart = mbr.mbr_partitions[i].part_start;
                    if (otherStart == -1) continue;
                    // Otra partición empieza dentro del espacio que queremos agregar
                    if (otherStart >= endOfPart && otherStart < endOfPart + addBytes) {
                        spaceAvailable = false;
                        break;
                    }
                }

                if (endOfPart + addBytes > mbr.mbr_tamano)
                    spaceAvailable = false;

                if (!spaceAvailable) {
                    file.close();
                    return "ERROR: no hay espacio libre suficiente después de la partición";
                }
            }

            mbr.mbr_partitions[slot].part_s = newSize;

            file.seekp(0);
            file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            file.close();

            std::string dir = (addBytes >= 0) ? "agregados" : "eliminados";
            return "SUCCESS: espacio " + dir + " a partición\n"
                   "  nombre       : " + name + "\n"
                   "  tamaño nuevo : " + std::to_string(newSize) + " bytes";
        }

        file.close();
        return "ERROR: operación no reconocida";
    }

    // ── Validar parámetros ────────────────────────────────────
    if (p.find("size") == p.end())
        return "ERROR: falta -size";
    int size = std::stoi(p.at("size"));
    if (size <= 0)
        return "ERROR: -size debe ser mayor que 0";

    if (p.find("path") == p.end())
        return "ERROR: falta -path";
    std::string path = expandPath(p.at("path"));

    if (p.find("name") == p.end())
        return "ERROR: falta -name";
    std::string name = p.at("name");
    if (name.size() > 16)
        return "ERROR: -name máximo 16 caracteres";

    // ── -unit (default K) ─────────────────────────────────────
    char unit = 'K';
    if (p.find("unit") != p.end()) {
        std::string u = toLower(p.at("unit"));
        if      (u == "b") unit = 'B';
        else if (u == "k") unit = 'K';
        else if (u == "m") unit = 'M';
        else return "ERROR: -unit solo acepta B, K o M";
    }

    // ── -type (default P) ─────────────────────────────────────
    char type = 'P';
    if (p.find("type") != p.end()) {
        std::string t = toLower(p.at("type"));
        if      (t == "p") type = 'P';
        else if (t == "e") type = 'E';
        else if (t == "l") type = 'L';
        else return "ERROR: -type solo acepta P, E o L";
    }

    // ── -fit (default WF) ─────────────────────────────────────
    char fit = 'W';
    if (p.find("fit") != p.end()) {
        std::string f = toLower(p.at("fit"));
        if      (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
        else return "ERROR: -fit solo acepta BF, FF o WF";
    }

    // ── Convertir a bytes ─────────────────────────────────────
    int sizeBytes;
    if      (unit == 'B') sizeBytes = size;
    else if (unit == 'K') sizeBytes = size * 1024;
    else                  sizeBytes = size * 1024 * 1024;

    // ── Verificar que el disco existe ─────────────────────────
    if (!fileExists(path))
        return "ERROR: el disco no existe: " + path;

    // ── Abrir disco ───────────────────────────────────────────
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco: " + path;

    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // ═════════════════════════════════════════════════════════
    //  PARTICIÓN LÓGICA — va en EBR, no en MBR
    // ═════════════════════════════════════════════════════════
    if (type == 'L') {
        // Buscar partición extendida
        int extStart = -1, extEnd = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_type == 'E' &&
                mbr.mbr_partitions[i].part_start != -1) {
                extStart = mbr.mbr_partitions[i].part_start;
                extEnd   = extStart + mbr.mbr_partitions[i].part_s;
                break;
            }
        }
        if (extStart == -1) {
            file.close();
            return "ERROR: no existe partición extendida";
        }

        // Leer primer EBR
        EBR ebr;
        file.seekg(extStart);
        file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));

        // Si el primer EBR está vacío, usarlo
        if (ebr.part_start == -1) {
            ebr.part_mount = '0';
            ebr.part_fit   = fit;
            ebr.part_start = extStart + sizeof(EBR);
            ebr.part_s     = sizeBytes;
            ebr.part_next  = -1;
            memset(ebr.part_name, 0, 16);
            memcpy(ebr.part_name, name.c_str(), name.size());

            file.seekp(extStart);
            file.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
            file.close();

            return "SUCCESS: partición lógica creada\n"
                   "  nombre : " + name + "\n"
                   "  inicio : " + std::to_string(ebr.part_start) + " bytes\n"
                   "  tamaño : " + std::to_string(sizeBytes) + " bytes";
        }

        // Recorrer EBRs hasta el último
        int currentPos = extStart;
        while (ebr.part_next != -1) {
            currentPos = ebr.part_next;
            file.seekg(currentPos);
            file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
        }

        // Calcular posición del nuevo EBR
        int newEBRPos = ebr.part_start + ebr.part_s;
        if (newEBRPos + (int)sizeof(EBR) + sizeBytes > extEnd) {
            file.close();
            return "ERROR: no hay espacio en la partición extendida";
        }

        // Crear nuevo EBR
        EBR newEBR;
        newEBR.part_mount = '0';
        newEBR.part_fit   = fit;
        newEBR.part_start = newEBRPos + sizeof(EBR);
        newEBR.part_s     = sizeBytes;
        newEBR.part_next  = -1;
        memset(newEBR.part_name, 0, 16);
        memcpy(newEBR.part_name, name.c_str(), name.size());

        // Enlazar EBR anterior al nuevo
        ebr.part_next = newEBRPos;
        file.seekp(currentPos);
        file.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));

        // Escribir nuevo EBR
        file.seekp(newEBRPos);
        file.write(reinterpret_cast<char*>(&newEBR), sizeof(EBR));
        file.close();

        return "SUCCESS: partición lógica creada\n"
               "  nombre : " + name + "\n"
               "  inicio : " + std::to_string(newEBR.part_start) + " bytes\n"
               "  tamaño : " + std::to_string(sizeBytes) + " bytes";
    }

    // ═════════════════════════════════════════════════════════
    //  PARTICIÓN PRIMARIA O EXTENDIDA — va en MBR
    // ═════════════════════════════════════════════════════════

    // Validaciones
    int countPE = 0;
    bool hasExtended = false;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1) {
            countPE++;
            if (mbr.mbr_partitions[i].part_type == 'E')
                hasExtended = true;
        }
    }
    if (countPE >= 4)
        return "ERROR: ya hay 4 particiones primarias/extendidas";
    if (type == 'E' && hasExtended)
        return "ERROR: ya existe una partición extendida";

    // Buscar espacio libre
    auto spaces = getFreeSpaces(mbr, mbr.mbr_tamano);
    int idx = chooseSpace(spaces, sizeBytes, fit);
    if (idx == -1) {
        file.close();
        return "ERROR: no hay espacio suficiente en el disco";
    }
    int startPos = spaces[idx].start;

    // Buscar slot libre en MBR
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start == -1) { slot = i; break; }
    }
    if (slot == -1) {
        file.close();
        return "ERROR: no hay slots disponibles en el MBR";
    }

    // Llenar partición
    mbr.mbr_partitions[slot].part_status      = '0';
    mbr.mbr_partitions[slot].part_type        = type;
    mbr.mbr_partitions[slot].part_fit         = fit;
    mbr.mbr_partitions[slot].part_start       = startPos;
    mbr.mbr_partitions[slot].part_s           = sizeBytes;
    mbr.mbr_partitions[slot].part_correlative = -1;
    memset(mbr.mbr_partitions[slot].part_name, 0, 16);
    memcpy(mbr.mbr_partitions[slot].part_name, name.c_str(), name.size());
    memset(mbr.mbr_partitions[slot].part_id,   0, 4);

    // Si es extendida, escribir EBR inicial vacío
    if (type == 'E') {
        EBR ebr;
        ebr.part_mount = '0';
        ebr.part_fit   = fit;
        ebr.part_start = -1;
        ebr.part_s     = -1;
        ebr.part_next  = -1;
        memset(ebr.part_name, 0, 16);

        file.seekp(startPos);
        file.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
    }

    // Guardar MBR
    file.seekp(0);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.close();

    std::string typeStr = (type=='P') ? "Primaria" : "Extendida";
    std::string fitStr  = (fit=='B')  ? "BF" : (fit=='F') ? "FF" : "WF";
    std::string unitStr = (unit=='B') ? "B" : (unit=='K') ? "KB" : "MB";

    return "SUCCESS: partición creada\n"
           "  nombre : " + name + "\n"
           "  tipo   : " + typeStr + "\n"
           "  inicio : " + std::to_string(startPos) + " bytes\n"
           "  tamaño : " + std::to_string(size) + " " + unitStr +
                          " (" + std::to_string(sizeBytes) + " bytes)\n"
           "  ajuste : " + fitStr;
}