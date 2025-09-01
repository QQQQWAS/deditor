#include <ncurses.h>
#include <cstring>
#include <vector>
#include <string>
#include <cstdint>

#include <readFile/readFile.hpp>

std::vector<std::string> printHex(std::string data, uint32_t columnLen = 16){
  std::vector<std::string> ret(1);
  uint32_t column = 0;
  for(int i = 0; i < data.size(); i++){
    std::string& s = ret[ret.size()-1];
    uint8_t c = *(uint8_t*)&data[i];
    if(c == 0){
      s += "00";
    }
    else{
      char buffer[4];
      sprintf(buffer, "%02x", c);
      s += buffer;
    }
    column ++;
    if(column == columnLen){
      column = 0;
      ret.push_back("");
    }
    else{
      s += ' ';
    }
  }
  if(column < columnLen){
    std::string& s = ret[ret.size()-1];
  }
  while(column < columnLen){
    std::string& s = ret[ret.size()-1];
    s += "   ";
    column ++;
  }
  std::string& s = ret[ret.size()-1];
  s.resize(s.size()-1);
  return ret;
}

std::vector<std::string> printChar(std::string data, uint32_t columnLen = 16, char empty = '.'){
  std::vector<std::string> ret(1);
  uint32_t column = 0;
  for(int i = 0; i < data.size(); i++){
    std::string& s = ret[ret.size()-1];
    uint8_t c = *(uint8_t*)&data[i];
    if(c < 32 || c > 126){
      s += empty;
    }
    else{
      s += *(char*)&c;
    }
    column ++;
    if(column == columnLen){
      column = 0;
      ret.push_back("");
    }
    else{
      // s += ' ';
    }
  }
  return ret;
}

#define COLOR_GRAY 9

enum ColorPairs{
  COLORPAIR_INV = 1,
  COLORPAIR_SEL,
};

int main(int argc, char** argv){
  if(argc < 2){
    printf("Not enough args\nUsage: deditor file\n");
    exit(1);
  }
  std::string filePath = argv[1];
  printf("Opening %s\n", filePath.data());

  std::string fileData = readFile(filePath);
  
  initscr();
  start_color();

  init_color(COLOR_GRAY, 400, 400, 400);
  init_pair(COLORPAIR_INV, COLOR_BLACK, COLOR_WHITE);
  init_pair(COLORPAIR_SEL, COLOR_WHITE, COLOR_GRAY);
  
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);

  size_t cursor = 0;
  int focus = 1; // 0 for hexView, 1 for charView 
  uint32_t columns = 32;
  bool shouldRun = true;
  while(shouldRun){
    clear();

    //draw
    auto hexView = printHex(fileData, columns);
    auto charView = printChar(fileData, columns);
    for(int termLine = 0; termLine < LINES-2; termLine++){
      bool currLineStart = cursor/columns == termLine;
      if(termLine >= hexView.size()) break;
      auto& hv = hexView[termLine];
      auto& cv = charView[termLine];

      if(currLineStart){
        int columnCursorPos = cursor%columns;

        //hex view
        int hvCursorPos = columnCursorPos*3;
        printw("%.*s", hvCursorPos, hv.data());
        attron(COLOR_PAIR(focus == 0?COLORPAIR_INV:COLORPAIR_SEL));
        printw("%.*s", 2, hv.data()+hvCursorPos);
        attroff(COLOR_PAIR(focus == 0?COLORPAIR_INV:COLORPAIR_SEL));
        if(hv.size()-4 > hvCursorPos)
          printw(" %s", hv.data()+hvCursorPos+3);

        printw(" | ");
        
        //char view
        printw("%.*s", columnCursorPos, cv.data());
        attron(COLOR_PAIR(focus == 1?COLORPAIR_INV:COLORPAIR_SEL));
        printw("%.*s", 1, cv.data()+columnCursorPos);
        attroff(COLOR_PAIR(focus == 1?COLORPAIR_INV:COLORPAIR_SEL));
        if(cv.size()-1 > columnCursorPos)
          printw("%s", cv.data()+columnCursorPos+1);
      }
      else{
        printw("%s | %s", hv.data(), cv.data());
      }

      // if(focus == 1 && currLine){
      //   int cursorPos = (cursor%columns);
      //   printw("%.*s", cursorPos, cv.data());
      //   init_pair(1, COLOR_BLACK, COLOR_WHITE);
      //   attron(COLOR_PAIR(1));
      //   // attron(A_BOLD);
      //   printw("%.*s", 1, cv.data()+cursorPos);
      //   // attroff(A_BOLD);
      //   attroff(COLOR_PAIR(1));
      //   if(cv.size()-1 > cursorPos)
      //     printw("%s", cv.data()+cursorPos+1);
      // }
      // else{
      //   printw("%s", cv.data());
      // }
      // else{
      // }
      // printw("%s | %s\n", hexView[i].data(), charView[i].data());        

      printw("\n");
    }

    char bottomBar[COLS];

    attron(COLOR_PAIR(COLORPAIR_SEL));
    printw("%s\n", bottomBar);
    attroff(COLOR_PAIR(COLORPAIR_SEL));
    //input
    int i = getch();
    switch(i){
      //movement
      case KEY_RIGHT: {
        if(cursor < fileData.size()-1) cursor ++;
      }; break;
      case KEY_LEFT: {
        if(cursor > 0) cursor --;
      }; break;
      case KEY_DOWN: {
        if(cursor < fileData.size()-columns) cursor += columns;
      }; break;
      case KEY_UP: {
        if(cursor > columns-1) cursor -= columns;
      }; break;

      // case KEY_SRIGHT: {
      //   if(cursor+extent < fileData.size()-1) extent ++;
      // }; break;
      // case KEY_SLEFT: {
      //   if(extent > 1) extent --;
      // }; break;
      
      case ':': {
        shouldRun = false;
      }; break;
    }

    refresh();
  }
  endwin();
}
