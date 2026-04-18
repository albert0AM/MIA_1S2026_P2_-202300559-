#include "users.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <ctime>

// ── Leer users.txt completo ───────────────────────────────────
static std::string readUsers(const std::string& diskPath,
                              const Superblock& sb) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "";

    // Leer inodo root
    Inode root;
    file.seekg(sb.s_inode_start);
    file.read(reinterpret_cast<char*>(&root), sizeof(Inode));

    // Buscar users.txt en bloque root
    int usersInodeIdx = -1;
    for (int b = 0; b < 12; b++) {
        if (root.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + root.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (std::string(fb.b_content[j].b_name) == "users.txt") {
                usersInodeIdx = fb.b_content[j].b_inodo;
                break;
            }
        }
        if (usersInodeIdx != -1) break;
    }
    if (usersInodeIdx == -1) { file.close(); return ""; }

    Inode usersInode;
    file.seekg(sb.s_inode_start + usersInodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    std::string content;
    for (int b = 0; b < 12; b++) {
        if (usersInode.i_block[b] == -1) break;
        FileBlock fb;
        file.seekg(sb.s_block_start + usersInode.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        content += std::string(fb.b_content, 64);
    }
    file.close();
    return content.substr(0, usersInode.i_s);
}

// ── Escribir users.txt completo ───────────────────────────────
static bool writeUsers(const std::string& diskPath,
                        const Superblock& sb, int partStart,
                        const std::string& content) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) return false;

    // Leer bitmap de bloques
    std::vector<char> bm(sb.s_blocks_count);
    file.seekg(sb.s_bm_block_start);
    file.read(bm.data(), sb.s_blocks_count);

    // Leer inodo root
    Inode root;
    file.seekg(sb.s_inode_start);
    file.read(reinterpret_cast<char*>(&root), sizeof(Inode));

    // Buscar inodo de users.txt
    int usersInodeIdx = -1;
    for (int b = 0; b < 12; b++) {
        if (root.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + root.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (std::string(fb.b_content[j].b_name) == "users.txt") {
                usersInodeIdx = fb.b_content[j].b_inodo;
                break;
            }
        }
        if (usersInodeIdx != -1) break;
    }
    if (usersInodeIdx == -1) { file.close(); return false; }

    Inode usersInode;
    file.seekg(sb.s_inode_start + usersInodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    // Calcular bloques necesarios y asignar nuevos si hace falta
    int blocksNeeded = ((int)content.size() + 63) / 64;
    if (blocksNeeded == 0) blocksNeeded = 1;

    for (int b = 0; b < blocksNeeded && b < 12; b++) {
        if (usersInode.i_block[b] == -1) {
            int freeBlock = -1;
            for (int i = 0; i < sb.s_blocks_count; i++) {
                if (bm[i] == '0') { freeBlock = i; break; }
            }
            if (freeBlock == -1) { file.close(); return false; }
            usersInode.i_block[b] = freeBlock;
            bm[freeBlock] = '1';
            file.seekp(sb.s_bm_block_start + freeBlock);
            file.write("1", 1);
        }
    }

    // Escribir contenido en bloques
    size_t written = 0;
    for (int b = 0; b < blocksNeeded && b < 12; b++) {
        FileBlock fb;
        memset(&fb, 0, sizeof(FileBlock));
        size_t chunk = std::min((size_t)64, content.size() - written);
        memcpy(fb.b_content, content.c_str() + written, chunk);
        file.seekp(sb.s_block_start + usersInode.i_block[b] * sb.s_block_s);
        file.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        written += chunk;
    }

    // Actualizar inodo
    usersInode.i_s = (int32_t)content.size();
    usersInode.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + usersInodeIdx * sb.s_inode_s);
    file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    file.close();
    return true;
}

// ── Helper: obtener superblock ────────────────────────────────
static bool getSuperblock(const std::string& diskPath, int partStart,
                           Superblock& sb) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return false;
    file.seekg(partStart);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    file.close();
    return sb.s_magic == 0xEF53;
}

// ── Helper: obtener partStart desde id ───────────────────────
static int getPartStart(const std::string& diskPath, const std::string& id) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return -1;
    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.close();
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) return mbr.mbr_partitions[i].part_start;
    }
    return -1;
}

