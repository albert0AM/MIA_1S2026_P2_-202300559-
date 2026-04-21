#include "journal.h"

#include <cstring>
#include <ctime>
#include <algorithm>

// ─────────────────────────────────────────────────────────────
//  writeJournal
//
//  Busca el próximo slot libre (j_count == -1) en el área de
//  journaling. Si todos están ocupados usa rotación circular
//  tomando el de menor j_count (el más antiguo).
//  Solo actúa si la partición es EXT3 (s_journal_start != -1).
// ─────────────────────────────────────────────────────────────
void writeJournal(std::fstream& file,
                  const Superblock& sb,
                  const std::string& operation,
                  const std::string& path,
                  const std::string& content) {

    // Solo EXT3 tiene journaling
    if (sb.s_journal_start == -1) return;

    int journalSize = sizeof(Journal);
    int maxEntries  = 50;

    // ── Leer todas las entradas actuales ──────────────────────
    Journal entries[50];
    for (int i = 0; i < maxEntries; i++) {
        file.seekg(sb.s_journal_start + i * journalSize);
        file.read(reinterpret_cast<char*>(&entries[i]), journalSize);
    }

    // ── Buscar slot libre (j_count == -1) ─────────────────────
    int targetSlot = -1;
    for (int i = 0; i < maxEntries; i++) {
        if (entries[i].j_count == -1) {
            targetSlot = i;
            break;
        }
    }

    // ── Si no hay slot libre, usar el más antiguo (menor j_count) ──
    int nextCount = 0;
    if (targetSlot == -1) {
        int minCount = entries[0].j_count;
        targetSlot   = 0;
        for (int i = 1; i < maxEntries; i++) {
            if (entries[i].j_count < minCount) {
                minCount   = entries[i].j_count;
                targetSlot = i;
            }
        }
        // El nuevo count es el máximo actual + 1
        int maxCount = entries[0].j_count;
        for (int i = 1; i < maxEntries; i++)
            if (entries[i].j_count > maxCount)
                maxCount = entries[i].j_count;
        nextCount = maxCount + 1;
    } else {
        // Calcular el próximo count global
        int maxCount = -1;
        for (int i = 0; i < maxEntries; i++)
            if (entries[i].j_count != -1 && entries[i].j_count > maxCount)
                maxCount = entries[i].j_count;
        nextCount = maxCount + 1;
    }

    // ── Construir la nueva entrada ────────────────────────────
    Journal entry;
    entry.j_count = nextCount;

    // i_operation (máx 10 chars)
    memset(entry.j_content.i_operation, 0, 10);
    strncpy(entry.j_content.i_operation,
            operation.c_str(),
            std::min((int)operation.size(), 9));

    // i_path (máx 32 chars)
    memset(entry.j_content.i_path, 0, 32);
    strncpy(entry.j_content.i_path,
            path.c_str(),
            std::min((int)path.size(), 31));

    // i_content (máx 64 chars)
    memset(entry.j_content.i_content, 0, 64);
    if (!content.empty()) {
        strncpy(entry.j_content.i_content,
                content.c_str(),
                std::min((int)content.size(), 63));
    }

    // i_date: timestamp unix como float
    entry.j_content.i_date = (float)time(nullptr);

    // ── Escribir en disco ─────────────────────────────────────
    file.seekp(sb.s_journal_start + targetSlot * journalSize);
    file.write(reinterpret_cast<char*>(&entry), journalSize);
}