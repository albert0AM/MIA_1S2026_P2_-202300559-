#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>

#pragma pack(push, 1)

struct Partition {
    char    part_status;
    char    part_type;
    char    part_fit;
    int32_t part_start;
    int32_t part_s;
    char    part_name[16];
    int32_t part_correlative;
    char    part_id[4];

    Partition() {
        part_status      = '0';
        part_type        = 'P';
        part_fit         = 'W';
        part_start       = -1;
        part_s           = -1;
        part_correlative = -1;
        memset(part_name, 0, sizeof(part_name));
        memset(part_id,   0, sizeof(part_id));
    }
};

struct MBR {
    int32_t   mbr_tamano;
    char      mbr_fecha_creacion[16];
    int32_t   mbr_dsk_signature;
    char      dsk_fit;
    Partition mbr_partitions[4];

    MBR() {
        mbr_tamano         = 0;
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        strftime(mbr_fecha_creacion, sizeof(mbr_fecha_creacion), "%Y-%m-%d %H:%M", timeinfo);
        mbr_dsk_signature  = 0;
        dsk_fit            = 'F';
    }
};

struct EBR {
    char    part_mount;
    char    part_fit;
    int32_t part_start;
    int32_t part_s;
    int32_t part_next;
    char    part_name[16];

    EBR() {
        part_mount = '0';
        part_fit   = 'W';
        part_start = -1;
        part_s     = -1;
        part_next  = -1;
        memset(part_name, 0, sizeof(part_name));
    }
};

#pragma pack(pop)