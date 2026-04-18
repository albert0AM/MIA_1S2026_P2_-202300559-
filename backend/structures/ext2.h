#pragma once
#include <cstdint>
#include <cstring>

#pragma pack(push, 1)

struct Superblock {
    int32_t s_filesystem_type;   // 2 = EXT2, 3 = EXT3
    int32_t s_inodes_count;
    int32_t s_blocks_count;
    int32_t s_free_inodes_count;
    int32_t s_free_blocks_count;
    int32_t s_mtime;
    int32_t s_umtime;
    int32_t s_mnt_count;
    int32_t s_magic;             // 0xEF53
    int32_t s_inode_s;
    int32_t s_block_s;
    int32_t s_firts_ino;
    int32_t s_first_blo;
    int32_t s_bm_inode_start;
    int32_t s_bm_block_start;
    int32_t s_inode_start;
    int32_t s_block_start;
    int32_t s_journal_start;     // inicio del área de journaling (-1 si EXT2)

    Superblock() {
        memset(this, 0, sizeof(Superblock));
        s_filesystem_type = 2;
        s_magic           = 0xEF53;
        s_journal_start   = -1;
    }
};

struct Inode {
    int32_t i_uid;
    int32_t i_gid;
    int32_t i_s;
    int32_t i_atime;
    int32_t i_ctime;
    int32_t i_mtime;
    int32_t i_block[15];
    char    i_type;      // '0'=carpeta  '1'=archivo
    char    i_perm[3];   // "664"

    Inode() {
        memset(this, 0, sizeof(Inode));
        for (int i = 0; i < 15; i++) i_block[i] = -1;
        i_type = '0';
        memcpy(i_perm, "664", 3);
    }
};

struct Content {
    char    b_name[12];
    int32_t b_inodo;

    Content() {
        memset(b_name, 0, sizeof(b_name));
        b_inodo = -1;
    }
};

struct FolderBlock {
    Content b_content[4];
};

struct FileBlock {
    char b_content[64];

    FileBlock() {
        memset(b_content, 0, sizeof(b_content));
    }
};

struct PointerBlock {
    int32_t b_pointers[16];

    PointerBlock() {
        for (int i = 0; i < 16; i++) b_pointers[i] = -1;
    }
};

struct Information {
    char  i_operation[10];
    char  i_path[32];
    char  i_content[64];
    float i_date;

    Information() {
        memset(i_operation, 0, sizeof(i_operation));
        memset(i_path,      0, sizeof(i_path));
        memset(i_content,   0, sizeof(i_content));
        i_date = 0.0f;
    }
};

struct Journal {
    int32_t     j_count;
    Information j_content;

    Journal() {
        j_count = -1;
    }
};

#pragma pack(pop)