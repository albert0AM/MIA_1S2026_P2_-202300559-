#include "mkfs.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <ctime>
#include <cmath>

std::string cmdMkfs(const std::map<std::string,std::string>& p) {

    // ── Validar -id ───────────────────────────────────────────
    if (p.find("id") == p.end())
        return "ERROR: falta -id";
    std::string id = p.at("id");

    // ── Leer parámetro -fs (opcional, default ext2) ───────────
    std::string fs = "2fs";
    if (p.find("fs") != p.end()) {
        fs = p.at("fs");
        if (fs != "2fs" && fs != "3fs")
            return "ERROR: -fs solo acepta '2fs' o '3fs'";
    }
    bool isExt3 = (fs == "3fs");

    // ── Buscar partición montada ──────────────────────────────
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: no existe partición montada con id: " + id;

    MountedPartition& mp = mountedPartitions[id];

    // ── Abrir disco ───────────────────────────────────────────
    std::fstream file(mp.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open())
        return "ERROR: no se pudo abrir el disco: " + mp.path;

    // ── Leer MBR para encontrar la partición ──────────────────
    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    int partStart = -1;
    int partSize  = -1;
    for (int i = 0; i < 4; i++)
    {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        // quitar caracteres nulos al final
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id)
        {
            partStart = mbr.mbr_partitions[i].part_start;
            partSize  = mbr.mbr_partitions[i].part_s;
            break;
        }
    }

    if (partStart == -1) {
        file.close();
        return "ERROR: no se encontró la partición en el disco";
    }

    // ── Calcular n (número de inodos) ─────────────────────────
    int sb_s      = sizeof(Superblock);
    int inode_s   = sizeof(Inode);
    int block_s   = sizeof(FolderBlock);
    int journal_s = sizeof(Journal);

    int n;
    if (isExt3) {
        // EXT3: tamaño = sb + 50*journal + n + 3n + n*inode + 3n*block
        n = (int)floor((double)(partSize - sb_s - 50 * journal_s)
                       / (4 + inode_s + 3 * block_s));
    } else {
        // EXT2: tamaño = sb + n + 3n + n*inode + 3n*block
        n = (int)floor((double)(partSize - sb_s)
                       / (4 + inode_s + 3 * block_s));
    }

    if (n <= 0) {
        file.close();
        return "ERROR: partición demasiado pequeña para " + fs;
    }

    // ── Construir Superblock ──────────────────────────────────
    Superblock sb;
    sb.s_filesystem_type   = isExt3 ? 3 : 2;
    sb.s_inodes_count      = n;
    sb.s_blocks_count      = 3 * n;
    sb.s_free_inodes_count = n - 1;
    sb.s_free_blocks_count = 3*n - 1;
    sb.s_mtime             = (int32_t)time(nullptr);
    sb.s_umtime            = 0;
    sb.s_mnt_count         = 1;
    sb.s_magic             = 0xEF53;
    sb.s_inode_s           = inode_s;
    sb.s_block_s           = block_s;

    // EXT3 inserta 50 journals entre el superblock y el bitmap de inodos
    if (isExt3) {
        sb.s_journal_start  = partStart + sb_s;
        sb.s_bm_inode_start = sb.s_journal_start + 50 * journal_s;
    } else {
        sb.s_journal_start  = -1;
        sb.s_bm_inode_start = partStart + sb_s;
    }

    sb.s_bm_block_start = sb.s_bm_inode_start + n;
    sb.s_inode_start    = sb.s_bm_block_start + 3*n;
    sb.s_block_start    = sb.s_inode_start + n * inode_s;
    sb.s_firts_ino      = sb.s_inode_start + inode_s;
    sb.s_first_blo      = sb.s_block_start + block_s;

    // ── Escribir Superblock ───────────────────────────────────
    file.seekp(partStart);
    file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    // ── Escribir 50 journals vacíos (solo EXT3) ───────────────
    if (isExt3) {
        Journal emptyJournal;
        for (int i = 0; i < 50; i++) {
            file.seekp(sb.s_journal_start + i * journal_s);
            file.write(reinterpret_cast<char*>(&emptyJournal), journal_s);
        }
    }

    // ── Escribir bitmaps (todo en 0) ──────────────────────────
    char zero = '0';
    for (int i = 0; i < n; i++) {
        file.seekp(sb.s_bm_inode_start + i);
        file.write(&zero, 1);
    }
    for (int i = 0; i < 3*n; i++) {
        file.seekp(sb.s_bm_block_start + i);
        file.write(&zero, 1);
    }

    // ── Marcar root en bitmaps ────────────────────────────────
    char one = '1';
    file.seekp(sb.s_bm_inode_start);
    file.write(&one, 1);
    file.seekp(sb.s_bm_block_start);
    file.write(&one, 1);

    // ── Crear inodo root (inodo 0) ────────────────────────────
    Inode root;
    root.i_uid    = 1;
    root.i_gid    = 1;
    root.i_s      = 0;
    root.i_atime  = (int32_t)time(nullptr);
    root.i_ctime  = (int32_t)time(nullptr);
    root.i_mtime  = (int32_t)time(nullptr);
    root.i_type   = '0';  // carpeta
    memcpy(root.i_perm, "755", 3);
    for (int i = 0; i < 15; i++) root.i_block[i] = -1;
    root.i_block[0] = 0;  // bloque 0

    file.seekp(sb.s_inode_start);
    file.write(reinterpret_cast<char*>(&root), sizeof(Inode));

    // ── Crear bloque carpeta root ─────────────────────────────
    FolderBlock folder;
    memset(&folder, 0, sizeof(FolderBlock));

    memcpy(folder.b_content[0].b_name, ".",  1);
    folder.b_content[0].b_inodo = 0;
    memcpy(folder.b_content[1].b_name, "..", 2);
    folder.b_content[1].b_inodo = 0;
    folder.b_content[2].b_inodo = -1;
    folder.b_content[3].b_inodo = -1;

    file.seekp(sb.s_block_start);
    file.write(reinterpret_cast<char*>(&folder), sizeof(FolderBlock));

    // ── Crear /users.txt ──────────────────────────────────────
    // Contenido inicial
    std::string usersContent = "1,G,root\n1,U,root,root,123\n";

    // Inodo 1 para users.txt
    char one2 = '1';
    file.seekp(sb.s_bm_inode_start + 1);
    file.write(&one2, 1);
    file.seekp(sb.s_bm_block_start + 1);
    file.write(&one2, 1);

    Inode usersInode;
    usersInode.i_uid   = 1;
    usersInode.i_gid   = 1;
    usersInode.i_s     = (int32_t)usersContent.size();
    usersInode.i_atime = (int32_t)time(nullptr);
    usersInode.i_ctime = (int32_t)time(nullptr);
    usersInode.i_mtime = (int32_t)time(nullptr);
    usersInode.i_type  = '1';  // archivo
    memcpy(usersInode.i_perm, "664", 3);
    for (int i = 0; i < 15; i++) usersInode.i_block[i] = -1;
    usersInode.i_block[0] = 1;  // bloque 1

    file.seekp(sb.s_inode_start + inode_s);
    file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inode));

    // Bloque 1: contenido de users.txt
    FileBlock usersBlock;
    memset(&usersBlock, 0, sizeof(FileBlock));
    memcpy(usersBlock.b_content, usersContent.c_str(), usersContent.size());

    file.seekp(sb.s_block_start + block_s);
    file.write(reinterpret_cast<char*>(&usersBlock), sizeof(FileBlock));

    // Agregar users.txt al bloque root
    memcpy(folder.b_content[2].b_name, "users.txt", 9);
    folder.b_content[2].b_inodo = 1;

    file.seekp(sb.s_block_start);
    file.write(reinterpret_cast<char*>(&folder), sizeof(FolderBlock));

    // Actualizar superblock
    sb.s_free_inodes_count = n - 2;
    sb.s_free_blocks_count = 3*n - 2;
    sb.s_firts_ino         = sb.s_inode_start + 2*inode_s;
    sb.s_first_blo         = sb.s_block_start + 2*block_s;

    file.seekp(partStart);
    file.write(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    file.close();

    return "SUCCESS: partición formateada como " + fs + "\n"
           "  id            : " + id + "\n"
           "  inodos        : " + std::to_string(n) + "\n"
           "  bloques       : " + std::to_string(3*n) + "\n"
           "  magic         : 0xEF53\n"
           "  users.txt     : creado con root:123";
}