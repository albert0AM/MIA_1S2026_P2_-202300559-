#include "vendor/crow/include/crow.h"
#include "utils/utils.h"
#include "commands/mkdisk.h"
#include "commands/rmdisk.h"
#include "commands/fdisk.h"
#include "commands/mount.h"
#include "structures/globals.h"
#include "commands/mkfs.h"
#include "commands/login.h"
#include "commands/users.h"
#include "commands/mkdir.h"
#include "commands/mkfile.h"
#include "commands/cat.h"
#include "commandsP2/rename.h"
#include "commandsP2/remove.h"
#include "commandsP2/copy.h"
#include "commandsP2/find.h"
#include "commandsP2/move.h"
#include "commandsP2/chown.h"
#include "commandsP2/chmod.h"
#include "commandsP2/journaling.h"
#include "commandsP2/loss.h"
#include "reports/rep.h"
#include "structures/mbr.h"
#include "structures/ext2.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>

// ─────────────────────────────────────────────────────────────
//  Leer archivo estático
// ─────────────────────────────────────────────────────────────
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ─────────────────────────────────────────────────────────────
//  Ejecutar script de comandos
// ─────────────────────────────────────────────────────────────
static std::string executeScript(const std::string& script) {
    std::string output;
    std::istringstream stream(script);
    std::string line;

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty())   { output += "\n"; continue; }
        if (line[0] == '#') { output += line + "\n"; continue; }

        auto params = parseCommand(line);
        if (params.find("cmd") == params.end()) {
            output += "ERROR: línea no reconocida: " + line + "\n";
            continue;
        }

        std::string cmd = params["cmd"];
        std::string result;

        if (cmd == "mkdisk")
            result = cmdMkdisk(params);
        else if (cmd == "rmdisk")
            result = cmdRmdisk(params);
        else if (cmd == "fdisk")
            result = cmdFdisk(params);
        else if (cmd == "mount")
            result = cmdMount(params);
        else if (cmd == "mounted")
            result = cmdMounted();
        else if (cmd == "unmount")
            result = cmdUnmount(params);
        else if (cmd == "mkfs")
            result = cmdMkfs(params);
        else if (cmd == "login")
            result = cmdLogin(params);
        else if (cmd == "logout")
            result = cmdLogout();
        else if (cmd == "mkgrp")
            result = cmdMkgrp(params);
        else if (cmd == "rmgrp")
            result = cmdRmgrp(params);
        else if (cmd == "mkusr")
            result = cmdMkusr(params);
        else if (cmd == "rmusr")
            result = cmdRmusr(params);
        else if (cmd == "chgrp")
            result = cmdChgrp(params);
        else if (cmd == "mkdir")
            result = cmdMkdir(params);
        else if (cmd == "mkfile")
            result = cmdMkfile(params);
        else if (cmd == "cat")
            result = cmdCat(params);
        else if (cmd == "rep")
            result = cmdRep(params);
        else if (cmd == "rename")
            result = cmdRename(params);
        else if (cmd == "remove")
            result = cmdRemove(params);
        else if (cmd == "copy")
            result = cmdCopy(params);
        else if (cmd == "find")
            result = cmdFind(params);
        else if (cmd == "move")
            result = cmdMove(params);
        else if (cmd == "chown")
            result = cmdChown(params);
        else if (cmd == "chmod")
            result = cmdChmod(params);
        else if (cmd == "journaling")
            result = cmdJournaling(params);
        else if (cmd == "loss")
            result = cmdLoss(params);
        else
            result = "ERROR: comando no reconocido: " + cmd;

        output += result + "\n";
    }
    return output;
}

// ─────────────────────────────────────────────────────────────
//  Headers CORS para todas las respuestas
// ─────────────────────────────────────────────────────────────
static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type");
    res.add_header("Content-Type", "application/json");
}

// ─────────────────────────────────────────────────────────────
//  Navegar path en EXT2/EXT3 → retorna inodo final o -1
// ─────────────────────────────────────────────────────────────
static int navigatePath(std::fstream& file, const Superblock& sb,
                         const std::string& path) {
    int curInode = 0; // raíz
    if (path == "/") return curInode;

    std::istringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty()) continue;

        Inode cur;
        file.seekg(sb.s_inode_start + curInode * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&cur), sizeof(Inode));

        bool found = false;
        for (int b = 0; b < 12; b++) {
            if (cur.i_block[b] == -1) continue;
            FolderBlock fb;
            file.seekg(sb.s_block_start + cur.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));
            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1 &&
                    strncmp(fb.b_content[j].b_name, seg.c_str(), 12) == 0) {
                    curInode = fb.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return -1;
    }
    return curInode;
}

