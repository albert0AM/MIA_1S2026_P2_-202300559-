#include "chmod.h"
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

// Resolver inodo de un path
static int resolvePath(std::fstream& file, const Superblock& sb,
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
    if (!seg.empty()) parts.push_back(seg);

    int cur = 0;
    for (auto& part : parts) {
        int next = findInDir(file, sb, cur, part.c_str());
        if (next == -1) return -1;
        cur = next;
    }
    return cur;
}

// ─────────────────────────────────────────────────────────────
//  Aplicar chmod recursivamente
//  Solo cambia inodos que pertenecen al usuario activo
//  (o todos si es root)
// ─────────────────────────────────────────────────────────────
static void applyChmod(std::fstream& file, const Superblock& sb,
                        int inodeIdx, const char newPerm[3],
                        bool recursive, const Session& session) {

    Inode inode = readInode(file, sb, inodeIdx);

    // Solo cambia si es root o propietario
    if (session.uid == 1 || inode.i_uid == session.uid) {
        memcpy(inode.i_perm, newPerm, 3);
        writeInode(file, sb, inodeIdx, inode);
    }

    // Si no es recursivo o no es carpeta, terminar
    if (!recursive || inode.i_type != '0') return;

    // Recorrer hijos
    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + inode.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            int childIdx = fb.b_content[j].b_inodo;
            if (childIdx == -1) continue;
            if (strncmp(fb.b_content[j].b_name, ".",  12) == 0) continue;
            if (strncmp(fb.b_content[j].b_name, "..", 12) == 0) continue;
            applyChmod(file, sb, childIdx, newPerm, recursive, session);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  cmdChmod
// ─────────────────────────────────────────────────────────────
std::string cmdChmod(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path") == p.end()) return "ERROR: falta -path";
    if (p.find("ugo")  == p.end()) return "ERROR: falta -ugo";

    std::string path   = p.at("path");
    std::string ugoStr = p.at("ugo");
    bool recursive     = (p.find("r") != p.end());

    if (path.empty() || path[0] != '/')
        return "ERROR: -path debe iniciar con '/'";

    // ── Validar -ugo ──────────────────────────────────────────
    if (ugoStr.size() != 3)
        return "ERROR: -ugo debe tener exactamente 3 dígitos (ej: 764)";

    for (char c : ugoStr) {
        if (c < '0' || c > '7')
            return "ERROR: cada dígito de -ugo debe estar entre 0 y 7";
    }

    // Guardar como array de 3 chars (igual que i_perm)
    char newPerm[3];
    newPerm[0] = ugoStr[0]; // usuario
    newPerm[1] = ugoStr[1]; // grupo
    newPerm[2] = ugoStr[2]; // otros

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

    // ── Resolver inodo del path ───────────────────────────────
    int targetInode = resolvePath(file, sb, path);
    if (targetInode == -1) {
        file.close();
        return "ERROR: no existe la ruta: " + path;
    }

    // ── Verificar que el usuario actual es propietario o root ──
    Inode inode = readInode(file, sb, targetInode);
    if (activeSession.uid != 1 && inode.i_uid != activeSession.uid) {
        file.close();
        return "ERROR: solo root o el propietario pueden cambiar permisos de: " + path;
    }

    // ── Aplicar chmod ─────────────────────────────────────────
    applyChmod(file, sb, targetInode, newPerm, recursive, activeSession);

    file.close();

    std::string msg = "SUCCESS: permisos cambiados a " + ugoStr + "\n"
                      "  path      : " + path + "\n"
                      "  recursivo : ";
    msg += recursive ? "sí" : "no";
    return msg;
}