// ═════════════════════════════════════════════════════════════
//  MKGRP
// ═════════════════════════════════════════════════════════════
std::string cmdMkgrp(const std::map<std::string,std::string>& p) {
    if (!activeSession.active)  return "ERROR: no hay sesión activa";
    if (activeSession.username != "root") return "ERROR: solo root puede crear grupos";
    if (p.find("name") == p.end()) return "ERROR: falta -name";

    std::string name = p.at("name");
    if (name.size() > 10) return "ERROR: nombre máximo 10 caracteres";

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    int partStart = getPartStart(mp.path, activeSession.partId);
    if (partStart == -1) return "ERROR: no se encontró la partición";

    Superblock sb;
    if (!getSuperblock(mp.path, partStart, sb))
        return "ERROR: partición no formateada";

    std::string content = readUsers(mp.path, sb);

    // Verificar que no exista
    std::istringstream ss(content);
    std::string line;
    int maxGid = 0;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));
        if (parts.size() == 3 && parts[1] == "G") {
            if (parts[2] == name && parts[0] != "0")
                return "ERROR: el grupo '" + name + "' ya existe";
            if (parts[0] != "0")
                maxGid = std::max(maxGid, std::stoi(parts[0]));
        }
    }

    int newGid = maxGid + 1;
    content += std::to_string(newGid) + ",G," + name + "\n";

    if (!writeUsers(mp.path, sb, partStart, content))
        return "ERROR: no se pudo escribir users.txt";

    return "SUCCESS: grupo creado\n"
           "  nombre : " + name + "\n"
           "  gid    : " + std::to_string(newGid);
}

// ═════════════════════════════════════════════════════════════
//  RMGRP
// ═════════════════════════════════════════════════════════════
std::string cmdRmgrp(const std::map<std::string,std::string>& p) {
    if (!activeSession.active) return "ERROR: no hay sesión activa";
    if (activeSession.username != "root") return "ERROR: solo root puede eliminar grupos";
    if (p.find("name") == p.end()) return "ERROR: falta -name";

    std::string name = p.at("name");
    if (name == "root") return "ERROR: no se puede eliminar el grupo root";

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    int partStart = getPartStart(mp.path, activeSession.partId);
    Superblock sb;
    if (!getSuperblock(mp.path, partStart, sb))
        return "ERROR: partición no formateada";

    std::string content = readUsers(mp.path, sb);
    std::istringstream ss(content);
    std::string line, newContent;
    bool found = false;

    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) { newContent += "\n"; continue; }
        std::vector<std::string> parts;
        std::istringstream ls(trimmed);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));
        if (parts.size() == 3 && parts[1] == "G" && parts[2] == name) {
            newContent += "0,G," + name + "\n";
            found = true;
        } else {
            newContent += trimmed + "\n";
        }
    }

    if (!found) return "ERROR: grupo '" + name + "' no encontrado";
    if (!writeUsers(mp.path, sb, partStart, newContent))
        return "ERROR: no se pudo escribir users.txt";

    return "SUCCESS: grupo eliminado\n  nombre : " + name;
}

// ═════════════════════════════════════════════════════════════
//  MKUSR
// ═════════════════════════════════════════════════════════════
std::string cmdMkusr(const std::map<std::string,std::string>& p) {
    if (!activeSession.active) return "ERROR: no hay sesión activa";
    if (activeSession.username != "root") return "ERROR: solo root puede crear usuarios";
    if (p.find("user") == p.end()) return "ERROR: falta -user";
    if (p.find("pass") == p.end()) return "ERROR: falta -pass";
    if (p.find("grp")  == p.end()) return "ERROR: falta -grp";

    std::string user = p.at("user");
    std::string pass = p.at("pass");
    std::string grp  = p.at("grp");

    if (user.size() > 10) return "ERROR: usuario máximo 10 caracteres";
    if (pass.size() > 10) return "ERROR: contraseña máximo 10 caracteres";
    if (grp.size()  > 10) return "ERROR: grupo máximo 10 caracteres";

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    int partStart = getPartStart(mp.path, activeSession.partId);
    Superblock sb;
    if (!getSuperblock(mp.path, partStart, sb))
        return "ERROR: partición no formateada";

    std::string content = readUsers(mp.path, sb);
    std::istringstream ss(content);
    std::string line;
    int maxUid = 0, grpGid = -1;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));

        if (parts.size() == 3 && parts[1] == "G" && parts[2] == grp && parts[0] != "0")
            grpGid = std::stoi(parts[0]);

        if (parts.size() == 5 && parts[1] == "U") {
            if (parts[3] == user && parts[0] != "0")
                return "ERROR: el usuario '" + user + "' ya existe";
            if (parts[0] != "0")
                maxUid = std::max(maxUid, std::stoi(parts[0]));
        }
    }

    if (grpGid == -1) return "ERROR: el grupo '" + grp + "' no existe";

    int newUid = maxUid + 1;
    content += std::to_string(newUid) + ",U," + grp + "," + user + "," + pass + "\n";

    if (!writeUsers(mp.path, sb, partStart, content))
        return "ERROR: no se pudo escribir users.txt";

    return "SUCCESS: usuario creado\n"
           "  usuario : " + user + "\n"
           "  grupo   : " + grp + "\n"
           "  uid     : " + std::to_string(newUid);
}

