#include "mount.h"
#include "../structures/mbr.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>

std::string cmdMount(const std::map<std::string,std::string>& p) {

    // ── Validar parámetros ────────────────────────────────────
    if (p.find("path") == p.end())
        return "ERROR: falta -path";
    std::string path = expandPath(p.at("path"));

    if (p.find("name") == p.end())
        return "ERROR: falta -name";
    std::string name = p.at("name");

    // ── Verificar que el disco existe ─────────────────────────
    if (!fileExists(path))
        return "ERROR: el disco no existe: " + path;

    // ── Leer MBR ──────────────────────────────────────────────
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco: " + path;

    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // ── Buscar la partición por nombre ────────────────────────
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1) {
            std::string pname(mbr.mbr_partitions[i].part_name);
            if (pname == name) {
                slot = i;
                break;
            }
        }
    }

    if (slot == -1)
        return "ERROR: no se encontró la partición: " + name;

    if (mbr.mbr_partitions[slot].part_status == '1')
        return "ERROR: la partición ya está montada";
    
    if (slot == -1)
    return "ERROR: no se encontró la partición: " + name;

    // ── Asignar letra al disco si no tiene ────────────────────
    if (diskLetter.find(path) == diskLetter.end()) {
        char next = 'A' + (char)diskLetter.size();
        diskLetter[path] = next;
    }
    char letter = diskLetter[path];

    // ── Asignar correlativo ───────────────────────────────────
    std::string key = path + ":" + letter;
    if (diskCorrelative.find(key) == diskCorrelative.end())
        diskCorrelative[key] = 1;
    else
        diskCorrelative[key]++;

    int correlative = diskCorrelative[key];

    // Carnet: 202300559 → "59"
    std::string id = "59" + std::to_string(correlative) + letter;

    // ── Actualizar partición en disco ─────────────────────────
    mbr.mbr_partitions[slot].part_status      = '1';
    mbr.mbr_partitions[slot].part_correlative = correlative;
    memset(mbr.mbr_partitions[slot].part_id, 0, 4);
    memcpy(mbr.mbr_partitions[slot].part_id, id.c_str(),
           std::min((int)id.size(), 4));

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.close();

    // ── Guardar en memoria ────────────────────────────────────
    MountedPartition mp;
    mp.id          = id;
    mp.path        = path;
    mp.name        = name;
    mp.correlative = correlative;
    mp.letter      = letter;
    mountedPartitions[id] = mp;

    return "SUCCESS: partición montada\n"
           "  nombre : " + name + "\n"
           "  id     : " + id + "\n"
           "  disco  : " + path + "\n"
           "  letra  : " + letter;
}

std::string cmdMounted() {
    if (mountedPartitions.empty())
        return "No hay particiones montadas";

    std::string result = "Particiones montadas:\n";
    for (auto& pair : mountedPartitions) {
        result += "  " + pair.second.id +
                  " → " + pair.second.path +
                  " [" + pair.second.name + "]\n";
    }
    return result;
}

std::string cmdUnmount(const std::map<std::string,std::string>& p) {

    // ── Validar -id ───────────────────────────────────────────
    if (p.find("id") == p.end())
        return "ERROR: falta -id";
    std::string id = p.at("id");

    // ── Verificar que existe en memoria ───────────────────────
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: no existe partición montada con id: " + id;

    MountedPartition& mp = mountedPartitions[id];

    // ── Abrir disco ───────────────────────────────────────────
    std::fstream file(mp.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco: " + mp.path;

    // ── Leer MBR ──────────────────────────────────────────────
    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // ── Buscar la partición por id en el MBR ─────────────────
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { slot = i; break; }
    }

    if (slot == -1) {
        file.close();
        return "ERROR: no se encontró la partición en el disco para id: " + id;
    }

    // ── Resetear estado en disco ──────────────────────────────
    mbr.mbr_partitions[slot].part_status      = '0';
    mbr.mbr_partitions[slot].part_correlative = 0;   // vuelve a estado inicial
    memset(mbr.mbr_partitions[slot].part_id, 0, 4);

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.close();

    // ── Eliminar de memoria ───────────────────────────────────
    std::string name = mp.name;
    mountedPartitions.erase(id);

    return "SUCCESS: partición desmontada\n"
           "  id     : " + id + "\n"
           "  nombre : " + name;
}