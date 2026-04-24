#include "mkdir.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"
#include "../commandsP2/journal.h"

#include <fstream>
#include <cstring>
#include <ctime>
#include <filesystem>

// ── Buscar inodo libre ────────────────────────────────────────
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

// ── Buscar bloque libre ───────────────────────────────────────
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

// ── Insertar entrada en carpeta padre (con expansión de bloques) ──
static bool insertInFolder(std::fstream& file, const Superblock& sb,
                            int parentInodeIdx, const std::string& name, int childInodeIdx) {
    Inode parent;
    file.seekg(sb.s_inode_start + parentInodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&parent), sizeof(Inode));

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

    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] != -1) continue;
        int newBlock = allocBlock(file, sb);
        if (newBlock == -1) return false;

        FolderBlock fb;
        memset(&fb, 0, sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) fb.b_content[j].b_inodo = -1;

        memset(fb.b_content[0].b_name, 0, 24);
        memcpy(fb.b_content[0].b_name, name.c_str(),
               std::min((int)name.size(), 23));
        fb.b_content[0].b_inodo = childInodeIdx;

        file.seekp(sb.s_block_start + newBlock * sb.s_block_s);
        file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        parent.i_block[b] = newBlock;
        file.seekp(sb.s_inode_start + parentInodeIdx * sb.s_inode_s);
        file.write(reinterpret_cast<char*>(&parent), sizeof(Inode));

        return true;
    }
    return false;
}

// ── Crear carpeta en EXT2 ─────────────────────────────────────
static int createFolder(std::fstream& file, const Superblock& sb,
                         int parentInodeIdx, const std::string& name,
                         int uid, int gid) {
    int newInode = allocInode(file, sb);
    if (newInode == -1) return -1;

    int newBlock = allocBlock(file, sb);
    if (newBlock == -1) return -1;

    Inode inode;
    inode.i_uid   = uid;
    inode.i_gid   = gid;
    inode.i_s     = 0;
    inode.i_atime = (int32_t)time(nullptr);
    inode.i_ctime = (int32_t)time(nullptr);
    inode.i_mtime = (int32_t)time(nullptr);
    inode.i_type  = '0';
    memcpy(inode.i_perm, "755", 3);
    for (int i = 0; i < 15; i++) inode.i_block[i] = -1;
    inode.i_block[0] = newBlock;

    file.seekp(sb.s_inode_start + newInode * sb.s_inode_s);
    file.write(reinterpret_cast<char*>(&inode), sizeof(Inode));

    FolderBlock fb;
    memset(&fb, 0, sizeof(FolderBlock));
    memcpy(fb.b_content[0].b_name, ".",  1);
    fb.b_content[0].b_inodo = newInode;
    memcpy(fb.b_content[1].b_name, "..", 2);
    fb.b_content[1].b_inodo = parentInodeIdx;
    fb.b_content[2].b_inodo = -1;
    fb.b_content[3].b_inodo = -1;

    file.seekp(sb.s_block_start + newBlock * sb.s_block_s);
    file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

    insertInFolder(file, sb, parentInodeIdx, name, newInode);

    return newInode;
}

// ── Buscar nombre en directorio, retorna inodo hijo ───────────
static int findInDir(std::fstream& file, const Superblock& sb,
                     int inodeIdx, const char* name) {
    Inode cur;
    file.seekg(sb.s_inode_start + inodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&cur), sizeof(Inode));

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

std::string cmdMkdir(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path") == p.end())
        return "ERROR: falta -path";

    std::string path = p.at("path");
    if (path.empty() || path[0] != '/')
        return "ERROR: -path debe iniciar con '/'";

    bool createParents = (p.find("p") != p.end());

    MountedPartition& mp = mountedPartitions[activeSession.partId];
    std::fstream file(mp.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco";

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

    // ── Recorrer path con strtok (sin vectores) ───────────────
    int currentInode  = 0;
    std::string created     = "";
    std::string accumulated = "";

    // Contar segmentos para identificar el último
    int totalSegments = 0;
    {
        char tmp[512];
        strncpy(tmp, path.c_str(), sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';
        char* t = strtok(tmp, "/");
        while (t) { totalSegments++; t = strtok(nullptr, "/"); }
    }

    char pathBuf[512];
    strncpy(pathBuf, path.c_str(), sizeof(pathBuf)-1);
    pathBuf[sizeof(pathBuf)-1] = '\0';

    int segIdx = 0;
    char* token = strtok(pathBuf, "/");
    while (token != nullptr) {
        accumulated += "/";
        accumulated += token;
        segIdx++;

        int found = findInDir(file, sb, currentInode, token);

        if (found != -1) {
            currentInode = found;
            token = strtok(nullptr, "/");
            continue;
        }

        bool isLast = (segIdx == totalSegments);
        if (!isLast && !createParents) {
            file.close();
            return "ERROR: directorio padre no existe: " + accumulated +
                   "\n  (usa -p para crear padres automáticamente)";
        }

        std::string name(token);
        int newInode = createFolder(file, sb, currentInode, name,
                                    activeSession.uid, activeSession.gid);
        if (newInode == -1) {
            file.close();
            return "ERROR: no hay espacio para crear: " + accumulated;
        }

        try {
            std::filesystem::create_directories("." + accumulated);
        } catch (...) {}

        created += "  creado : " + accumulated + "\n";
        writeJournal(file, sb, "mkdir", accumulated);
        currentInode = newInode;

        token = strtok(nullptr, "/");
    }

    file.close();

    if (created.empty())
        return "ERROR: el directorio ya existe: " + path;

    return "SUCCESS: directorio(s) creado(s)\n" + created;
}