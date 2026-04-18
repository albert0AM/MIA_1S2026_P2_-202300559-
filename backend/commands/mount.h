#pragma once
#include <string>
#include <map>

std::string cmdMount(const std::map<std::string,std::string>& p);
std::string cmdMounted();
std::string cmdUnmount(const std::map<std::string,std::string>& p);