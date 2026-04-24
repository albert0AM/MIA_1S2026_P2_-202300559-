#include "find.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────
//  Helpers internos
// ─────────────────────────────────────────────────────────────

static Inode readInode(std::fstream& file, const Superblock& sb, int idx) {
    Inode inode;
    file.seekg(sb.s_inode_start + idx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));
    return inode;
}

// Verificar permiso de lectura
static bool hasReadPerm(const Inode& inode, const Session& session) {
    if (session.uid == 1) return true;
    char permChar;
    if      (inode.i_uid == session.uid) permChar = inode.i_perm[0];
    else if (inode.i_gid == session.gid) permChar = inode.i_perm[1];
    else                                 permChar = inode.i_perm[2];
    return ((permChar - '0') & 4) != 0;
}

// Buscar nombre en directorio → retorna inodo hijo
static int findInDir(std::fstream& file, const Superblock& sb,
                     int inodeIdx, const char* name) {
    Inode cur = readInode(file, sb, inodeIdx);
    for (int b = 0; b < 12; b++) {
        if (cur.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + cur.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo != -1 &&
                strncmp(fb.b_content[j].b_name, name, 24) == 0)
                return fb.b_content[j].b_inodo;
        }
    }
    return -1;
}

// Resolver inodo de un path directorio
static int resolveDir(std::fstream& file, const Superblock& sb,
                       const std::string& path) {
    std::vector<std::string> parts;
    std::string seg;
    for (char c : path) {
        if (c == '/') {
            if (!seg.empty()) { parts.push_back(seg); seg.clear(); }
        } else {
            seg += c;
        }
    }
    int cur = 0;
    for (auto& part : parts) {
        int next = findInDir(file, sb, cur, part.c_str());
        if (next == -1) return -1;
        cur = next;
    }
    return cur;
}

// ─────────────────────────────────────────────────────────────
//  Matching con wildcards: ? = un carácter, * = uno o más
// ─────────────────────────────────────────────────────────────
static bool matchWildcard(const std::string& pattern, const std::string& name) {
    int p = 0, n = 0;
    int pLen = (int)pattern.size();
    int nLen = (int)name.size();

    // Índices de retroceso para *
    int starP = -1, starN = -1;

    while (n < nLen) {
        if (p < pLen && (pattern[p] == '?' || pattern[p] == name[n])) {
            // '?' coincide con exactamente un carácter
            p++; n++;
        } else if (p < pLen && pattern[p] == '*') {
            // Guardar posición del * para retroceso
            starP = p;
            starN = n;
            p++;
            // * debe coincidir con al menos un carácter
            if (n < nLen) n++;
        } else if (starP != -1) {
            // Retroceder: * consume un carácter más
            p = starP + 1;
            starN++;
            n = starN;
        } else {
            return false;
        }
    }

    // Consumir * sobrantes al final del patrón
    while (p < pLen && pattern[p] == '*') p++;

    return (p == pLen);
}

// ─────────────────────────────────────────────────────────────
//  Búsqueda DFS recursiva
//  Llena 'results' con las rutas encontradas
// ─────────────────────────────────────────────────────────────
static void searchDFS(std::fstream& file, const Superblock& sb,
                       int inodeIdx, const std::string& currentPath,
                       const std::string& pattern,
                       const Session& session,
                       std::vector<std::string>& results) {

    Inode inode = readInode(file, sb, inodeIdx);
    if (inode.i_type != '0') return; // solo explorar carpetas

    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + inode.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        for (int j = 0; j < 4; j++) {
            int childIdx = fb.b_content[j].b_inodo;
            if (childIdx == -1) continue;

            // Saltar . y ..
            if (strncmp(fb.b_content[j].b_name, ".",  12) == 0) continue;
            if (strncmp(fb.b_content[j].b_name, "..", 24) == 0) continue;

            std::string childName(fb.b_content[j].b_name,
                                  strnlen(fb.b_content[j].b_name, 24));

            Inode childInode = readInode(file, sb, childIdx);

            // Verificar permiso de lectura
            if (!hasReadPerm(childInode, session)) continue;

            std::string childPath = currentPath;
            if (childPath.back() != '/') childPath += '/';
            childPath += childName;

            // Verificar si el nombre coincide con el patrón
            if (matchWildcard(pattern, childName)) {
                results.push_back(childPath);
            }

            // Si es carpeta, buscar recursivamente
            if (childInode.i_type == '0') {
                searchDFS(file, sb, childIdx, childPath, pattern, session, results);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  Formatear resultados como árbol
// ─────────────────────────────────────────────────────────────
static std::string formatResults(const std::vector<std::string>& results) {
    if (results.empty())
        return "  (sin resultados)";

    std::string out;
    for (auto& r : results) {
        out += "  " + r + "\n";
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
//  cmdFind
// ─────────────────────────────────────────────────────────────
std::string cmdFind(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path") == p.end()) return "ERROR: falta -path";
    if (p.find("name") == p.end()) return "ERROR: falta -name";

    std::string searchPath = p.at("path");
    std::string pattern    = p.at("name");

    if (searchPath.empty() || searchPath[0] != '/')
        return "ERROR: -path debe iniciar con '/'";
    if (pattern.empty())
        return "ERROR: -name no puede estar vacío";

    // ── Obtener partición activa ──────────────────────────────
    if (mountedPartitions.find(activeSession.partId) == mountedPartitions.end())
        return "ERROR: no hay partición activa";

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    std::fstream file(mp.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco";

    // ── Leer MBR y Superblock ─────────────────────────────────
    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == activeSession.partId) {
            partStart = mbr.mbr_partitions[i].part_start;
            break;
        }
    }
    if (partStart == -1) { file.close(); return "ERROR: partición no encontrada"; }

    Superblock sb;
    file.seekg(partStart);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    if (sb.s_magic != 0xEF53) { file.close(); return "ERROR: partición no formateada"; }

    // ── Resolver inodo del path de búsqueda ───────────────────
    int startInode = resolveDir(file, sb, searchPath);
    if (startInode == -1) {
        file.close();
        return "ERROR: no existe la ruta: " + searchPath;
    }

    // Verificar que el path inicial sea una carpeta con permiso de lectura
    Inode startInodeData = readInode(file, sb, startInode);
    if (startInodeData.i_type != '0') {
        file.close();
        return "ERROR: -path debe ser una carpeta";
    }
    if (!hasReadPerm(startInodeData, activeSession)) {
        file.close();
        return "ERROR: sin permiso de lectura sobre: " + searchPath;
    }

    // ── Búsqueda DFS ──────────────────────────────────────────
    std::vector<std::string> results;
    searchDFS(file, sb, startInode, searchPath, pattern, activeSession, results);

    file.close();

    if (results.empty())
        return "find: no se encontraron coincidencias para '" + pattern + "' en " + searchPath;

    std::string out = "find: resultados para '" + pattern + "' en " + searchPath + ":\n";
    out += formatResults(results);
    return out;
}