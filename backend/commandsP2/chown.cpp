#include "chown.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"
#include "../commandsP2/journal.h"

#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

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
                strncmp(fb.b_content[j].b_name, name, 24) == 0)
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

// Leer contenido de users.txt para buscar uid de un usuario
// Formato: uid,U,username,group,pass  o  gid,G,groupname
static int getUidFromUsers(std::fstream& file, const Superblock& sb,
                            const std::string& username) {
    // users.txt está en inodo 1
    Inode usersInode = readInode(file, sb, 1);
    if (usersInode.i_type != '1') return -1;

    std::string content;
    for (int b = 0; b < 12; b++) {
        if (usersInode.i_block[b] == -1) continue;
        FileBlock fb;
        file.seekg(sb.s_block_start + usersInode.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        content += std::string(fb.b_content, strnlen(fb.b_content, 64));
    }

    // Parsear línea por línea
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        // Tokenizar por coma
        std::vector<std::string> tokens;
        std::string tok;
        for (char c : line) {
            if (c == ',') { tokens.push_back(tok); tok.clear(); }
            else tok += c;
        }
        tokens.push_back(tok);

        // uid,U,username,group,pass
        if (tokens.size() >= 3 && tokens[1] == "U" && tokens[2] == username) {
            return std::stoi(tokens[0]);
        }
    }
    return -1; // usuario no encontrado
}

// Aplicar chown recursivamente a partir de un inodo
static void applyChown(std::fstream& file, const Superblock& sb,
                        int inodeIdx, int newUid, bool recursive,
                        const Session& session) {

    Inode inode = readInode(file, sb, inodeIdx);

    // Solo root o propietario pueden cambiar dueño
    if (session.uid == 1 || inode.i_uid == session.uid) {
        inode.i_uid = newUid;
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
            if (strncmp(fb.b_content[j].b_name, "..", 24) == 0) continue;
            applyChown(file, sb, childIdx, newUid, recursive, session);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  cmdChown
// ─────────────────────────────────────────────────────────────
std::string cmdChown(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path")    == p.end()) return "ERROR: falta -path";
    if (p.find("usuario") == p.end()) return "ERROR: falta -usuario";

    std::string path     = p.at("path");
    std::string username = p.at("usuario");
    bool recursive       = (p.find("r") != p.end());

    if (path.empty() || path[0] != '/')
        return "ERROR: -path debe iniciar con '/'";
    if (username.empty())
        return "ERROR: -usuario no puede estar vacío";

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

    // ── Buscar uid del nuevo propietario en users.txt ─────────
    int newUid = getUidFromUsers(file, sb, username);
    if (newUid == -1) {
        file.close();
        return "ERROR: usuario no existe: " + username;
    }

    // ── Resolver inodo del path ───────────────────────────────
    int targetInode = resolvePath(file, sb, path);
    if (targetInode == -1) {
        file.close();
        return "ERROR: no existe la ruta: " + path;
    }

    // ── Verificar que el usuario actual puede cambiar el dueño ─
    // Solo root o el propietario actual pueden usar chown
    Inode inode = readInode(file, sb, targetInode);
    if (activeSession.uid != 1 && inode.i_uid != activeSession.uid) {
        file.close();
        return "ERROR: solo root o el propietario pueden cambiar el dueño de: " + path;
    }

    // ── Aplicar chown ─────────────────────────────────────────
    applyChown(file, sb, targetInode, newUid, recursive, activeSession);

    // Registrar en journal si es EXT3
    writeJournal(file, sb, "chown", path);

    file.close();

    std::string msg = "SUCCESS: propietario cambiado a '" + username + "'\n"
                      "  path      : " + path + "\n"
                      "  recursivo : ";
    msg += recursive ? "sí" : "no";
    return msg;
}