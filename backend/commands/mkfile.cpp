#include "mkfile.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <ctime>
#include <filesystem>

// ── Buscar inodo libre ────────────────────────────────────────
static int allocInodeF(std::fstream& file, const Superblock& sb) {
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
static int allocBlockF(std::fstream& file, const Superblock& sb) {
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

// ── Buscar nombre en directorio, retorna inodo hijo ───────────
static int findInDirF(std::fstream& file, const Superblock& sb,
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
                strncmp(fb.b_content[j].b_name, name, 12) == 0)
                return fb.b_content[j].b_inodo;
        }
    }
    return -1;
}

// ── Insertar en carpeta (con expansión de bloques) ────────────
static bool insertEntry(std::fstream& file, const Superblock& sb,
                         int parentInode, const std::string& name, int childInode) {
    Inode parent;
    file.seekg(sb.s_inode_start + parentInode * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&parent), sizeof(Inode));

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
                fb.b_content[j].b_inodo = childInode;
                file.seekp(sb.s_block_start + parent.i_block[b] * sb.s_block_s);
                file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
                return true;
            }
        }
    }

    for (int b = 0; b < 12; b++) {
        if (parent.i_block[b] != -1) continue;
        int newBlock = allocBlockF(file, sb);
        if (newBlock == -1) return false;

        FolderBlock fb;
        memset(&fb, 0, sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) fb.b_content[j].b_inodo = -1;

        memset(fb.b_content[0].b_name, 0, 12);
        memcpy(fb.b_content[0].b_name, name.c_str(),
               std::min((int)name.size(), 11));
        fb.b_content[0].b_inodo = childInode;

        file.seekp(sb.s_block_start + newBlock * sb.s_block_s);
        file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

        parent.i_block[b] = newBlock;
        file.seekp(sb.s_inode_start + parentInode * sb.s_inode_s);
        file.write(reinterpret_cast<char*>(&parent), sizeof(Inode));

        return true;
    }
    return false;
}