// ─────────────────────────────────────────────────────────────
//  Abrir disco y leer Superblock de una partición montada
//  Retorna partStart o -1 si falla
// ─────────────────────────────────────────────────────────────
static int openPartition(const std::string& id,
                          std::fstream& file,
                          Superblock& sb) {
    if (mountedPartitions.find(id) == mountedPartitions.end()) return -1;
    MountedPartition& mp = mountedPartitions[id];

    file.open(mp.path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) return -1;

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
    if (partStart == -1) { file.close(); return -1; }

    file.seekg(partStart);
    file.read(reinterpret_cast<char*>(&sb), sizeof(Superblock));
    if (sb.s_magic != 0xEF53) { file.close(); return -1; }

    return partStart;
}

// ─────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────
int main() {
    crow::SimpleApp app;

    // ── GET / — sirve el frontend ─────────────────────────────
    CROW_ROUTE(app, "/")([]() {
        std::string html = readFile("static/index.html");
        crow::response res(200, html);
        res.add_header("Content-Type", "text/html; charset=UTF-8");
        return res;
    });

    // ── GET /health ───────────────────────────────────────────
    CROW_ROUTE(app, "/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        return crow::response(200, r);
    });

    // ── POST /execute — ejecuta comandos ──────────────────────
    CROW_ROUTE(app, "/execute").methods(crow::HTTPMethod::POST)(
    [](const crow::request& req) {
        crow::response res;
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        res.add_header("Content-Type", "application/json");

        auto body = crow::json::load(req.body);
        if (!body || !body.has("commands")) {
            crow::json::wvalue e;
            e["output"] = "ERROR: JSON invalido";
            res.code = 400; res.body = e.dump(); return res;
        }

        crow::json::wvalue result;
        result["output"] = executeScript(body["commands"].s());
        res.code = 200;
        res.body = result.dump();
        return res;
    });

    // ── OPTIONS /execute — preflight CORS ─────────────────────
    CROW_ROUTE(app, "/execute").methods(crow::HTTPMethod::OPTIONS)(
    [](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res;
    });

    // ── GET /disks ────────────────────────────────────────────
    // Retorna lista de discos conocidos con info básica
    CROW_ROUTE(app, "/disks")([]() {
        crow::response res;
        addCORS(res);

        crow::json::wvalue::list disks;

        for (auto& [path, letter] : diskLetter) {
            if (!fileExists(path)) continue;

            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) continue;

            MBR mbr;
            f.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            f.close();

            crow::json::wvalue disk;
            disk["path"]   = path;
            disk["size"]   = mbr.mbr_tamano;
            disk["fit"]    = std::string(1, mbr.dsk_fit);
            disk["letter"] = std::string(1, letter);

            crow::json::wvalue::list parts;
            for (auto& [id, mp] : mountedPartitions)
                if (mp.path == path) parts.push_back(id);
            disk["partitions"] = std::move(parts);

            disks.push_back(std::move(disk));
        }

        crow::json::wvalue result;
        result["disks"] = std::move(disks);
        res.code = 200;
        res.body = result.dump();
        return res;
    });

    // ── GET /partitions?disk=/ruta/disco.mia ──────────────────
    // Retorna particiones de un disco con info básica
    CROW_ROUTE(app, "/partitions")(
    [](const crow::request& req) {
        crow::response res;
        addCORS(res);

        std::string diskPath = req.url_params.get("disk") ?
                               req.url_params.get("disk") : "";
        if (diskPath.empty()) {
            crow::json::wvalue e; e["error"] = "falta ?disk=";
            res.code = 400; res.body = e.dump(); return res;
        }
        if (!fileExists(diskPath)) {
            crow::json::wvalue e; e["error"] = "disco no encontrado: " + diskPath;
            res.code = 404; res.body = e.dump(); return res;
        }

        std::ifstream f(diskPath, std::ios::binary);
        if (!f.is_open()) {
            crow::json::wvalue e; e["error"] = "no se pudo abrir el disco";
            res.code = 500; res.body = e.dump(); return res;
        }

        MBR mbr;
        f.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        f.close();

        crow::json::wvalue::list parts;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start == -1) continue;

            std::string name(mbr.mbr_partitions[i].part_name,
                             strnlen(mbr.mbr_partitions[i].part_name, 16));
            std::string pid(mbr.mbr_partitions[i].part_id,
                            strnlen(mbr.mbr_partitions[i].part_id, 4));

            crow::json::wvalue part;
            part["name"]   = name;
            part["id"]     = pid;
            part["size"]   = mbr.mbr_partitions[i].part_s;
            part["fit"]    = std::string(1, mbr.mbr_partitions[i].part_fit);
            part["status"] = std::string(1, mbr.mbr_partitions[i].part_status);
            part["type"]   = std::string(1, mbr.mbr_partitions[i].part_type);
            parts.push_back(std::move(part));
        }

        crow::json::wvalue result;
        result["partitions"] = std::move(parts);
        res.code = 200;
        res.body = result.dump();
        return res;
    });

    // ── GET /ls?id=591A&path=/home ────────────────────────────
    // Retorna contenido de una carpeta del sistema de archivos
    CROW_ROUTE(app, "/ls")(
    [](const crow::request& req) {
        crow::response res;
        addCORS(res);

        std::string id   = req.url_params.get("id")   ? req.url_params.get("id")   : "";
        std::string path = req.url_params.get("path") ? req.url_params.get("path") : "/";

        if (id.empty()) {
            crow::json::wvalue e; e["error"] = "falta ?id=";
            res.code = 400; res.body = e.dump(); return res;
        }

        std::fstream file;
        Superblock sb;
        int partStart = openPartition(id, file, sb);
        if (partStart == -1) {
            crow::json::wvalue e; e["error"] = "partición no encontrada o no formateada: " + id;
            res.code = 404; res.body = e.dump(); return res;
        }

        int dirInode = navigatePath(file, sb, path);
        if (dirInode == -1) {
            file.close();
            crow::json::wvalue e; e["error"] = "path no encontrado: " + path;
            res.code = 404; res.body = e.dump(); return res;
        }

        Inode dir;
        file.seekg(sb.s_inode_start + dirInode * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&dir), sizeof(Inode));

        if (dir.i_type != '0') {
            file.close();
            crow::json::wvalue e; e["error"] = "el path no es un directorio";
            res.code = 400; res.body = e.dump(); return res;
        }

        crow::json::wvalue::list entries;
        for (int b = 0; b < 12; b++) {
            if (dir.i_block[b] == -1) continue;
            FolderBlock fb;
            file.seekg(sb.s_block_start + dir.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FolderBlock));

            for (int j = 0; j < 4; j++) {
                int childIdx = fb.b_content[j].b_inodo;
                if (childIdx == -1) continue;

                std::string name(fb.b_content[j].b_name,
                                 strnlen(fb.b_content[j].b_name, 12));
                if (name == "." || name == "..") continue;

                Inode child;
                file.seekg(sb.s_inode_start + childIdx * sb.s_inode_s);
                file.read(reinterpret_cast<char*>(&child), sizeof(Inode));

                crow::json::wvalue entry;
                entry["name"] = name;
                entry["type"] = std::string(1, child.i_type);
                entry["perm"] = std::string(child.i_perm, 3);
                entry["uid"]  = child.i_uid;
                entry["gid"]  = child.i_gid;
                entry["size"] = child.i_s;
                entries.push_back(std::move(entry));
            }
        }

        file.close();
        crow::json::wvalue result;
        result["entries"] = std::move(entries);
        res.code = 200;
        res.body = result.dump();
        return res;
    });

    // ── GET /cat?id=591A&path=/archivo.txt ────────────────────
    // Retorna el contenido de un archivo
    CROW_ROUTE(app, "/cat")(
    [](const crow::request& req) {
        crow::response res;
        addCORS(res);

        std::string id   = req.url_params.get("id")   ? req.url_params.get("id")   : "";
        std::string path = req.url_params.get("path") ? req.url_params.get("path") : "";

        if (id.empty() || path.empty()) {
            crow::json::wvalue e; e["error"] = "faltan ?id= o ?path=";
            res.code = 400; res.body = e.dump(); return res;
        }

        std::fstream file;
        Superblock sb;
        int partStart = openPartition(id, file, sb);
        if (partStart == -1) {
            crow::json::wvalue e; e["error"] = "partición no encontrada: " + id;
            res.code = 404; res.body = e.dump(); return res;
        }

        int fileInode = navigatePath(file, sb, path);
        if (fileInode == -1) {
            file.close();
            crow::json::wvalue e; e["error"] = "archivo no encontrado: " + path;
            res.code = 404; res.body = e.dump(); return res;
        }

        Inode inode;
        file.seekg(sb.s_inode_start + fileInode * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&inode), sizeof(Inode));

        if (inode.i_type != '1') {
            file.close();
            crow::json::wvalue e; e["error"] = "el path no es un archivo";
            res.code = 400; res.body = e.dump(); return res;
        }

        std::string content;
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] == -1) continue;
            FileBlock fb;
            file.seekg(sb.s_block_start + inode.i_block[b] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&fb), sizeof(FileBlock));
            content += std::string(fb.b_content, strnlen(fb.b_content, 64));
        }

        file.close();
        crow::json::wvalue result;
        result["content"] = content;
        res.code = 200;
        res.body = result.dump();
        return res;
    });

    // ── GET /journaling?id=591A ───────────────────────────────
    // Retorna entradas del journal de una partición EXT3
    CROW_ROUTE(app, "/journaling")(
    [](const crow::request& req) {
        crow::response res;
        addCORS(res);

        std::string id = req.url_params.get("id") ? req.url_params.get("id") : "";
        if (id.empty()) {
            crow::json::wvalue e; e["error"] = "falta ?id=";
            res.code = 400; res.body = e.dump(); return res;
        }

        std::map<std::string, std::string> params;
        params["cmd"] = "journaling";
        params["id"]  = id;

        std::string jsonResult = cmdJournaling(params);

        // cmdJournaling retorna JSON array válido directamente
        res.code = 200;
        res.body = jsonResult;
        return res;
    });

    CROW_LOG_INFO << "Servidor en http://localhost:8080";
    app.port(8080).multithreaded().run();
    return 0;
}