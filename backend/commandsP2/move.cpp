#include "move.h"
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

static void writeInode(std::fstream& file, const Superblock& sb, int idx, const Inode& inode) {
    file.seekp(sb.s_inode_start + idx * sb.s_inode_s);
    file.write(reinterpret_cast<const char*>(&inode), sizeof(Inode));
}

// Verificar permiso de escritura
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

// Insertar entrada en carpeta destino
static bool insertInFolder(std::fstream& file, const Superblock& sb,
                            int parentInodeIdx, const std::string& name,
                            int childInodeIdx) {
    Inode parent = readInode(file, sb, parentInodeIdx);
    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) {
                memset(fb.b_content[j].b_name, 0, 12);
                memcpy(fb.b_content[j].b_name, name.c_str(),
                       std::min((int)name.size(), 11));
                fb.b_content[j].b_inodo = childInodeIdx;
                file.seekp(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                return true;
            }
        }
    }
    // Buscar slot nuevo en i_block del padre
    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] != -1) continue;
        // Buscar bloque libre
        for (int i = 0; i < sb.s_blocks_count; i++) {
            char status;
            file.seekg(sb.s_bm_block_start + i);
            file.read(&status, 1);
            if (status == '0') {
                char one = '1';
                file.seekp(sb.s_bm_block_start + i);
                file.write(&one, 1);

                FolderBlock fb;
                memset(&fb, 0, sizeof(FolderBlock));
                for (int j = 0; j < 4; j++) fb.b_content[j].b_inodo = -1;
                memset(fb.b_content[0].b_name, 0, 12);
                memcpy(fb.b_content[0].b_name, name.c_str(),
                       std::min((int)name.size(), 11));
                fb.b_content[0].b_inodo = childInodeIdx;
                file.seekp(sb.s_block_start + i * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

                parent.i_block[b] = i;
                writeInode(file, sb, parentInodeIdx, parent);
                return true;
            }
        }
    }
    return false;
}

// Eliminar entrada del padre por inodo hijo
static void removeEntryFromParent(std::fstream& file, const Superblock& sb,
                                   int parentInodeIdx, int childInodeIdx) {
    Inode parent = readInode(file, sb, parentInodeIdx);
    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == childInodeIdx) {
                memset(fb.b_content[j].b_name, 0, 12);
                fb.b_content[j].b_inodo = -1;
                file.seekp(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                return;
            }
        }
    }
}

// Actualizar referencia ".." dentro de una carpeta movida
static void updateDotDot(std::fstream& file, const Superblock& sb,
                          int movedInodeIdx, int newParentInodeIdx) {
    Inode moved = readInode(file, sb, movedInodeIdx);
    if (moved.i_type != '0') return; // solo carpetas tienen ".."
    for (int b = 0; b < 12; b++) {
        if (moved.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + moved.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (strncmp(fb.b_content[j].b_name, "..", 12) == 0) {
                fb.b_content[j].b_inodo = newParentInodeIdx;
                file.seekp(sb.s_block_start + moved.i_block[b] * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                return;
            }
        }
    }
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
//  cmdMove
// ─────────────────────────────────────────────────────────────
std::string cmdMove(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path")    == p.end()) return "ERROR: falta -path";
    if (p.find("destino") == p.end()) return "ERROR: falta -destino";

    std::string srcPath  = p.at("path");
    std::string destPath = p.at("destino");

    if (srcPath.empty()  || srcPath[0]  != '/') return "ERROR: -path debe iniciar con '/'";
    if (destPath.empty() || destPath[0] != '/') return "ERROR: -destino debe iniciar con '/'";

    // No tiene sentido mover a sí mismo
    if (srcPath == destPath)
        return "ERROR: origen y destino son el mismo path";

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

    // ── Resolver origen ───────────────────────────────────────
    int srcParent, srcTarget;
    std::string srcName;
    bool foundSrc = resolvePath(file, sb, srcPath, srcParent, srcTarget, srcName);
    if (!foundSrc) {
        file.close();
        return "ERROR: no existe la ruta origen: " + srcPath;
    }

    // Verificar permiso de escritura sobre el origen
    Inode srcInode = readInode(file, sb, srcTarget);
    if (!hasWritePerm(srcInode, activeSession)) {
        file.close();
        return "ERROR: sin permiso de escritura sobre: " + srcPath;
    }

    // ── Resolver destino ──────────────────────────────────────
    int destDirInode = resolveDir(file, sb, destPath);
    if (destDirInode == -1) {
        file.close();
        return "ERROR: no existe la carpeta destino: " + destPath;
    }

    // Verificar permiso de escritura sobre el destino
    Inode destInode = readInode(file, sb, destDirInode);
    if (!hasWritePerm(destInode, activeSession)) {
        file.close();
        return "ERROR: sin permiso de escritura sobre destino: " + destPath;
    }

    // ── Mover: solo cambia referencias (misma partición) ──────
    // 1. Insertar en el destino con el mismo nombre
    bool inserted = insertInFolder(file, sb, destDirInode, srcName, srcTarget);
    if (!inserted) {
        file.close();
        return "ERROR: no se pudo insertar en el destino (sin espacio)";
    }

    // 2. Eliminar del padre original
    removeEntryFromParent(file, sb, srcParent, srcTarget);

    // 3. Si es carpeta, actualizar ".." para apuntar al nuevo padre
    updateDotDot(file, sb, srcTarget, destDirInode);

    file.close();
    return "SUCCESS: movido\n"
           "  origen  : " + srcPath + "\n"
           "  destino : " + destPath + "/" + srcName;
}