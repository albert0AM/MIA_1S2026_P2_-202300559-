#include "copy.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"
#include "../commandsP2/journal.h"

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

// Verificar permiso de lectura
static bool hasReadPerm(const Inode& inode, const Session& session) {
    if (session.uid == 1) return true;
    char permChar;
    if      (inode.i_uid == session.uid) permChar = inode.i_perm[0];
    else if (inode.i_gid == session.gid) permChar = inode.i_perm[1];
    else                                 permChar = inode.i_perm[2];
    return ((permChar - '0') & 4) != 0;
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
                strncmp(fb.b_content[j].b_name, name, 24) == 0)
                return fb.b_content[j].b_inodo;
        }
    }
    return -1;
}

// Buscar inodo libre en bitmap
static int allocInode(std::fstream& file, const Superblock& sb) {
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char status;
        file.seekg(sb.s_bm_inode_start + i);
        file.read(&status, 1);
        if (status == '0') {
            char one = '1';
            file.seekp(sb.s_bm_inode_start + i);
            file.write(&one, 1);
            return i;
        }
    }
    return -1;
}

// Buscar bloque libre en bitmap
static int allocBlock(std::fstream& file, const Superblock& sb) {
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char status;
        file.seekg(sb.s_bm_block_start + i);
        file.read(&status, 1);
        if (status == '0') {
            char one = '1';
            file.seekp(sb.s_bm_block_start + i);
            file.write(&one, 1);
            return i;
        }
    }
    return -1;
}

// Insertar entrada en carpeta destino
static bool insertInFolder(std::fstream& file, const Superblock& sb,
                            int parentInodeIdx, const std::string& name, int childInodeIdx) {
    Inode parent = readInode(file, sb, parentInodeIdx);
    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) {
                memset(fb.b_content[j].b_name, 0, 24);
                memcpy(fb.b_content[j].b_name, name.c_str(),
                       std::min((int)name.size(), 23));
                fb.b_content[j].b_inodo = childInodeIdx;
                file.seekp(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                return true;
            }
        }
    }
    // Necesita nuevo bloque para la carpeta destino
    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] != -1) continue;
        int newBlock = allocBlock(file, sb);
        if (newBlock == -1) return false;
        FolderBlock fb;
        memset(&fb, 0, sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) fb.b_content[j].b_inodo = -1;
        memset(fb.b_content[0].b_name, 0, 24);
        memcpy(fb.b_content[0].b_name, name.c_str(), std::min((int)name.size(), 23));
        fb.b_content[0].b_inodo = childInodeIdx;
        file.seekp(sb.s_block_start + newBlock * sb.s_block_s);
        file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        parent.i_block[b] = newBlock;
        writeInode(file, sb, parentInodeIdx, parent);
        return true;
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

