#include "loss.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <vector>

std::string cmdLoss(const std::map<std::string,std::string>& p) {

    // ── Validar -id ───────────────────────────────────────────
    if (p.find("id") == p.end())
        return "ERROR: falta -id";
    std::string id = p.at("id");

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
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) {
            partStart = mbr.mbr_partitions[i].part_start;
            break;
        }
    }
    if (partStart == -1) {
        file.close();
        return "ERROR: no se encontró la partición en el disco";
    }

    // ── Leer Superblock ───────────────────────────────────────
    Superblock sb;
    file.seekg(partStart);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    if (sb.s_magic != 0xEF53) {
        file.close();
        return "ERROR: la partición no está formateada";
    }

    // ── Verificar que es EXT3 ─────────────────────────────────
    if (sb.s_filesystem_type != 3) {
        file.close();
        return "ERROR: loss solo aplica a particiones EXT3";
    }

    // ── Limpiar las 4 áreas con \0 ────────────────────────────
    // El journal NO se toca — es la razón de existir del journaling

    // 1. Bitmap de inodos
    int bmInodeSize = sb.s_inodes_count;
    std::vector<char> zeros(bmInodeSize, '\0');
    file.seekp(sb.s_bm_inode_start);
    file.write(zeros.data(), bmInodeSize);

    // 2. Bitmap de bloques
    int bmBlockSize = sb.s_blocks_count;
    zeros.assign(bmBlockSize, '\0');
    file.seekp(sb.s_bm_block_start);
    file.write(zeros.data(), bmBlockSize);

    // 3. Área de inodos
    int inodeAreaSize = sb.s_inodes_count * sb.s_inode_s;
    zeros.assign(inodeAreaSize, '\0');
    file.seekp(sb.s_inode_start);
    file.write(zeros.data(), inodeAreaSize);

    // 4. Área de bloques
    int blockAreaSize = sb.s_blocks_count * sb.s_block_s;
    zeros.assign(blockAreaSize, '\0');
    file.seekp(sb.s_block_start);
    file.write(zeros.data(), blockAreaSize);

    file.close();

    return "SUCCESS: simulación de pérdida completada\n"
           "  id              : " + id + "\n"
           "  bitmap inodos   : limpiado (" + std::to_string(bmInodeSize)   + " bytes)\n"
           "  bitmap bloques  : limpiado (" + std::to_string(bmBlockSize)   + " bytes)\n"
           "  área inodos     : limpiada (" + std::to_string(inodeAreaSize) + " bytes)\n"
           "  área bloques    : limpiada (" + std::to_string(blockAreaSize) + " bytes)\n"
           "  journal         : intacto (usar 'journaling -id=" + id + "' para recuperar)";
}