// ── Crear directorio en EXT2 ──────────────────────────────────
static int createDirF(std::fstream& file, const Superblock& sb,
                       int parentInode, const char* name, int uid, int gid) {
    int ni = allocInodeF(file, sb);
    int nb = allocBlockF(file, sb);
    if (ni == -1 || nb == -1) return -1;

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
    inode.i_block[0] = nb;

    file.seekp(sb.s_inode_start + ni * sb.s_inode_s);
    file.write(reinterpret_cast<char*>(&inode), sizeof(Inode));

    FolderBlock fb;
    memset(&fb, 0, sizeof(FolderBlock));
    memcpy(fb.b_content[0].b_name, ".",  1);
    fb.b_content[0].b_inodo = ni;
    memcpy(fb.b_content[1].b_name, "..", 2);
    fb.b_content[1].b_inodo = parentInode;
    fb.b_content[2].b_inodo = -1;
    fb.b_content[3].b_inodo = -1;

    file.seekp(sb.s_block_start + nb * sb.s_block_s);
    file.write(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

    insertEntry(file, sb, parentInode, std::string(name), ni);
    return ni;
}

std::string cmdMkfile(const std::map<std::string,std::string>& p) {

    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    if (p.find("path") == p.end())
        return "ERROR: falta -path";

    std::string path = p.at("path");
    if (path.empty() || path[0] != '/')
        return "ERROR: -path debe iniciar con '/'";

    bool createParents = (p.find("p") != p.end() || p.find("r") != p.end());

    int size = 0;
    if (p.find("size") != p.end()) {
        size = std::stoi(p.at("size"));
        if (size < 0)
            return "ERROR: -size no puede ser negativo";
    }

    std::string cont = "";
    if (p.find("cont") != p.end())
        cont = p.at("cont");

    // Extraer nombre de archivo y path del padre sin vectores
    // Encontrar último '/'
    size_t lastSlash = path.find_last_of('/');
    std::string parentPath = path.substr(0, lastSlash);   // ej: /home/archivos/user/docs
    std::string filename   = path.substr(lastSlash + 1);  // ej: Tarea.txt

    if (filename.empty())
        return "ERROR: path inválido";

    // Obtener partición
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

    // Navegar/crear directorio padre con strtok (sin vectores)
    int parentInode = 0;

    if (!parentPath.empty()) {
        // Primero intentar navegar
        char navBuf[512];
        strncpy(navBuf, parentPath.c_str(), sizeof(navBuf)-1);
        navBuf[sizeof(navBuf)-1] = '\0';

        int tmpInode = 0;
        bool navOk = true;
        char* tok = strtok(navBuf, "/");
        while (tok != nullptr) {
            int found = findInDirF(file, sb, tmpInode, tok);
            if (found == -1) { navOk = false; break; }
            tmpInode = found;
            tok = strtok(nullptr, "/");
        }

        if (navOk) {
            parentInode = tmpInode;
        } else {
            if (!createParents) {
                file.close();
                return "ERROR: directorio padre no existe (usa -p)";
            }
            // Crear padres con strtok
            char mkBuf[512];
            strncpy(mkBuf, parentPath.c_str(), sizeof(mkBuf)-1);
            mkBuf[sizeof(mkBuf)-1] = '\0';

            parentInode = 0;
            char* t = strtok(mkBuf, "/");
            while (t != nullptr) {
                int found = findInDirF(file, sb, parentInode, t);
                if (found != -1) {
                    parentInode = found;
                } else {
                    int ni = createDirF(file, sb, parentInode, t,
                                        activeSession.uid, activeSession.gid);
                    if (ni == -1) { file.close(); return "ERROR: no hay espacio"; }
                    parentInode = ni;
                }
                t = strtok(nullptr, "/");
            }
        }
    }

    // Generar contenido
    std::string content = "";
    if (size > 0) {
        for (int i = 0; i < size; i++)
            content += (char)('0' + (i % 10));
    }
    if (!cont.empty()) {
        std::ifstream contFile(cont);
        if (contFile.is_open()) {
            char line[256];
            while (contFile.getline(line, sizeof(line)))
                content += std::string(line) + "\n";
            contFile.close();
        } else {
            content = cont;
        }
    }

    // Crear inodo del archivo
    int newInode = allocInodeF(file, sb);
    if (newInode == -1) { file.close(); return "ERROR: no hay inodos libres"; }

    Inode fileInode;
    fileInode.i_uid   = activeSession.uid;
    fileInode.i_gid   = activeSession.gid;
    fileInode.i_s     = (int32_t)content.size();
    fileInode.i_atime = (int32_t)time(nullptr);
    fileInode.i_ctime = (int32_t)time(nullptr);
    fileInode.i_mtime = (int32_t)time(nullptr);
    fileInode.i_type  = '1';
    memcpy(fileInode.i_perm, "664", 3);
    for (int i = 0; i < 15; i++) fileInode.i_block[i] = -1;

    // Escribir bloques
    size_t written = 0;
    int blockIdx = 0;
    while (written < content.size() && blockIdx < 12) {
        int nb = allocBlockF(file, sb);
        if (nb == -1) break;
        FileBlock fb;
        memset(&fb, 0, sizeof(FileBlock));
        size_t chunk = std::min((size_t)64, content.size() - written);
        memcpy(fb.b_content, content.c_str() + written, chunk);
        file.seekp(sb.s_block_start + nb * sb.s_block_s);
        file.write(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        fileInode.i_block[blockIdx++] = nb;
        written += chunk;
    }

    file.seekp(sb.s_inode_start + newInode * sb.s_inode_s);
    file.write(reinterpret_cast<char*>(&fileInode), sizeof(Inode));

    // Insertar en carpeta padre
    if (!insertEntry(file, sb, parentInode, filename, newInode)) {
        file.close();
        return "ERROR: no se pudo insertar en directorio padre";
    }

    // Crear archivo físico
    try {
        std::string realPath = "." + path;
        std::filesystem::create_directories(
            std::filesystem::path(realPath).parent_path()
        );
        std::ofstream realFile(realPath);
        if (realFile.is_open()) {
            realFile << content;
            realFile.close();
        }
    } catch (...) {}

    file.close();

    return "SUCCESS: archivo creado\n"
           "  path    : " + path + "\n"
           "  tamaño  : " + std::to_string(content.size()) + " bytes\n"
           "  bloques : " + std::to_string(blockIdx);
}