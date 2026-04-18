#include "globals.h"

std::map<std::string, MountedPartition> mountedPartitions;
std::map<std::string, char>             diskLetter;
std::map<std::string, int>              diskCorrelative;
Session                                 activeSession;