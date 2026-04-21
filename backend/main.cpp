#include "vendor/crow/include/crow.h"
#include "utils/utils.h"
#include "commands/mkdisk.h"
#include "commands/rmdisk.h"
#include "commands/fdisk.h" 
#include "commands/mount.h"
#include "structures/globals.h"
#include "commands/mkfs.h"
#include "commands/login.h"
#include  "commands/users.h"
#include  "commands/mkdir.h"
#include  "commands/mkfile.h"
#include  "commands/cat.h"
#include  "commandsP2/rename.h"
#include  "commandsP2/remove.h"
#include  "commandsP2/copy.h"
#include  "commandsP2/find.h"
#include  "commandsP2/move.h"
#include  "commandsP2/chown.h"
#include   "commandsP2/chmod.h"
#include  "commandsP2/journaling.h"
#include  "commandsP2/loss.h"
#include "reports/rep.h"
#include <fstream>
#include <sstream>

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

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

int main() {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/")([]() {
        std::string html = readFile("static/index.html");
        crow::response res(200, html);
        res.add_header("Content-Type", "text/html; charset=UTF-8");
        return res;
    });

    CROW_ROUTE(app, "/health")([]() {
        crow::json::wvalue r;
        r["status"] = "ok";
        return crow::response(200, r);
    });

    CROW_ROUTE(app, "/execute").methods(crow::HTTPMethod::POST)(
    [](const crow::request& req) {
        crow::response res;
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

    CROW_LOG_INFO << "Servidor en http://localhost:8080";
    app.port(8080).multithreaded().run();
    return 0;
}