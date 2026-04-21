#include "journaling.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <ctime>
#include <sstream>
#include <vector>
#include <algorithm>

// Convertir timestamp float a string legible
static std::string timestampToString(float ts) {
    time_t t = (time_t)ts;
    char buf[32];
    struct tm* tm_info = localtime(&t);
    if (tm_info)
        strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", tm_info);
    else
        snprintf(buf, sizeof(buf), "%.0f", ts);
    return std::string(buf);
}

std::string cmdJournaling(const std::map<std::string,std::string>& p) {

    // ── Validar -id ───────────────────────────────────────────
    if (p.find("id") == p.end())
        return "ERROR: falta -id";
    std::string id = p.at("id");

    // ── Buscar partición montada ──────────────────────────────
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: no existe partición montada con id: " + id;

    MountedPartition& mp = mountedPartitions[id];

    // ── Abrir disco ───────────────────────────────────────────
    std::fstream file(mp.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco: " + mp.path;

    // ── Leer MBR para encontrar la partición ──────────────────
    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) {
            partStart = mbr.mbr_partitions[i].part_start;
            break;
        }
    }
    if (partStart == -1) {
        file.close();
        return "ERROR: no se encontró la partición en el disco";
    }

    // ── Leer Superblock ───────────────────────────────────────
    Superblock sb;
    file.seekg(partStart);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    if (sb.s_magic != 0xEF53) {
        file.close();
        return "ERROR: la partición no está formateada";
    }

    if (sb.s_filesystem_type != 3 || sb.s_journal_start == -1) {
        file.close();
        return "ERROR: la partición no es EXT3 o no tiene journal";
    }

    // ── Leer las 50 entradas del journal ──────────────────────
    int journalSize = sizeof(Journal);
    std::vector<Journal> entries(50);
    for (int i = 0; i < 50; i++) {
        file.seekg(sb.s_journal_start + i * journalSize);
        file.read(reinterpret_cast<char*>(&entries[i]), journalSize);
    }
    file.close();

    // ── Filtrar entradas válidas (j_count != -1) ──────────────
    std::vector<Journal> valid;
    for (auto& e : entries)
        if (e.j_count != -1) valid.push_back(e);

    if (valid.empty())
        return "JOURNALING: no hay entradas registradas para: " + id;

    // ── Ordenar por j_count ascendente ────────────────────────
    std::sort(valid.begin(), valid.end(),
              [](const Journal& a, const Journal& b) {
                  return a.j_count < b.j_count;
              });

    // ── Construir respuesta JSON para el frontend ─────────────
    // Formato: JSON array de objetos con operacion, path, contenido, fecha
    std::ostringstream json;
    json << "[";
    for (int i = 0; i < (int)valid.size(); i++) {
        Journal& e = valid[i];

        std::string op(e.j_content.i_operation,
                       strnlen(e.j_content.i_operation, 10));
        std::string pt(e.j_content.i_path,
                       strnlen(e.j_content.i_path, 32));
        std::string ct(e.j_content.i_content,
                       strnlen(e.j_content.i_content, 64));
        std::string dt = timestampToString(e.j_content.i_date);

        // Escapar comillas dobles en contenido
        std::string ctEscaped;
        for (char c : ct) {
            if (c == '"') ctEscaped += "\\\"";
            else          ctEscaped += c;
        }

        json << "{"
             << "\"count\":"     << e.j_count    << ","
             << "\"operacion\":\"" << op          << "\","
             << "\"path\":\""      << pt          << "\","
             << "\"contenido\":\"" << ctEscaped   << "\","
             << "\"fecha\":\""     << dt          << "\""
             << "}";

        if (i < (int)valid.size() - 1) json << ",";
    }
    json << "]";

    return json.str();
}