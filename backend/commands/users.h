#pragma once
#include <string>
#include <map>

std::string cmdMkgrp(const std::map<std::string,std::string>& p);
std::string cmdRmgrp(const std::map<std::string,std::string>& p);
std::string cmdMkusr(const std::map<std::string,std::string>& p);
std::string cmdRmusr(const std::map<std::string,std::string>& p);
std::string cmdChgrp(const std::map<std::string,std::string>& p);