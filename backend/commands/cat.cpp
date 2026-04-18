#include "cat.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>

std::string cmdCat(const std::map<std::string,std::string>& p) {

// ── Validar sesión ────────────────────────────────────────
    if (!activeSession.active)
        return "ERROR: no hay sesión activa";

    std::string path = "";
    if (p.find("file") != p.end())
        path = p.at("file");
    else if (p.find("file1") != p.end())
        path = p.at("file1");
    else if (p.find("file2") != p.end())
        path = p.at("file2");
    else if (p.find("file3") != p.end())
        path = p.at("file3");

    if (path.empty())
        return "ERROR: falta -file";
    if (path[0] != '/')
        return "ERROR: -file debe iniciar con '/'";
    
        
    // ── Obtener partición ─────────────────────────────────────
    MountedPartition& mp = mountedPartitions[activeSession.partId];

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
        if (pid == activeSession.partId) {
            partStart = mbr.mbr_partitions[i].part_start;
            break;
        }
    }
    if (partStart == -1) { file.close(); return "ERROR: partición no encontrada"; }

    Superblock sb;
    file.seekg(partStart);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    if (sb.s_magic != 0xEF53) {
        file.close();
        return "ERROR: partición no formateada";
    }

    // ── Separar path ──────────────────────────────────────────
    std::istringstream ss(path);
    std::string token;
    std::vector<std::string> parts;
    while (std::getline(ss, token, '/'))
        if (!token.empty()) parts.push_back(token);

    if (parts.empty()) { file.close(); return "ERROR: path inválido"; }

    std::string filename = parts.back();
    parts.pop_back();

    // ── Navegar hasta directorio padre ────────────────────────
    int currentInode = 0;
    for (const std::string& dir : parts) {
        Inode cur;
        file.seekg(sb.s_inode_start + currentInode * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&cur), sizeof(Inode));

        int found = -1;
        for (int b = 0; b < 12; b++) {
            if (cur.i_block[b] == -1) continue;
            FolderBlock fb;
            file.seekg(sb.s_block_start + cur.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1 &&
                    std::string(fb.b_content[j].b_name) == dir) {
                    found = fb.b_content[j].b_inodo;
                    break;
                }
            }
            if (found != -1) break;
        }
        if (found == -1) {
            file.close();
            return "ERROR: directorio no existe: " + dir;
        }
        currentInode = found;
    }

    // ── Buscar archivo en directorio actual ───────────────────
    Inode cur;
    file.seekg(sb.s_inode_start + currentInode * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&cur), sizeof(Inode));

    int fileInodeIdx = -1;
    for (int b = 0; b < 12; b++) {
        if (cur.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + cur.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo != -1 &&
                std::string(fb.b_content[j].b_name) == filename) {
                fileInodeIdx = fb.b_content[j].b_inodo;
                break;
            }
        }
        if (fileInodeIdx != -1) break;
    }

    if (fileInodeIdx == -1) {
        file.close();
        return "ERROR: archivo no encontrado: " + filename;
    }

    // ── Leer inodo del archivo ────────────────────────────────
    Inode fileInode;
    file.seekg(sb.s_inode_start + fileInodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&fileInode), sizeof(Inode));

    if (fileInode.i_type != '1') {
        file.close();
        return "ERROR: '" + filename + "' es un directorio, no un archivo";
    }

    // ── Leer contenido ────────────────────────────────────────
    std::string content = "";
    for (int b = 0; b < 12; b++) {
        if (fileInode.i_block[b] == -1) break;
        FileBlock fb;
        file.seekg(sb.s_block_start + fileInode.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        content += std::string(fb.b_content, 64);
    }

    file.close();

    // Recortar al tamaño real
    if (fileInode.i_s > 0 && fileInode.i_s <= (int)content.size())
        content = content.substr(0, fileInode.i_s);

    return "SUCCESS: contenido de " + path + "\n" +
           "─────────────────────\n" +
           content;
}