// ═════════════════════════════════════════════════════════════
//  RMUSR
// ═════════════════════════════════════════════════════════════
std::string cmdRmusr(const std::map<std::string,std::string>& p) {
    if (!activeSession.active) return "ERROR: no hay sesión activa";
    if (activeSession.username != "root") return "ERROR: solo root puede eliminar usuarios";
    if (p.find("user") == p.end()) return "ERROR: falta -user";

    std::string user = p.at("user");
    if (user == "root") return "ERROR: no se puede eliminar el usuario root";

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    int partStart = getPartStart(mp.path, activeSession.partId);
    Superblock sb;
    if (!getSuperblock(mp.path, partStart, sb))
        return "ERROR: partición no formateada";

    std::string content = readUsers(mp.path, sb);
    std::istringstream ss(content);
    std::string line, newContent;
    bool found = false;

    while (std::getline(ss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) { newContent += "\n"; continue; }
        std::vector<std::string> parts;
        std::istringstream ls(trimmed);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));
        if (parts.size() == 5 && parts[1] == "U" && parts[3] == user) {
            newContent += "0,U," + parts[2] + "," + user + "," + parts[4] + "\n";
            found = true;
        } else {
            newContent += trimmed + "\n";
        }
    }

    if (!found) return "ERROR: usuario '" + user + "' no encontrado";
    if (!writeUsers(mp.path, sb, partStart, newContent))
        return "ERROR: no se pudo escribir users.txt";

    return "SUCCESS: usuario eliminado\n  usuario : " + user;
}

// ═════════════════════════════════════════════════════════════
//  CHGRP
// ═════════════════════════════════════════════════════════════
std::string cmdChgrp(const std::map<std::string,std::string>& p) {
    if (!activeSession.active) return "ERROR: no hay sesión activa";
    if (activeSession.username != "root") return "ERROR: solo root puede cambiar grupos";
    if (p.find("user") == p.end()) return "ERROR: falta -user";
    if (p.find("grp")  == p.end()) return "ERROR: falta -grp";

    std::string user = p.at("user");
    std::string grp  = p.at("grp");

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    int partStart = getPartStart(mp.path, activeSession.partId);
    Superblock sb;
    if (!getSuperblock(mp.path, partStart, sb))
        return "ERROR: partición no formateada";

    std::string content = readUsers(mp.path, sb);
    std::istringstream ss(content);
    std::string line;
    bool grpExists = false;

    while (std::getline(ss, line)) {
        line = trim(line);
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));
        if (parts.size() == 3 && parts[1] == "G" && parts[2] == grp && parts[0] != "0") {
            grpExists = true; break;
        }
    }
    if (!grpExists) return "ERROR: el grupo '" + grp + "' no existe";

    std::istringstream ss2(content);
    std::string newContent;
    bool found = false;

    while (std::getline(ss2, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) { newContent += "\n"; continue; }
        std::vector<std::string> parts;
        std::istringstream ls(trimmed);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));
        if (parts.size() == 5 && parts[1] == "U" && parts[3] == user) {
            newContent += parts[0] + ",U," + grp + "," + user + "," + parts[4] + "\n";
            found = true;
        } else {
            newContent += trimmed + "\n";
        }
    }

    if (!found) return "ERROR: usuario '" + user + "' no encontrado";
    if (!writeUsers(mp.path, sb, partStart, newContent))
        return "ERROR: no se pudo escribir users.txt";

    return "SUCCESS: grupo cambiado\n"
           "  usuario : " + user + "\n"
           "  grupo   : " + grp;
}