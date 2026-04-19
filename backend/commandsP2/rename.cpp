#include "rename.h"
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

static bool hasWritePerm(const Inode& inode, const Session& session) {
    if (session.uid == 1) return true;
    char permChar;
    if      (inode.i_uid == session.uid) permChar = inode.i_perm[0];
    else if (inode.i_gid == session.gid) permChar = inode.i_perm[1];
    else                                 permChar = inode.i_perm[2];
    return ((permChar - '0') & 2) != 0;
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
                strncmp(fb.b_content[j].b_name, name, 12) == 0)
                return fb.b_content[j].b_inodo;
        }
    }
    return -1;
}

// Verificar que un nombre NO existe en el directorio padre
static bool nameExistsInDir(std::fstream& file, const Superblock& sb,
                              int parentInodeIdx, const std::string& name) {
    return findInDir(file, sb, parentInodeIdx, name.c_str()) != -1;
}

// Renombrar la entrada en el FolderBlock del padre
static bool renameEntryInParent(std::fstream& file, const Superblock& sb,
                                 int parentInodeIdx, int targetInodeIdx,
                                 const std::string& newName) {
    Inode parent = readInode(file, sb, parentInodeIdx);
    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == targetInodeIdx) {
                memset(fb.b_content[j].b_name, 0, 12);
                memcpy(fb.b_content[j].b_name, newName.c_str(),
                       std::min((int)newName.size(), 11));
                file.seekp(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                return true;
            }
        }
    }
    return false;
}

// Resolver path → retorna parentInodeIdx, targetInodeIdx y nombre final
static bool resolvePath(std::fstream& file, const Superblock& sb,
                         const std::string& path,
                         int& outParent, int& outTarget, std::string& outName) {
    std::vector<std::string> parts;
    std::string seg;
    for (char c : path) {
        if (c == '/') {
            if (!seg.empty()) { parts.push_back(seg); seg.clear(); }
        } else {
            seg += c;
        }
    }
    if (!seg.empty()) parts.push_back(seg);
    if (parts.empty()) return false;

    int cur = 0;
    for (int i = 0; i < (int)parts.size() - 1; i++) {
        int next = findInDir(file, sb, cur, parts[i].c_str());
        if (next == -1) return false;
        cur = next;
    }

    outParent = cur;
    outName   = parts.back();
    outTarget = findInDir(file, sb, cur, outName.c_str());
    return (outTarget != -1);
}

// ─────────────────────────────────────────────────────────────
//  cmdRename
// ─────────────────────────────────────────────────────────────
std::string cmdRename(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path") == p.end())
        return "ERROR: falta -path";
    if (p.find("name") == p.end())
        return "ERROR: falta -name";

    std::string path    = p.at("path");
    std::string newName = p.at("name");

    if (path.empty() || path[0] != '/')
        return "ERROR: -path debe iniciar con '/'";
    if (newName.empty())
        return "ERROR: -name no puede estar vacío";
    if (newName.size() > 11)
        return "ERROR: -name máximo 11 caracteres";

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

    // ── Resolver path ─────────────────────────────────────────
    int parentIdx, targetIdx;
    std::string currentName;
    bool found = resolvePath(file, sb, path, parentIdx, targetIdx, currentName);
    if (!found) {
        file.close();
        return "ERROR: no existe la ruta: " + path;
    }

    // ── Verificar permiso de escritura sobre el target ────────
    Inode targetInode = readInode(file, sb, targetIdx);
    if (!hasWritePerm(targetInode, activeSession)) {
        file.close();
        return "ERROR: sin permiso de escritura sobre: " + path;
    }

    // ── Verificar que el nuevo nombre no exista ya ────────────
    if (nameExistsInDir(file, sb, parentIdx, newName)) {
        file.close();
        return "ERROR: ya existe un archivo o carpeta con el nombre: " + newName;
    }

    // ── Renombrar entrada en el padre ─────────────────────────
    bool ok = renameEntryInParent(file, sb, parentIdx, targetIdx, newName);
    if (!ok) {
        file.close();
        return "ERROR: no se pudo renombrar la entrada en el directorio padre";
    }

    file.close();
    return "SUCCESS: renombrado\n"
           "  path         : " + path + "\n"
           "  nombre nuevo : " + newName;
}