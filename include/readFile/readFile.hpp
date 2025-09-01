#pragma once

#include <string>
#include <ios>

#include <fstream>

std::string readFile(std::string path, std::ios_base::openmode openmode = std::fstream::binary){
  std::ifstream file(path, openmode);
  if(!file){
    printf("%s is not a valid file path\n", path.data());
    return "";
  }
  std::string line, text;
  while (std::getline(file, line)) {
    text.append(line + "\n");
  }
  file.close();
  return text;
}