// Resolver solo el inodo de un path (para el destino que ya existe)
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
//  Copia recursiva de un inodo al directorio destino
//  Retorna el nuevo inodo creado, o -1 si falla
// ─────────────────────────────────────────────────────────────
static int copyInode(std::fstream& file, const Superblock& sb,
                      int srcInodeIdx, int destDirInodeIdx,
                      const std::string& name, const Session& session) {

    Inode src = readInode(file, sb, srcInodeIdx);

    // Verificar permiso de lectura sobre el origen
    if (!hasReadPerm(src, session))
        return -1; // sin permiso, omitir silenciosamente

    if (src.i_type == '1') {
        // ── Copiar archivo ────────────────────────────────────
        int newInodeIdx = allocInode(file, sb);
        if (newInodeIdx == -1) return -1;

        Inode newInode = src;
        // Limpiar bloques del nuevo inodo, se copiarán uno a uno
        for (int i = 0; i < 15; i++) newInode.i_block[i] = -1;
        newInode.i_uid = session.uid;
        newInode.i_gid = session.gid;

        // Copiar bloques de contenido
        for (int b = 0; b < 12; b++) {
            if (src.i_block[b] == -1) continue;
            int newBlockIdx = allocBlock(file, sb);
            if (newBlockIdx == -1) return -1;

            FileBlock fb;
            file.seekg(sb.s_block_start + src.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
            file.seekp(sb.s_block_start + newBlockIdx * sb.s_block_s);
            file.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));

            newInode.i_block[b] = newBlockIdx;
        }

        writeInode(file, sb, newInodeIdx, newInode);
        insertInFolder(file, sb, destDirInodeIdx, name, newInodeIdx);
        return newInodeIdx;

    } else {
        // ── Copiar carpeta recursivamente ─────────────────────
        int newInodeIdx = allocInode(file, sb);
        if (newInodeIdx == -1) return -1;

        int newBlockIdx = allocBlock(file, sb);
        if (newBlockIdx == -1) return -1;

        // Crear inodo de la nueva carpeta
        Inode newInode = src;
        for (int i = 0; i < 15; i++) newInode.i_block[i] = -1;
        newInode.i_block[0] = newBlockIdx;
        newInode.i_uid = session.uid;
        newInode.i_gid = session.gid;
        writeInode(file, sb, newInodeIdx, newInode);

        // Crear FolderBlock con . y ..
        FolderBlock fb;
        memset(&fb, 0, sizeof(FolderBlock));
        memcpy(fb.b_content[0].b_name, ".",  1);
        fb.b_content[0].b_inodo = newInodeIdx;
        memcpy(fb.b_content[1].b_name, "..", 2);
        fb.b_content[1].b_inodo = destDirInodeIdx;
        fb.b_content[2].b_inodo = -1;
        fb.b_content[3].b_inodo = -1;
        file.seekp(sb.s_block_start + newBlockIdx * sb.s_block_s);
        file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        // Insertar en el destino
        insertInFolder(file, sb, destDirInodeIdx, name, newInodeIdx);

        // Recorrer hijos del origen y copiarlos recursivamente
        for (int b = 0; b < 12; b++) {
            if (src.i_block[b] == -1) continue;
            FolderBlock srcFb;
            file.seekg(sb.s_block_start + src.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&srcFb), sizeof(FolderBlock));
            for (int j = 0; j < 4; j++) {
                int childIdx = srcFb.b_content[j].b_inodo;
                if (childIdx == -1) continue;
                if (strncmp(srcFb.b_content[j].b_name, ".",  24) == 0) continue;
                if (strncmp(srcFb.b_content[j].b_name, "..", 24) == 0) continue;

                std::string childName(srcFb.b_content[j].b_name,
                                      strnlen(srcFb.b_content[j].b_name, 24));
                // Si falla por permisos, simplemente se omite ese hijo
                copyInode(file, sb, childIdx, newInodeIdx, childName, session);
            }
        }

        return newInodeIdx;
    }
}

// ─────────────────────────────────────────────────────────────
//  Actualizar superblock
// ─────────────────────────────────────────────────────────────
static void updateSuperblock(std::fstream& file, int partStart, Superblock& sb) {
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
//  cmdCopy
// ─────────────────────────────────────────────────────────────
std::string cmdCopy(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path")    == p.end()) return "ERROR: falta -path";
    if (p.find("destino") == p.end()) return "ERROR: falta -destino";

    std::string srcPath  = p.at("path");
    std::string destPath = p.at("destino");

    if (srcPath.empty()  || srcPath[0]  != '/') return "ERROR: -path debe iniciar con '/'";
    if (destPath.empty() || destPath[0] != '/') return "ERROR: -destino debe iniciar con '/'";

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

    // Verificar permiso de lectura sobre el origen
    Inode srcInode = readInode(file, sb, srcTarget);
    if (!hasReadPerm(srcInode, activeSession)) {
        file.close();
        return "ERROR: sin permiso de lectura sobre: " + srcPath;
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

    // ── Copiar ────────────────────────────────────────────────
    int newInode = copyInode(file, sb, srcTarget, destDirInode, srcName, activeSession);
    if (newInode == -1) {
        file.close();
        return "ERROR: no hay espacio suficiente para la copia";
    }

    // ── Actualizar superblock ─────────────────────────────────
    updateSuperblock(file, partStart, sb);

    // Registrar en journal si es EXT3
    writeJournal(file, sb, "copy", destPath + "/" + srcName);

    file.close();
    return "SUCCESS: copiado\n"
           "  origen  : " + srcPath + "\n"
           "  destino : " + destPath + "/" + srcName;
}