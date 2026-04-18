#include "rep.h"
#include "../structures/mbr.h"
#include "../structures/ext2.h"
#include "../structures/globals.h"
#include "../utils/utils.h"

#include <fstream>
#include <cstring>
#include <cstdlib>
#include <ctime>

// ── Ejecutar comando dot ──────────────────────────────────────
static bool runDot(const std::string& dotFile, const std::string& outFile) {
    std::string ext = outFile.substr(outFile.find_last_of('.') + 1);
    std::string cmd = "dot -T" + ext + " \"" + dotFile + "\" -o \"" + outFile + "\"";
    return system(cmd.c_str()) == 0;
}

// ── Crear directorios del path de salida ──────────────────────
static void mkdirOut(const std::string& path) {
    std::string dir = path.substr(0, path.find_last_of('/'));
    std::string cmd = "mkdir -p \"" + dir + "\"";
    system(cmd.c_str());
}

// ── Convertir timestamp a string legible ──────────────────────
static std::string tsToStr(int32_t ts) {
    time_t t = (time_t)ts;
    char buf[32];
    struct tm* tm_info = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buf);
}

// ── Buscar inodo en directorio ────────────────────────────────
static int findInDirR(std::fstream& file, const Superblock& sb,
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

// ── Navegar path sin vectores, retorna inodo final ────────────
static int navigatePathR(std::fstream& file, const Superblock& sb,
                          const std::string& path) {
    char buf[512];
    strncpy(buf, path.c_str(), sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    int cur = 0;
    char* tok = strtok(buf, "/");
    while (tok != nullptr) {
        cur = findInDirR(file, sb, cur, tok);
        if (cur == -1) return -1;
        tok = strtok(nullptr, "/");
    }
    return cur;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE MBR
// ═════════════════════════════════════════════════════════════
static std::string repMBR(const std::string& diskPath, const std::string& out) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    std::string dot = "digraph MBR {\n";
    dot += "  node [shape=plaintext];\n";
    dot += "  mbr [label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";

    dot += "      <TR><TD COLSPAN='2' BGCOLOR='#2874a6'><FONT COLOR='white'><B>REPORTE DE MBR</B></FONT></TD></TR>\n";
    dot += "      <TR><TD>mbr_tamano</TD><TD>" + std::to_string(mbr.mbr_tamano) + "</TD></TR>\n";
    dot += "      <TR><TD>mbr_fecha_creacion</TD><TD>" + std::string(mbr.mbr_fecha_creacion) + "</TD></TR>\n";
    dot += "      <TR><TD>mbr_disk_signature</TD><TD>" + std::to_string(mbr.mbr_dsk_signature) + "</TD></TR>\n";

    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start == -1) continue;
        char ptype = mbr.mbr_partitions[i].part_type;
        std::string pname(mbr.mbr_partitions[i].part_name);
        pname = pname.substr(0, pname.find('\0'));

        std::string bgPart = "#aed6f1";
        std::string bgHead = "#2874a6";
        std::string title  = "Particion";
        if (ptype == 'E' || ptype == 'e') { bgPart = "#d6eaf8"; bgHead = "#1f618d"; title = "Particion Extendida"; }

        dot += "      <TR><TD COLSPAN='2' BGCOLOR='" + bgHead + "'><FONT COLOR='white'><B>" + title + "</B></FONT></TD></TR>\n";
        dot += "      <TR><TD BGCOLOR='" + bgPart + "'>part_status</TD><TD BGCOLOR='" + bgPart + "'>" + std::string(1, mbr.mbr_partitions[i].part_status) + "</TD></TR>\n";
        dot += "      <TR><TD BGCOLOR='" + bgPart + "'>part_type</TD><TD BGCOLOR='" + bgPart + "'>" + std::string(1, ptype) + "</TD></TR>\n";
        dot += "      <TR><TD BGCOLOR='" + bgPart + "'>part_fit</TD><TD BGCOLOR='" + bgPart + "'>" + std::string(1, mbr.mbr_partitions[i].part_fit) + "</TD></TR>\n";
        dot += "      <TR><TD BGCOLOR='" + bgPart + "'>part_start</TD><TD BGCOLOR='" + bgPart + "'>" + std::to_string(mbr.mbr_partitions[i].part_start) + "</TD></TR>\n";
        dot += "      <TR><TD BGCOLOR='" + bgPart + "'>part_size</TD><TD BGCOLOR='" + bgPart + "'>" + std::to_string(mbr.mbr_partitions[i].part_s) + "</TD></TR>\n";
        dot += "      <TR><TD BGCOLOR='" + bgPart + "'>part_name</TD><TD BGCOLOR='" + bgPart + "'>" + pname + "</TD></TR>\n";

        if (ptype == 'E' || ptype == 'e') {
            int pos = mbr.mbr_partitions[i].part_start;
            while (pos != -1) {
                EBR ebr;
                file.seekg(pos);
                file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                std::string ename(ebr.part_name);
                ename = ename.substr(0, ename.find('\0'));
                if (ename.empty()) break;

                dot += "      <TR><TD COLSPAN='2' BGCOLOR='#154360'><FONT COLOR='white'><B>Particion Logica</B></FONT></TD></TR>\n";
                dot += "      <TR><TD BGCOLOR='#85c1e2'>part_status</TD><TD BGCOLOR='#85c1e2'>" + std::string(1, ebr.part_mount) + "</TD></TR>\n";
                dot += "      <TR><TD BGCOLOR='#85c1e2'>part_next</TD><TD BGCOLOR='#85c1e2'>" + std::to_string(ebr.part_next) + "</TD></TR>\n";
                dot += "      <TR><TD BGCOLOR='#85c1e2'>part_fit</TD><TD BGCOLOR='#85c1e2'>" + std::string(1, ebr.part_fit) + "</TD></TR>\n";
                dot += "      <TR><TD BGCOLOR='#85c1e2'>part_start</TD><TD BGCOLOR='#85c1e2'>" + std::to_string(ebr.part_start) + "</TD></TR>\n";
                dot += "      <TR><TD BGCOLOR='#85c1e2'>part_size</TD><TD BGCOLOR='#85c1e2'>" + std::to_string(ebr.part_s) + "</TD></TR>\n";
                dot += "      <TR><TD BGCOLOR='#85c1e2'>part_name</TD><TD BGCOLOR='#85c1e2'>" + ename + "</TD></TR>\n";
                pos = ebr.part_next;
            }
        }
    }

    dot += "    </TABLE>>];\n}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot al generar imagen";
    return "SUCCESS: reporte MBR generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE DISK
// ═════════════════════════════════════════════════════════════
static std::string repDisk(const std::string& diskPath, const std::string& out) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    double totalBytes = (double)mbr.mbr_tamano;
    if (totalBytes <= 0) { file.close(); return "ERROR: disco inválido"; }

    std::string diskName = diskPath.substr(diskPath.find_last_of('/') + 1);

    std::string dot = "digraph DISK {\n";
    dot += "  graph [label=\"" + diskName + "\" labelloc=t fontsize=16 fontname=\"Helvetica\"];\n";
    dot += "  node [shape=plaintext fontname=\"Helvetica\" fontsize=11];\n";
    dot += "  rankdir=LR;\n";
    dot += "  disk [label=<\n";
    dot += "    <TABLE BORDER='2' CELLBORDER='1' CELLSPACING='0' COLOR='#5dade2' BGCOLOR='white'>\n";
    dot += "      <TR>\n";
    dot += "        <TD BGCOLOR='#d6eaf8' WIDTH='50'><B>MBR</B></TD>\n";

    int order[4] = {0,1,2,3};
    for (int i = 0; i < 3; i++)
        for (int j = i+1; j < 4; j++)
            if (mbr.mbr_partitions[order[j]].part_start != -1 &&
               (mbr.mbr_partitions[order[i]].part_start == -1 ||
                mbr.mbr_partitions[order[j]].part_start < mbr.mbr_partitions[order[i]].part_start))
                { int tmp = order[i]; order[i] = order[j]; order[j] = tmp; }

    long firstStart = -1;
    for (int oi = 0; oi < 4; oi++) {
        int i = order[oi];
        if (mbr.mbr_partitions[i].part_start != -1) { firstStart = mbr.mbr_partitions[i].part_start; break; }
    }
    long mbrEnd = (long)sizeof(MBR);
    if (firstStart > mbrEnd) {
        double freePct = (double)(firstStart - mbrEnd) / totalBytes * 100.0;
        char buf[32]; snprintf(buf, sizeof(buf), "%.0f%%", freePct);
        dot += "        <TD BGCOLOR='white' WIDTH='80'>Libre<BR/>" + std::string(buf) + " del disco</TD>\n";
    }

    long lastEnd = firstStart == -1 ? mbrEnd : firstStart;

    for (int oi = 0; oi < 4; oi++) {
        int i = order[oi];
        if (mbr.mbr_partitions[i].part_start == -1) continue;

        char ptype = mbr.mbr_partitions[i].part_type;
        std::string pname(mbr.mbr_partitions[i].part_name);
        pname = pname.substr(0, pname.find('\0'));
        long psize = mbr.mbr_partitions[i].part_s;
        double pPct = (double)psize / totalBytes * 100.0;
        char buf[32]; snprintf(buf, sizeof(buf), "%.0f%%", pPct);

        if (mbr.mbr_partitions[i].part_start > lastEnd) {
            double gapPct = (double)(mbr.mbr_partitions[i].part_start - lastEnd) / totalBytes * 100.0;
            char gbuf[32]; snprintf(gbuf, sizeof(gbuf), "%.0f%%", gapPct);
            dot += "        <TD BGCOLOR='white'>Libre<BR/>" + std::string(gbuf) + " del disco</TD>\n";
        }

        if (ptype == 'P' || ptype == 'p') {
            dot += "        <TD BGCOLOR='#d6eaf8'><B>Primaria</B><BR/>" + pname + "<BR/>" + std::string(buf) + " del disco</TD>\n";
        } else if (ptype == 'E' || ptype == 'e') {
            dot += "        <TD BGCOLOR='#d5f5e3'>\n";
            dot += "          <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' COLOR='#27ae60'>\n";
            dot += "            <TR><TD COLSPAN='20' BGCOLOR='#abebc6'><B>Extendida</B></TD></TR>\n";
            dot += "            <TR>\n";

            int pos = mbr.mbr_partitions[i].part_start;
            long extEnd = mbr.mbr_partitions[i].part_start + psize;

            while (pos != -1 && pos < extEnd) {
                EBR ebr;
                file.seekg(pos);
                file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                dot += "              <TD BGCOLOR='#a9cce3' WIDTH='30'>EBR</TD>\n";
                std::string ename(ebr.part_name);
                ename = ename.substr(0, ename.find('\0'));
                if (!ename.empty() && ebr.part_s > 0) {
                    double lPct = (double)ebr.part_s / totalBytes * 100.0;
                    char lbuf[32]; snprintf(lbuf, sizeof(lbuf), "%.0f%%", lPct);
                    dot += "              <TD BGCOLOR='#d6eaf8'>Lógica<BR/>" + ename + "<BR/>" + std::string(lbuf) + " del Disco</TD>\n";
                    long ebrEnd = ebr.part_start + ebr.part_s;
                    if (ebr.part_next != -1 && ebr.part_next > ebrEnd) {
                        double lfPct = (double)(ebr.part_next - ebrEnd) / totalBytes * 100.0;
                        char lfbuf[32]; snprintf(lfbuf, sizeof(lfbuf), "%.0f%%", lfPct);
                        dot += "              <TD BGCOLOR='white'>Libre<BR/>" + std::string(lfbuf) + " del Disco</TD>\n";
                    }
                }
                if (ebr.part_next == -1) break;
                pos = ebr.part_next;
            }
            dot += "            </TR>\n          </TABLE>\n        </TD>\n";
        }
        lastEnd = mbr.mbr_partitions[i].part_start + psize;
    }

    if (lastEnd < (long)mbr.mbr_tamano) {
        double freePct = (double)(mbr.mbr_tamano - lastEnd) / totalBytes * 100.0;
        char buf[32]; snprintf(buf, sizeof(buf), "%.0f%%", freePct);
        dot += "        <TD BGCOLOR='white'>Libre<BR/>" + std::string(buf) + " del disco</TD>\n";
    }

    dot += "      </TR>\n    </TABLE>>];\n}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot al generar imagen";
    return "SUCCESS: reporte DISK generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE SUPERBLOCK
// ═════════════════════════════════════════════════════════════
static std::string repSuperblock(const std::string& diskPath,
                                  const std::string& id,
                                  const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    if (partStart == -1) { file.close(); return "ERROR: partición no encontrada"; }

    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    file.close();

    std::string dot = "digraph SB {\n  node [shape=plaintext];\n  sb [label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";
    dot += "    <TR><TD COLSPAN='2' BGCOLOR='#2980b9'><FONT COLOR='white'><B>SuperBlock</B></FONT></TD></TR>\n";

    auto row = [&](const std::string& k, const std::string& v) {
        dot += "    <TR><TD BGCOLOR='#d6eaf8'>" + k + "</TD><TD>" + v + "</TD></TR>\n";
    };
    row("s_filesystem_type",   std::to_string(sb.s_filesystem_type));
    row("s_inodes_count",      std::to_string(sb.s_inodes_count));
    row("s_blocks_count",      std::to_string(sb.s_blocks_count));
    row("s_free_inodes_count", std::to_string(sb.s_free_inodes_count));
    row("s_free_blocks_count", std::to_string(sb.s_free_blocks_count));
    row("s_mtime",             tsToStr(sb.s_mtime));
    row("s_umtime",            tsToStr(sb.s_umtime));
    row("s_mnt_count",         std::to_string(sb.s_mnt_count));
    row("s_magic",             "0xEF53");
    row("s_inode_s",           std::to_string(sb.s_inode_s));
    row("s_block_s",           std::to_string(sb.s_block_s));
    row("s_firts_ino",         std::to_string(sb.s_firts_ino));
    row("s_first_blo",         std::to_string(sb.s_first_blo));
    row("s_bm_inode_start",    std::to_string(sb.s_bm_inode_start));
    row("s_bm_block_start",    std::to_string(sb.s_bm_block_start));
    row("s_inode_start",       std::to_string(sb.s_inode_start));
    row("s_block_start",       std::to_string(sb.s_block_start));
    dot += "    </TABLE>>];\n}\n";

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot al generar imagen";
    return "SUCCESS: reporte SuperBlock generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE BITMAP INODOS
// ═════════════════════════════════════════════════════════════
static std::string repBmInode(const std::string& diskPath,
                               const std::string& id,
                               const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    mkdirOut(out);

    std::string ext = out.size() >= 4 ? out.substr(out.size()-4) : "";
    if (ext == ".txt") {
        std::string text = "Bitmap Inodos\n";
        int cols = 0;
        for (int i = 0; i < sb.s_inodes_count; i++) {
            char st; file.seekg(sb.s_bm_inode_start + i); file.read(&st, 1);
            text += st; cols++;
            if (cols == 20) { text += "\n"; cols = 0; }
        }
        file.close();
        std::ofstream tf(out); tf << text; tf.close();
        return "SUCCESS: reporte Bitmap Inodos generado\n  path : " + out;
    }

    std::string dot = "digraph BM {\n  node [shape=plaintext];\n  bm [label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";
    dot += "    <TR><TD COLSPAN='20' BGCOLOR='#2980b9'><FONT COLOR='white'><B>Bitmap Inodos</B></FONT></TD></TR>\n";
    dot += "    <TR>\n";
    int cols = 0;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char status; file.seekg(sb.s_bm_inode_start + i); file.read(&status, 1);
        std::string color = (status == '1') ? "#e74c3c" : "#2ecc71";
        dot += "      <TD BGCOLOR='" + color + "'><FONT COLOR='white'>" + std::string(1,status) + "</FONT></TD>\n";
        cols++;
        if (cols == 20) { dot += "    </TR><TR>\n"; cols = 0; }
    }
    dot += "    </TR></TABLE>>];\n}\n";
    file.close();

    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte Bitmap Inodos generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE BITMAP BLOQUES
// ═════════════════════════════════════════════════════════════
static std::string repBmBlock(const std::string& diskPath,
                               const std::string& id,
                               const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    mkdirOut(out);

    std::string ext = out.size() >= 4 ? out.substr(out.size()-4) : "";
    if (ext == ".txt") {
        std::string text = "Bitmap Bloques\n";
        int cols = 0;
        for (int i = 0; i < sb.s_blocks_count; i++) {
            char st; file.seekg(sb.s_bm_block_start + i); file.read(&st, 1);
            text += st; cols++;
            if (cols == 20) { text += "\n"; cols = 0; }
        }
        file.close();
        std::ofstream tf(out); tf << text; tf.close();
        return "SUCCESS: reporte Bitmap Bloques generado\n  path : " + out;
    }

    std::string dot = "digraph BM {\n  node [shape=plaintext];\n  bm [label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";
    dot += "    <TR><TD COLSPAN='20' BGCOLOR='#8e44ad'><FONT COLOR='white'><B>Bitmap Bloques</B></FONT></TD></TR>\n";
    dot += "    <TR>\n";
    int cols = 0;
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char status; file.seekg(sb.s_bm_block_start + i); file.read(&status, 1);
        std::string color = (status == '1') ? "#e74c3c" : "#2ecc71";
        dot += "      <TD BGCOLOR='" + color + "'><FONT COLOR='white'>" + std::string(1,status) + "</FONT></TD>\n";
        cols++;
        if (cols == 20) { dot += "    </TR><TR>\n"; cols = 0; }
    }
    dot += "    </TR></TABLE>>];\n}\n";
    file.close();

    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte Bitmap Bloques generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE TREE — Inodos y Bloques con flechas (estilo imágenes)
//  Azul=Inodo, Naranja=BloqueCarpeta, Amarillo=BloqueApuntador
//  Verde=BloqueArchivo
// ═════════════════════════════════════════════════════════════

// Genera nodo inodo en dot
static void genInodeNode(std::string& dot, int inodeIdx, const Inode& inode) {
    std::string tipo = (inode.i_type == '0') ? "carpeta" : "archivo";
    std::string fecha = tsToStr(inode.i_atime);

    // Contar bloques directos asignados
    int adCount = 0;
    for (int b = 0; b < 12; b++) if (inode.i_block[b] != -1) adCount++;

    dot += "  I" + std::to_string(inodeIdx) + " [shape=plaintext label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='#aed6f1' COLOR='#2980b9'>\n";
    dot += "    <TR><TD COLSPAN='2' BGCOLOR='#aed6f1'><B>I-nodo " + std::to_string(inodeIdx) + "</B></TD></TR>\n";
    dot += "    <TR><TD>ID</TD><TD>" + std::to_string(inodeIdx) + "</TD></TR>\n";
    dot += "    <TR><TD>UID</TD><TD>" + std::to_string(inode.i_uid) + "</TD></TR>\n";
    dot += "    <TR><TD>Fecha</TD><TD>" + fecha + "</TD></TR>\n";
    dot += "    <TR><TD>Tipo</TD><TD>" + tipo + "</TD></TR>\n";
    dot += "    <TR><TD>Size</TD><TD>" + std::to_string(inode.i_s) + "</TD></TR>\n";

    // Bloques directos
    for (int b = 0; b < 12; b++) {
        dot += "    <TR><TD>AD</TD><TD PORT='ad" + std::to_string(b) + "'>" + std::to_string(inode.i_block[b]) + "</TD></TR>\n";
    }
    // Bloques indirectos
    for (int b = 12; b < 15; b++) {
        dot += "    <TR><TD>AI</TD><TD PORT='ai" + std::to_string(b) + "'>" + std::to_string(inode.i_block[b]) + "</TD></TR>\n";
    }

    dot += "    </TABLE>>];\n";
}

// Genera nodo bloque carpeta en dot
static void genFolderBlockNode(std::string& dot, int blockIdx, const FolderBlock& fb) {
    dot += "  B" + std::to_string(blockIdx) + " [shape=plaintext label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='#f0b429' COLOR='#d68910'>\n";
    dot += "    <TR><TD COLSPAN='2' BGCOLOR='#f0b429'><B>Bloque " + std::to_string(blockIdx) + "</B></TD></TR>\n";
    for (int j = 0; j < 4; j++) {
        std::string nm(fb.b_content[j].b_name, 12);
        nm = nm.substr(0, nm.find('\0'));
        if (nm.empty()) nm = "--";
        dot += "    <TR><TD PORT='e" + std::to_string(j) + "'>" + nm + "</TD><TD>" + std::to_string(fb.b_content[j].b_inodo) + "</TD></TR>\n";
    }
    dot += "    </TABLE>>];\n";
}

// Genera nodo bloque archivo en dot
static void genFileBlockNode(std::string& dot, int blockIdx, const FileBlock& fb) {
    std::string content;
    for (int k = 0; k < 64 && fb.b_content[k] != '\0'; k++) {
        char c = fb.b_content[k];
        if (c == '<') content += "&lt;";
        else if (c == '>') content += "&gt;";
        else if (c == '&') content += "&amp;";
        else if (c == '\n') content += "\\n";
        else content += c;
    }
    dot += "  B" + std::to_string(blockIdx) + " [shape=plaintext label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='#82e0aa' COLOR='#27ae60'>\n";
    dot += "    <TR><TD BGCOLOR='#82e0aa'><B>Bloque Archivo " + std::to_string(blockIdx) + "</B></TD></TR>\n";
    dot += "    <TR><TD>" + content + "</TD></TR>\n";
    dot += "    </TABLE>>];\n";
}

// Genera nodo bloque apuntador en dot
static void genPointerBlockNode(std::string& dot, int blockIdx, const PointerBlock& pb) {
    dot += "  B" + std::to_string(blockIdx) + " [shape=plaintext label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='#f9e79f' COLOR='#d4ac0d'>\n";
    dot += "    <TR><TD BGCOLOR='#f9e79f'><B>Bloque Apuntadores " + std::to_string(blockIdx) + "</B></TD></TR>\n";
    for (int k = 0; k < 16; k++) {
        dot += "    <TR><TD PORT='p" + std::to_string(k) + "'>" + std::to_string(pb.b_pointers[k]) + "</TD></TR>\n";
    }
    dot += "    </TABLE>>];\n";
}

// Procesar inodo recursivamente para el tree
static void processInodeTree(std::fstream& file, const Superblock& sb,
                              int inodeIdx, std::string& dot,
                              bool visited[], int maxInodes) {
    if (inodeIdx < 0 || inodeIdx >= maxInodes) return;
    if (visited[inodeIdx]) return;
    visited[inodeIdx] = true;

    Inode inode;
    file.seekg(sb.s_inode_start + inodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

    genInodeNode(dot, inodeIdx, inode);

    if (inode.i_type == '0') {
        // Carpeta — bloques directos son FolderBlock
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            int blockIdx = inode.i_block[b];

            FolderBlock fb;
            file.seekg(sb.s_block_start + blockIdx * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
            genFolderBlockNode(dot, blockIdx, fb);

            // Flecha inodo -> bloque
            dot += "  I" + std::to_string(inodeIdx) + ":ad" + std::to_string(b) +
                   " -> B" + std::to_string(blockIdx) + ";\n";

            // Flechas bloque -> inodos hijos
            for (int j = 0; j < 4; j++) {
                int childInode = fb.b_content[j].b_inodo;
                if (childInode == -1) continue;
                std::string nm(fb.b_content[j].b_name, 12);
                nm = nm.substr(0, nm.find('\0'));
                if (nm == "." || nm == "..") continue;

                dot += "  B" + std::to_string(blockIdx) + ":e" + std::to_string(j) +
                       " -> I" + std::to_string(childInode) + ";\n";

                processInodeTree(file, sb, childInode, dot, visited, maxInodes);
            }
        }

        // Bloque apuntador simple i_block[12]
        if (inode.i_block[12] != -1) {
            int ptrBlockIdx = inode.i_block[12];
            PointerBlock pb;
            file.seekg(sb.s_block_start + ptrBlockIdx * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
            genPointerBlockNode(dot, ptrBlockIdx, pb);
            dot += "  I" + std::to_string(inodeIdx) + ":ai12 -> B" + std::to_string(ptrBlockIdx) + ";\n";

            for (int k = 0; k < 16; k++) {
                if (pb.b_pointers[k] == -1) continue;
                FolderBlock fb2;
                file.seekg(sb.s_block_start + pb.b_pointers[k] * sb.s_block_s);
                file.read(reinterpret_cast<char*>(&fb2), sizeof(FolderBlock));
                genFolderBlockNode(dot, pb.b_pointers[k], fb2);
                dot += "  B" + std::to_string(ptrBlockIdx) + ":p" + std::to_string(k) +
                       " -> B" + std::to_string(pb.b_pointers[k]) + ";\n";
                for (int j = 0; j < 4; j++) {
                    int ci = fb2.b_content[j].b_inodo;
                    if (ci == -1) continue;
                    std::string nm(fb2.b_content[j].b_name, 12);
                    nm = nm.substr(0, nm.find('\0'));
                    if (nm == "." || nm == "..") continue;
                    dot += "  B" + std::to_string(pb.b_pointers[k]) + ":e" + std::to_string(j) +
                           " -> I" + std::to_string(ci) + ";\n";
                    processInodeTree(file, sb, ci, dot, visited, maxInodes);
                }
            }
        }

    } else {
        // Archivo — bloques directos son FileBlock
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            int blockIdx = inode.i_block[b];
            FileBlock fb;
            file.seekg(sb.s_block_start + blockIdx * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
            genFileBlockNode(dot, blockIdx, fb);
            dot += "  I" + std::to_string(inodeIdx) + ":ad" + std::to_string(b) +
                   " -> B" + std::to_string(blockIdx) + ";\n";
        }

        // Bloque apuntador simple i_block[12]
        if (inode.i_block[12] != -1) {
            int ptrBlockIdx = inode.i_block[12];
            PointerBlock pb;
            file.seekg(sb.s_block_start + ptrBlockIdx * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
            genPointerBlockNode(dot, ptrBlockIdx, pb);
            dot += "  I" + std::to_string(inodeIdx) + ":ai12 -> B" + std::to_string(ptrBlockIdx) + ";\n";

            for (int k = 0; k < 16; k++) {
                if (pb.b_pointers[k] == -1) continue;
                FileBlock fb2;
                file.seekg(sb.s_block_start + pb.b_pointers[k] * sb.s_block_s);
                file.read(reinterpret_cast<char*>(&fb2), sizeof(FileBlock));
                genFileBlockNode(dot, pb.b_pointers[k], fb2);
                dot += "  B" + std::to_string(ptrBlockIdx) + ":p" + std::to_string(k) +
                       " -> B" + std::to_string(pb.b_pointers[k]) + ";\n";
            }
        }

        // Bloque apuntador doble i_block[13]
        if (inode.i_block[13] != -1) {
            int dblBlockIdx = inode.i_block[13];
            PointerBlock pb1;
            file.seekg(sb.s_block_start + dblBlockIdx * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&pb1), sizeof(PointerBlock));
            genPointerBlockNode(dot, dblBlockIdx, pb1);
            dot += "  I" + std::to_string(inodeIdx) + ":ai13 -> B" + std::to_string(dblBlockIdx) + ";\n";

            for (int k = 0; k < 16; k++) {
                if (pb1.b_pointers[k] == -1) continue;
                PointerBlock pb2;
                file.seekg(sb.s_block_start + pb1.b_pointers[k] * sb.s_block_s);
                file.read(reinterpret_cast<char*>(&pb2), sizeof(PointerBlock));
                genPointerBlockNode(dot, pb1.b_pointers[k], pb2);
                dot += "  B" + std::to_string(dblBlockIdx) + ":p" + std::to_string(k) +
                       " -> B" + std::to_string(pb1.b_pointers[k]) + ";\n";

                for (int m = 0; m < 16; m++) {
                    if (pb2.b_pointers[m] == -1) continue;
                    FileBlock fb2;
                    file.seekg(sb.s_block_start + pb2.b_pointers[m] * sb.s_block_s);
                    file.read(reinterpret_cast<char*>(&fb2), sizeof(FileBlock));
                    genFileBlockNode(dot, pb2.b_pointers[m], fb2);
                    dot += "  B" + std::to_string(pb1.b_pointers[k]) + ":p" + std::to_string(m) +
                           " -> B" + std::to_string(pb2.b_pointers[m]) + ";\n";
                }
            }
        }
    }
}

static std::string repTree(const std::string& diskPath,
                             const std::string& id,
                             const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    std::string dot = "digraph TREE {\n";
    dot += "  rankdir=LR;\n";
    dot += "  bgcolor=\"white\";\n";
    dot += "  node [fontname=\"Helvetica\" fontsize=10];\n";
    dot += "  edge [color=\"black\"];\n";

    bool visited[110000];
    memset(visited, 0, sizeof(visited));

    // Comenzar desde inodo 0 (root)
    processInodeTree(file, sb, 0, dot, visited, sb.s_inodes_count);

    dot += "}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte Tree generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE INODE
// ═════════════════════════════════════════════════════════════
static std::string repInode(const std::string& diskPath,
                             const std::string& id,
                             const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    std::string dot = "digraph INODES {\n";
    dot += "  node [shape=plaintext fontname=\"Helvetica\" fontsize=10];\n";
    dot += "  rankdir=LR;\n";

    int prevIdx = -1;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        char status; file.seekg(sb.s_bm_inode_start + i); file.read(&status, 1);
        if (status != '1') continue;

        Inode inode;
        file.seekg(sb.s_inode_start + i * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

        std::string tipo = (inode.i_type == '0') ? "carpeta" : "archivo";
        std::string perm(inode.i_perm, 3);

        dot += "  i" + std::to_string(i) + " [label=<\n";
        dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";
        dot += "    <TR><TD COLSPAN='2' BGCOLOR='#27ae60'><FONT COLOR='white'><B>Inodo " + std::to_string(i) + "</B></FONT></TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_uid</TD><TD>" + std::to_string(inode.i_uid) + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_gid</TD><TD>" + std::to_string(inode.i_gid) + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_size</TD><TD>" + std::to_string(inode.i_s) + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_atime</TD><TD>" + tsToStr(inode.i_atime) + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_ctime</TD><TD>" + tsToStr(inode.i_ctime) + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_mtime</TD><TD>" + tsToStr(inode.i_mtime) + "</TD></TR>\n";
        for (int b = 0; b < 15; b++)
            dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_block[" + std::to_string(b) + "]</TD><TD>" + std::to_string(inode.i_block[b]) + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_type</TD><TD>" + tipo + "</TD></TR>\n";
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_perm</TD><TD>" + perm + "</TD></TR>\n";
        dot += "    </TABLE>>];\n";

        if (prevIdx != -1)
            dot += "  i" + std::to_string(prevIdx) + " -> i" + std::to_string(i) + ";\n";
        prevIdx = i;
    }
    dot += "}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte Inodos generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE BLOCK
// ═════════════════════════════════════════════════════════════
static std::string repBlock(const std::string& diskPath,
                             const std::string& id,
                             const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    char blockType[110000];
    memset(blockType, 0, sizeof(blockType));

    for (int i = 0; i < sb.s_inodes_count; i++) {
        char status; file.seekg(sb.s_bm_inode_start + i); file.read(&status, 1);
        if (status != '1') continue;
        Inode inode;
        file.seekg(sb.s_inode_start + i * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));
        char directType = (inode.i_type == '0') ? 'F' : 'A';
        for (int b = 0; b < 12; b++)
            if (inode.i_block[b] != -1 && inode.i_block[b] < sb.s_blocks_count)
                blockType[inode.i_block[b]] = directType;
        if (inode.i_block[12] != -1 && inode.i_block[12] < sb.s_blocks_count)
            blockType[inode.i_block[12]] = 'P';
        if (inode.i_block[13] != -1 && inode.i_block[13] < sb.s_blocks_count)
            blockType[inode.i_block[13]] = 'P';
        if (inode.i_block[14] != -1 && inode.i_block[14] < sb.s_blocks_count)
            blockType[inode.i_block[14]] = 'P';
    }

    std::string dot = "digraph BLOCKS {\n";
    dot += "  node [shape=plaintext fontname=\"Helvetica\" fontsize=10];\n";
    dot += "  rankdir=LR;\n";

    int prevIdx = -1;
    for (int i = 0; i < sb.s_blocks_count; i++) {
        char status; file.seekg(sb.s_bm_block_start + i); file.read(&status, 1);
        if (status != '1') continue;

        if (blockType[i] == 'F') {
            FolderBlock fb;
            file.seekg(sb.s_block_start + i * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
            genFolderBlockNode(dot, i, fb);
        } else if (blockType[i] == 'A') {
            FileBlock fb;
            file.seekg(sb.s_block_start + i * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
            genFileBlockNode(dot, i, fb);
        } else if (blockType[i] == 'P') {
            PointerBlock pb;
            file.seekg(sb.s_block_start + i * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&pb), sizeof(PointerBlock));
            genPointerBlockNode(dot, i, pb);
        } else {
            dot += "  b" + std::to_string(i) + " [shape=plaintext label=<\n";
            dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='#ccc'>\n";
            dot += "    <TR><TD><B>Bloque " + std::to_string(i) + "</B></TD></TR>\n";
            dot += "    </TABLE>>];\n";
        }

        if (prevIdx != -1)
            dot += "  B" + std::to_string(prevIdx) + " -> B" + std::to_string(i) + ";\n";
        prevIdx = i;
    }
    dot += "}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte Bloques generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE FILE
// ═════════════════════════════════════════════════════════════
static std::string repFile(const std::string& diskPath,
                            const std::string& id,
                            const std::string& filePath,
                            const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    size_t lastSlash = filePath.find_last_of('/');
    std::string parentPath = filePath.substr(0, lastSlash);
    std::string filename   = filePath.substr(lastSlash + 1);

    int dirInode = navigatePathR(file, sb, parentPath);
    if (dirInode == -1) { file.close(); return "ERROR: directorio no existe"; }

    int fileInodeIdx = findInDirR(file, sb, dirInode, filename.c_str());
    if (fileInodeIdx == -1) { file.close(); return "ERROR: archivo no encontrado: " + filename; }

    Inode fileInode;
    file.seekg(sb.s_inode_start + fileInodeIdx * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&fileInode), sizeof(Inode));

    std::string dot = "digraph FILE {\n  bgcolor=\"white\";\n  node [shape=plaintext];\n  rankdir=LR;\n";
    dot += "  inode [label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";
    dot += "    <TR><TD COLSPAN='2' BGCOLOR='#27ae60'><FONT COLOR='white'><B>Inodo " + std::to_string(fileInodeIdx) + "</B></FONT></TD></TR>\n";
    dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_type</TD><TD>archivo</TD></TR>\n";
    dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_size</TD><TD>" + std::to_string(fileInode.i_s) + "</TD></TR>\n";
    dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_perm</TD><TD>" + std::string(fileInode.i_perm, 3) + "</TD></TR>\n";
    for (int i = 0; i < 12; i++) {
        if (fileInode.i_block[i] == -1) break;
        dot += "    <TR><TD BGCOLOR='#d5f5e3'>i_block[" + std::to_string(i) + "]</TD>"
               "<TD PORT='b" + std::to_string(i) + "'>" +
               std::to_string(fileInode.i_block[i]) + "</TD></TR>\n";
    }
    dot += "    </TABLE>>];\n";

    for (int i = 0; i < 12; i++) {
        if (fileInode.i_block[i] == -1) break;
        FileBlock fb;
        file.seekg(sb.s_block_start + fileInode.i_block[i] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
        std::string escaped;
        for (int k = 0; k < 64; k++) {
            char c = fb.b_content[k];
            if (c == '\0') break;
            else if (c == '\n') escaped += "\\n";
            else if (c == '"')  escaped += "\\\"";
            else if (c == '<')  escaped += "\\<";
            else if (c == '>')  escaped += "\\>";
            else if (c == '|')  escaped += "\\|";
            else if (c == '{')  escaped += "\\{";
            else if (c == '}')  escaped += "\\}";
            else escaped += c;
        }
        dot += "  block" + std::to_string(i) +
               " [label=\"Bloque " + std::to_string(fileInode.i_block[i]) +
               "\\n" + escaped + "\" shape=record style=filled "
               "fillcolor=\"#d6eaf8\" fontcolor=\"black\" color=\"#2980b9\"];\n";
        dot += "  inode:b" + std::to_string(i) + " -> block" + std::to_string(i) + ";\n";
    }
    dot += "}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte File generado\n  path : " + out;
}

// ═════════════════════════════════════════════════════════════
//  REPORTE LS
// ═════════════════════════════════════════════════════════════
static std::string repLs(const std::string& diskPath,
                          const std::string& id,
                          const std::string& dirPath,
                          const std::string& out) {
    if (mountedPartitions.find(id) == mountedPartitions.end())
        return "ERROR: partición no montada: " + id;

    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return "ERROR: no se pudo abrir el disco";

    MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    int partStart = -1;
    for (int i = 0; i < 4; i++) {
        std::string pid(mbr.mbr_partitions[i].part_id, 4);
        pid = pid.substr(0, pid.find('\0'));
        if (pid == id) { partStart = mbr.mbr_partitions[i].part_start; break; }
    }
    Superblock sb; file.seekg(partStart); file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));

    // Leer users.txt para mapear uid->nombre y gid->grupo
    char usersName[200][12]; memset(usersName, 0, sizeof(usersName));
    char groupName[200][12]; memset(groupName, 0, sizeof(groupName));

    // Leer inodo 1 (users.txt está en el root)
    Inode rootInode;
    file.seekg(sb.s_inode_start);
    file.read(reinterpret_cast<char*>(&rootInode), sizeof(Inode));

    int usersInodeIdx = -1;
    for (int b = 0; b < 12 && usersInodeIdx == -1; b++) {
        if (rootInode.i_block[b] == -1) continue;
        FolderBlock fb2;
        file.seekg(sb.s_block_start + rootInode.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb2), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (strncmp(fb2.b_content[j].b_name, "users.txt", 12) == 0) {
                usersInodeIdx = fb2.b_content[j].b_inodo; break;
            }
        }
    }

    if (usersInodeIdx != -1) {
        Inode ui;
        file.seekg(sb.s_inode_start + usersInodeIdx * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&ui), sizeof(Inode));
        std::string content;
        for (int b = 0; b < 12; b++) {
            if (ui.i_block[b] == -1) break;
            FileBlock fb3;
            file.seekg(sb.s_block_start + ui.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb3), sizeof(FileBlock));
            content += std::string(fb3.b_content, 64);
        }
        content = content.substr(0, ui.i_s);
        // Parsear líneas
        size_t pos = 0;
        while (pos < content.size()) {
            size_t end = content.find('\n', pos);
            if (end == std::string::npos) end = content.size();
            std::string line = content.substr(pos, end - pos);
            pos = end + 1;
            // Formato: id,G,nombre o id,U,grupo,nombre,pass
            char fields[5][32]; memset(fields, 0, sizeof(fields));
            int fi = 0; size_t fp = 0;
            for (size_t k = 0; k <= line.size() && fi < 5; k++) {
                if (k == line.size() || line[k] == ',') {
                    strncpy(fields[fi++], line.c_str() + fp, k - fp);
                    fp = k + 1;
                }
            }
            int lid = atoi(fields[0]);
            if (lid <= 0 || lid >= 200) continue;
            if (strcmp(fields[1], "G") == 0)
                strncpy(groupName[lid], fields[2], 11);
            else if (strcmp(fields[1], "U") == 0)
                strncpy(usersName[lid], fields[3], 11);
        }
    }

    // Navegar al directorio
    int dirInode = navigatePathR(file, sb, dirPath);
    if (dirInode == -1) { file.close(); return "ERROR: directorio no existe: " + dirPath; }

    Inode dir;
    file.seekg(sb.s_inode_start + dirInode * sb.s_inode_s);
    file.read(reinterpret_cast<char*>(&dir), sizeof(Inode));

    std::string dot = "digraph LS {\n  node [shape=plaintext];\n";
    dot += "  ls [label=<\n";
    dot += "    <TABLE BORDER='1' CELLBORDER='1' CELLSPACING='0' BGCOLOR='white'>\n";
    dot += "    <TR><TD COLSPAN='8' BGCOLOR='#2980b9'><FONT COLOR='white'><B>ls " + dirPath + "</B></FONT></TD></TR>\n";
    dot += "    <TR>"
           "<TD BGCOLOR='#d6eaf8'><B>Permisos</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Owner</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Grupo</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Size (Bytes)</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Fecha</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Hora</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Tipo</B></TD>"
           "<TD BGCOLOR='#d6eaf8'><B>Name</B></TD>"
           "</TR>\n";

    for (int b = 0; b < 12; b++) {
        if (dir.i_block[b] == -1) continue;
        FolderBlock fb;
        file.seekg(sb.s_block_start + dir.i_block[b] * sb.s_block_s);
        file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) continue;
            std::string nm(fb.b_content[j].b_name, 12);
            nm = nm.substr(0, nm.find('\0'));
            if (nm == "." || nm == "..") continue;

            Inode child;
            file.seekg(sb.s_inode_start + fb.b_content[j].b_inodo * sb.s_inode_s);
            file.read(reinterpret_cast<char*>(&child), sizeof(Inode));

            std::string tipo = (child.i_type == '0') ? "Carpeta" : "Archivo";

            // Permisos
            char p[3]; memcpy(p, child.i_perm, 3);
            auto permChar = [](char c, char r, char w, char x) -> std::string {
                std::string s = "-";
                int v = c - '0';
                s += (v & 4) ? std::string(1,r) : "-";
                s += (v & 2) ? std::string(1,w) : "-";
                s += (v & 1) ? std::string(1,x) : "-";
                return s;
            };
            std::string perms = permChar(p[0],'r','w','x') +
                                permChar(p[1],'r','w','x') +
                                permChar(p[2],'r','w','x');

            // Owner y Grupo
            std::string owner = (child.i_uid > 0 && child.i_uid < 200 && usersName[child.i_uid][0])
                                ? std::string(usersName[child.i_uid]) : std::to_string(child.i_uid);
            std::string grupo = (child.i_gid > 0 && child.i_gid < 200 && groupName[child.i_gid][0])
                                ? std::string(groupName[child.i_gid]) : std::to_string(child.i_gid);

            // Fecha y hora
            time_t t = (time_t)child.i_mtime;
            char fecha[16], hora[12];
            struct tm* tm_info = localtime(&t);
            strftime(fecha, sizeof(fecha), "%d/%m/%Y", tm_info);
            strftime(hora,  sizeof(hora),  "%H:%M",    tm_info);

            dot += "    <TR>"
                   "<TD>" + perms + "</TD>"
                   "<TD>" + owner + "</TD>"
                   "<TD>" + grupo + "</TD>"
                   "<TD>" + std::to_string(child.i_s) + "</TD>"
                   "<TD>" + std::string(fecha) + "</TD>"
                   "<TD>" + std::string(hora) + "</TD>"
                   "<TD>" + tipo + "</TD>"
                   "<TD>" + nm + "</TD>"
                   "</TR>\n";
        }
    }
    dot += "    </TABLE>>];\n}\n";
    file.close();

    mkdirOut(out);
    std::string dotFile = out + ".dot";
    std::ofstream df(dotFile); df << dot; df.close();
    if (!runDot(dotFile, out)) return "ERROR: falló dot";
    return "SUCCESS: reporte LS generado\n  path : " + out;
}
// ═════════════════════════════════════════════════════════════
//  DISPATCHER
// ═════════════════════════════════════════════════════════════
std::string cmdRep(const std::map<std::string,std::string>& p) {

    if (p.find("name") == p.end()) return "ERROR: falta -name";

    std::string out = "";
    if (p.count("path")) out = p.at("path");
    if (p.count("Path")) out = p.at("Path");
    if (out.empty()) return "ERROR: falta -path";

    std::string name = toLower(p.at("name"));
    std::string id   = p.count("id")        ? p.at("id")        : "";
    std::string disk = p.count("path_disk") ? p.at("path_disk") : "";

    std::string diskPath = disk;
    if (diskPath.empty() && !id.empty() && mountedPartitions.count(id))
        diskPath = mountedPartitions[id].path;
    if (diskPath.empty() && activeSession.active && mountedPartitions.count(activeSession.partId))
        diskPath = mountedPartitions[activeSession.partId].path;

    if (name == "mbr")      return repMBR(diskPath, out);
    if (name == "disk")     return repDisk(diskPath, out);
    if (name == "sb")       return repSuperblock(diskPath, id, out);
    if (name == "bm_inode") return repBmInode(diskPath, id, out);
    if (name == "bm_block") return repBmBlock(diskPath, id, out);
    if (name == "tree")     return repTree(diskPath, id, out);
    if (name == "inode")    return repInode(diskPath, id, out);
    if (name == "block")    return repBlock(diskPath, id, out);
    if (name == "file") {
        std::string ruta = "";
        if (p.count("ruta"))         ruta = p.at("ruta");
        if (p.count("path_file_ls")) ruta = p.at("path_file_ls");
        if (ruta.empty()) return "ERROR: falta -ruta o -path_file_ls";
        return repFile(diskPath, id, ruta, out);
    }
    if (name == "ls") {
        std::string ruta = "";
        if (p.count("ruta"))         ruta = p.at("ruta");
        if (p.count("path_file_ls")) ruta = p.at("path_file_ls");
        if (ruta.empty()) return "ERROR: falta -path_file_ls";
        return repLs(diskPath, id, ruta, out);
    }

    return "ERROR: reporte no reconocido: " + name +
           "\n  Reportes válidos: mbr, disk, sb, inode, block, bm_inode, bm_block, tree, file, ls";
}