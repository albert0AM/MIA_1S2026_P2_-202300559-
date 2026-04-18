#include "login.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>

// ── Leer contenido de users.txt desde la partición ───────────
static std::string readUsersFile(const std::string &diskPath, int partStart,
                                 int blockStart, int inodeStart, int inodeS,
                                 int blockS)
{
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open())
        return "";

    // Leer inodo root (inodo 0)
    Inode root;
    file.seekg(inodeStart);
    file.read(reinterpret_cast<char*>(&root), sizeof(Inode));

    // Buscar users.txt en el bloque root
    int usersInodeIdx = -1;
    for (int b = 0; b < 12; b++) {
        if (root.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(blockStart + root.i_block[b] * blockS);
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

    // Leer inodo de users.txt
    Inode usersInode;
    file.seekg(inodeStart + usersInodeIdx * inodeS);
    file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    // Leer contenido
    std::string content;
    for (int b = 0; b < 12; b++) {
        if (usersInode.i_block[b] == -1) break;
        FileBlock fb;
        file.seekg(blockStart + usersInode.i_block[b] * blockS);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        content += std::string(fb.b_content, 64);
    }

    file.close();
    // Recortar al tamaño real
    content = content.substr(0, usersInode.i_s);
    return content;
}

std::string cmdLogin(const std::map<std::string,std::string>& p) {

    // ── Validar parámetros ────────────────────────────────────
    if (p.find("user") == p.end()) return "ERROR: falta -user";
    if (p.find("pass") == p.end()) return "ERROR: falta -pass";
    if (p.find("id")   == p.end()) return "ERROR: falta -id";

    std::string user = p.at("user");
    std::string pass = p.at("pass");
    std::string id   = p.at("id");

    // ── Verificar sesión activa ───────────────────────────────
    if (activeSession.active)
        return "ERROR: ya hay una sesión activa (" + activeSession.username + ")";

    // ── Buscar partición montada ──────────────────────────────
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: no existe partición montada con id: " + id;

    MountedPartition& mp = mountedPartitions[id];

    // ── Leer MBR para obtener datos de la partición ───────────
    std::fstream file(mp.path, std::ios::binary | std::ios::in);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco";

    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    file.close();

    if (partStart == -1)
        return "ERROR: no se encontró la partición en el disco";

    // ── Leer Superblock ───────────────────────────────────────
    std::fstream file2(mp.path, std::ios::binary | std::ios::in);
    Superblock sb;
    file2.seekg(partStart);
    file2.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    file2.close();

    if (sb.s_magic != 0xEF53)
        return "ERROR: la partición no está formateada (MKFS)";

    // ── Leer users.txt ────────────────────────────────────────
    std::string content = readUsersFile(mp.path, partStart,
                                         sb.s_block_start, sb.s_inode_start,
                                         sb.s_inode_s, sb.s_block_s);
    if (content.empty())
        return "ERROR: no se pudo leer users.txt";

    // ── Parsear users.txt y buscar usuario ────────────────────
    // Formato: "UID,U,grupo,usuario,contraseña"
    std::istringstream ss(content);
    std::string line;
    bool found = false;
    int  uid = -1, gid = -1;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // Separar por comas
        std::vector<std::string> parts;
        std::istringstream ls(line);
        std::string token;
        while (std::getline(ls, token, ','))
            parts.push_back(trim(token));

        // Línea de usuario: UID, U, grupo, usuario, contraseña
        if (parts.size() == 5 && parts[1] == "U") {
            if (parts[0] == "0") continue; // usuario eliminado
            if (parts[3] == user && parts[4] == pass) {
                uid   = std::stoi(parts[0]);
                // Buscar GID del grupo
                std::istringstream ss2(content);
                std::string line2;
                while (std::getline(ss2, line2)) {
                    line2 = trim(line2);
                    std::vector<std::string> p2;
                    std::istringstream ls2(line2);
                    std::string t2;
                    while (std::getline(ls2, t2, ','))
                        p2.push_back(trim(t2));
                    if (p2.size() == 3 && p2[1] == "G" && p2[2] == parts[2]) {
                        gid = std::stoi(p2[0]);
                        break;
                    }
                }
                found = true;
                break;
            }
        }
    }

    if (!found)
        return "ERROR: usuario o contraseña incorrectos";

    // ── Iniciar sesión ────────────────────────────────────────
    activeSession.active   = true;
    activeSession.username = user;
    activeSession.partId   = id;
    activeSession.uid      = uid;
    activeSession.gid      = gid;

    return "SUCCESS: sesión iniciada\n"
           "  usuario : " + user + "\n"
           "  id      : " + id + "\n"
           "  uid     : " + std::to_string(uid) + "\n"
           "  gid     : " + std::to_string(gid);
}

std::string cmdLogout() {
    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    std::string user = activeSession.username;
    activeSession.active   = false;
    activeSession.username = "";
    activeSession.partId   = "";
    activeSession.uid      = 0;
    activeSession.gid      = 0;

    return "SUCCESS: sesión cerrada\n"
           "  usuario : " + user;
}