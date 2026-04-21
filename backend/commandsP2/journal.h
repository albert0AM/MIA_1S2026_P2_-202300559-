#pragma once
#include <string>
#include <fstream>
#include "../structures/ext2.h"


void writeJournal(std::fstream& file,
                  const Superblock& sb,
                  const std::string& operation,
                  const std::string& path,
                  const std::string& content = "");