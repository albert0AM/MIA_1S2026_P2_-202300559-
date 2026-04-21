#include "remove.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"
#include "../commandsP2/journal.h"

#include <fstream>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────
//  Helpers internos
// ─────────────────────────────────────────────────────────────

// Leer inodo desde disco
static Inode readInode(std::fstream& file, const Superblock& sb, int idx) {
    Inode inode;
    file.seekg(sb.s_inode_start + idx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));
    return inode;
}

// Escribir inodo en disco
static void writeInode(std::fstream& file, const Superblock& sb, int idx, const Inode& inode) {
    file.seekp(sb.s_inode_start + idx * sb.s_inode_s);
    file.write(reinterpret_cast<const char*>(&inode), sizeof(Inode));
}

// Marcar inodo como libre en bitmap
static void freeInode(std::fstream& file, const Superblock& sb, int idx) {
    char zero = '0';
    file.seekp(sb.s_bm_inode_start + idx);
    file.write(&zero, 1);
}

// Marcar bloque como libre en bitmap
static void freeBlock(std::fstream& file, const Superblock& sb, int idx) {
    char zero = '0';
    file.seekp(sb.s_bm_block_start + idx);
    file.write(&zero, 1);
}

// Verificar si el usuario activo tiene permiso de escritura sobre un inodo
static bool hasWritePerm(const Inode& inode, const Session& session) {
    // root puede todo
    if (session.uid == 1) return true;

    // Obtener dígito de permisos correspondiente
    char permChar;
    if (inode.i_uid == session.uid) {
        permChar = inode.i_perm[0]; // propietario
    } else if (inode.i_gid == session.gid) {
        permChar = inode.i_perm[1]; // grupo
    } else {
        permChar = inode.i_perm[2]; // otros
    }

    int perm = permChar - '0';
    return (perm & 2) != 0; // bit de escritura
}

// Buscar un nombre en el FolderBlock de un inodo y retornar su inodo hijo
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

// Eliminar entrada de nombre en el FolderBlock del padre
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

// Eliminar recursivamente un inodo (archivo o carpeta)
// Retorna true si se pudo eliminar completamente
static bool removeInode(std::fstream& file, const Superblock& sb,
                         int inodeIdx, const Session& session) {
    Inode inode = readInode(file, sb, inodeIdx);

    // Verificar permiso de escritura
    if (!hasWritePerm(inode, session))
        return false;

    if (inode.i_type == '1') {
        // ── Archivo: liberar bloques de contenido ─────────────
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            freeBlock(file, sb, inode.i_block[b]);
        }
        freeInode(file, sb, inodeIdx);
        return true;
    }

    // ── Carpeta: recorrer hijos recursivamente ────────────────
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
            if (strncmp(fb.b_content[j].b_name, "..", 12) == 0) continue;

            bool ok = removeInode(file, sb, childIdx, session);
            if (!ok) {
                // No se pudo eliminar un hijo → no eliminar esta carpeta ni sus padres
                return false;
            }
        }
    }

    // Liberar bloques de la carpeta
    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] == -1) continue;
        freeBlock(file, sb, inode.i_block[b]);
    }
    freeInode(file, sb, inodeIdx);
    return true;
}

// ─────────────────────────────────────────────────────────────
//  Navegación de path → retorna (inodoParent, inodoTarget, nombreFinal)
// ─────────────────────────────────────────────────────────────
static bool resolvePath(std::fstream& file, const Superblock& sb,
                         const std::string& path,
                         int& outParent, int& outTarget, std::string& outName) {
    // Separar segmentos
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

    int cur = 0; // inodo raíz
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
//  Actualizar superblock (contadores libres)
// ─────────────────────────────────────────────────────────────
static void updateSuperblock(std::fstream& file, int partStart, Superblock& sb) {
    // Recontamos inodos y bloques libres
    int freeInodes = 0, freeBlocks = 0;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char c; file.seekg(sb.s_bm_inode_start + i); file.read(&c, 1);
        if (c == '0') freeInodes++;
    }
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char c; file.seekg(sb.s_bm_block_start + i); file.read(&c, 1);
        if (c == '0') freeBlocks++;
    }
    sb.s_free_inodes_count = freeInodes;
    sb.s_free_blocks_count = freeBlocks;
    file.seekp(partStart);
    file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));
}

// ─────────────────────────────────────────────────────────────
//  cmdRemove
// ─────────────────────────────────────────────────────────────
std::string cmdRemove(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path") == p.end())
        return "ERROR: falta -path";

    std::string path = p.at("path");
    if (path.empty() || path[0] != '/')
        return "ERROR: -path debe iniciar con '/'";

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
    std::string targetName;
    bool found = resolvePath(file, sb, path, parentIdx, targetIdx, targetName);
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

    // ── Eliminar recursivamente ───────────────────────────────
    bool ok = removeInode(file, sb, targetIdx, activeSession);
    if (!ok) {
        file.close();
        return "ERROR: no se pudo eliminar (permisos insuficientes en subcarpeta): " + path;
    }

    // ── Eliminar entrada del padre ────────────────────────────
    removeEntryFromParent(file, sb, parentIdx, targetIdx);

    // ── Actualizar superblock ─────────────────────────────────
    updateSuperblock(file, partStart, sb);

    // Registrar en journal si es EXT3
    writeJournal(file, sb, "remove", path);

    file.close();
    return "SUCCESS: eliminado\n"
           "  path : " + path;
}