#include <algorithm>
#include <cassert>
#include <cstring>
#include <ncurses.h>
#include <string>
#include <vector>
#include <readFile/readFile.hpp>

enum{
  COLORPAIR_INV = 1,
  COLORPAIR_SEL,
  COLORPAIR_GRAY,
};

enum{
  COLOR_GRAY = 9,
};

void init_colors(){
  start_color();
  init_color(COLOR_GRAY, 400, 400, 400);
  init_pair(COLORPAIR_INV, COLOR_BLACK, COLOR_WHITE);
  init_pair(COLORPAIR_SEL, COLOR_BLACK, COLOR_GRAY);
  init_pair(COLORPAIR_GRAY, COLOR_GRAY, COLOR_BLACK);
}

struct File{
  std::string path;
  std::string data;
  std::string name(){
    return path.substr(path.find_last_of('/')+1);
  }
  File(){}
  File(std::string in_path){
    path = in_path;
    data = readFile(path);
  }
};
std::vector<File> files;

struct FileView{
  size_t i;
  size_t cursor;
  size_t scroll = 0;
  uint16_t columns = 16;
};

struct Panel{
  bool isSplit;
  union{
    bool type; //if split
    FileView file;
  };
};
std::vector<Panel> panelTree;

struct Context{
  size_t focus;
  uint16_t scrollPadding = 5;
} ctx;

void moveCursor(size_t d){
  size_t& cursor = panelTree[ctx.focus].file.cursor;
  cursor += d;
  if(cursor >= files[panelTree[ctx.focus].file.i].data.size()-1) cursor -= d; // integer overflow good
}

size_t findParent(size_t i){
  if(i == 0) return 0;
  int state = 0;
  for(int j = i-1; j >= 0; j--){
    bool found = 0;
    switch(state){
      case 0:
      case 1: //expect a leaf
        state ++;
        if(panelTree[j].isSplit == true){
          found = 1;
          return j;
        }
        break;
      case 2: //expect a split
        state = 0;
        if(panelTree[j].isSplit == false){
          return i;
        }
        break;
    }
    if(found) break;
  }
  return i;
}

size_t getSpan(size_t i){
  Panel& pt = panelTree[i];
  if(pt.isSplit){
    size_t iNow = i+1;
    iNow += getSpan(iNow);
    iNow += getSpan(iNow);
    return iNow - i;
  }
  else{
    return 1;
  }  
}

size_t findSibling(size_t d){
  size_t parent = findParent(d);
  if(parent == d - 1){ // first
    return d + getSpan(d);
  }
  else{
    return d+1;
  }
}

void fixFocus(){
  while(panelTree[ctx.focus].isSplit) ctx.focus ++;
}

// Split 0
//   Split 1
//    ./testfile
//    ./testfile
//   Split 1
//    Split 1
//      ./testfile
//      ./testfile
//    ./testfile

size_t moveFocus(int ch, size_t focusInitial){
  size_t parent = findParent(ctx.focus);
  bool first = panelTree[ctx.focus-1].isSplit;
  bool found = 0;
  if(panelTree[parent].type){ // hsplit
    if((ch == KEY_UP && !first) || (ch == KEY_DOWN && first)){
      found = 1;
    }    
  }
  else{
    if((ch == KEY_LEFT && !first) || (ch == KEY_RIGHT && first)){
      found = 1;
    }
  }
  if(found){
    if(first){
      ctx.focus += getSpan(ctx.focus);
    }
    else{
      ctx.focus = parent+1;
    }
    return parent;
  }
  else{
    if(parent == 0){
      ctx.focus = focusInitial;
      return 0;
    }
    ctx.focus = parent;
    return moveFocus(ch, focusInitial);
  }
}

size_t panelTreePrint(size_t i, size_t depth){
  Panel& pt = panelTree[i];
  if(i == ctx.focus) attron(COLOR_PAIR(COLORPAIR_INV));
  printw("%*s", (int)depth, "");
  if(i == ctx.focus) attroff(COLOR_PAIR(COLORPAIR_INV));
  if(pt.isSplit){
    printw("Split %d\n", pt.type);
    size_t iNow = i+1;
    iNow += panelTreePrint(iNow, depth+1);
    iNow += panelTreePrint(iNow, depth+1);
    return iNow - i;
  }
  else{
    printw("%s\n", files[pt.file.i].path.data());
    return 1;
  }
}

// 0: 1     1
// 1:  1     3
// 2:   0     1.
// 3:   1     
// 4:    0     
// 5:    0     
// 6:  1    
// 7:   0   
// 8:   0   

void drawBox(uint32_t x, uint32_t y, uint32_t w, uint32_t h){
  //corners
  move(y, x);
  printw("/");
  move(y, x+w-1);
  printw("\\");
  move(y+h-1, x);
  printw("\\");
  move(y+h-1, x+w-1);
  printw("/");

  // top / bottom
  for(uint32_t i = 1; i < w-1; i++){
    move(y, x+i);
    printw("`");
    move(y+h-1, x+i);
    printw("_");
  }
  // sides
  for(uint32_t i = 1; i < h-1; i++){
    move(y+i, x);
    printw("|");
    move(y+i, x+w-1);
    printw("|");
  }
}

int ceilDiv(int x, int y){
  return (x + y - 1) / y;
}

void printHex(char* data, size_t size, size_t selected, int selectedColor){
  for(size_t i = 0; i < size; i++){
    bool s = i == selected;
    if(s) attron(COLOR_PAIR(selectedColor));
    else if(data[i] == 0) attron(COLOR_PAIR(COLORPAIR_GRAY));
    printw("%02x ", (uint8_t)(data[i]));
    if(s) attroff(COLOR_PAIR(selectedColor));
    else if(data[i] == 0) attroff(COLOR_PAIR(COLORPAIR_GRAY));
  }
}

void printChar(char* data, size_t size, size_t selected, int selectedColor){
  for(size_t i = 0; i < size; i++){
    bool s = i == selected;
    uint8_t c = *(char*)&data[i];
    if(s) attron(COLOR_PAIR(selectedColor));
    if(c < 32 || c > 126){
      if(!s) attron(COLOR_PAIR(COLORPAIR_GRAY));
      printw(".");
      if(!s) attroff(COLOR_PAIR(COLORPAIR_GRAY));
    }
    else{
      printw("%c", c);
    }
    if(s) attroff(COLOR_PAIR(selectedColor));
  }
}

void fileDraw(FileView& fv, uint32_t x, uint32_t y, uint32_t w, uint32_t h){
  File& file = files[fv.i];
  size_t columns = fv.columns*4+3;
  if(w < columns){
    const char msg[] = "Width is too small";
    if(w < strlen(msg)+1){
      for(size_t i = 0; i < strlen(msg); i++){
        move(y+i, x+w/2);
        printw("%c", msg[i]);
      }
    }
    else{
      move(y+h/2, x+1);
      printw("%s", msg);
    }
    return;
  }
  for(size_t line = 0; line < h; line++){
    size_t l = line+fv.scroll;
    size_t ptr = l*panelTree[ctx.focus].file.columns;
    move(y+line, x);
    char* data = file.data.data()+ptr;
    size_t localSelected = fv.cursor-ptr;
    uint16_t remainder = std::min(file.data.size()-ptr-1, (size_t)fv.columns);
    if(remainder == 0) break;

    int sel = (&fv == &panelTree[ctx.focus].file)?COLORPAIR_INV:COLORPAIR_SEL;
    
    printHex(data, remainder, localSelected, sel);
    printw("%*s", (fv.columns-remainder+1)*3-1, "| ");
    printChar(data, remainder, localSelected, sel);

    if(remainder < fv.columns) break;
  }
}

size_t panelTreeDraw(size_t i, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t border = 0, bool drawSplit = false){
  Panel& pt = panelTree[i];
  move(y, x);
  if(pt.isSplit){
    size_t iNow = i+1;
    uint32_t sb = 0;
    if(drawSplit){
      sb = border;
      if(drawSplit && i == ctx.focus) attron(COLOR_PAIR(COLORPAIR_INV));
      drawBox(x, y, w, h);
      if(drawSplit && i == ctx.focus) attroff(COLOR_PAIR(COLORPAIR_INV));
    }
    if(pt.type == 0){
      iNow += panelTreeDraw(iNow, 1*sb+x,     1*sb+y,      -2*sb+w/2,           -2*sb+h,             border, drawSplit);
      iNow += panelTreeDraw(iNow, 1*sb+x+w/2, 1*sb+y,      -2*sb+ceilDiv(w, 2), -2*sb+h,             border, drawSplit);
    }
    else{
      iNow += panelTreeDraw(iNow, 1*sb+x,     1*sb+y,      -2*sb+w,             -2*sb+h/2,           border, drawSplit);
      iNow += panelTreeDraw(iNow, 1*sb+x,     1*sb+y+h/2,  -2*sb+w,             -2*sb+ceilDiv(h, 2), border, drawSplit);
    }
    return iNow - i;
  }
  else{
    size_t& cursor = pt.file.cursor;
    FileView& fv = pt.file;
    uint16_t scrollPadding = ctx.scrollPadding;
    while(scrollPadding > (h-1)/2){
      scrollPadding--;
    }
    while(cursor/fv.columns > fv.scroll + h - 2 - scrollPadding){
      fv.scroll ++;
    }
    while(cursor/fv.columns < fv.scroll + scrollPadding && fv.scroll != 0){
      fv.scroll --;
    }


    if(drawSplit && i == ctx.focus) attron(COLOR_PAIR(COLORPAIR_INV));
    drawBox(x, y, w, h);
    std::string name = files[pt.file.i].name();
    move(y+h-1, x+w-name.size()-3);
    printw(" %s ", name.data());
    if(drawSplit && i == ctx.focus) attroff(COLOR_PAIR(COLORPAIR_INV));

    fileDraw(pt.file, x+drawSplit, y+drawSplit, w-drawSplit*2, h-1-drawSplit*2);

    // move(y+h-1, x);
    // printw("0x%x", fv.cursor);
  }
  return 1;  
}

int main(int argc, char** argv){
  ctx.focus = 0;
  if(argc == 1){
    files.push_back(File());
    panelTree.push_back(Panel{.isSplit = false, .file = {.i = 0}});
  }
  else if(argc == 2){
    files.push_back(File(argv[1]));
    panelTree.push_back(Panel{.isSplit = false, .file = {.i = 0}});    
  }
  else{
    ctx.focus = 1;
    for(int arg = 1; arg < argc; arg++){
    files.push_back(File(argv[1]));
    }
    panelTree.push_back(Panel{.isSplit = true, .type = 0});
    for(size_t i = 0; i < files.size()-2; i++){
      panelTree.push_back(Panel{.isSplit = false, .file = {.i = i}});
      panelTree.push_back(Panel{.isSplit = true, .type = i%2==0});
    }
      panelTree.push_back(Panel{.isSplit = false, .file = {.i = files.size()-2}});
      panelTree.push_back(Panel{.isSplit = false, .file = {.i = files.size()-1}});
  }

  // panelTreePrint(0, 0);

  // exit(0);
  
  initscr();

  init_colors();
  
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);

  bool running = true;
  while(running){
    clear();

    panelTreeDraw(0, 0, 0, COLS, LINES-1);
    // move(LINES/2, 0);

    int ch = getch();
    switch(ch){
      case 'q': {
        running = false;
      }; break;
      case KEY_RIGHT: moveCursor(1); break;
      case KEY_LEFT:  moveCursor(-1); break;
      case KEY_DOWN:  moveCursor(panelTree[ctx.focus].file.columns); break;
      case KEY_UP:    moveCursor(-(int)panelTree[ctx.focus].file.columns); break;
      case 'w': {
        bool running = true;
        while(running){
          panelTreeDraw(0, 0, 0, COLS, LINES-panelTree.size()-1, 1, 1);
          move(LINES-panelTree.size()-1, 0);
          panelTreePrint(0, 2);

          int ch = getch();
          clear();
          switch(ch){
            case 'q':
            case 27: { // esc
              running = false;
            }; break;
            case 'v': { // vsplit
              panelTree.insert(panelTree.begin() + ctx.focus, Panel{.isSplit = false, .file = {0}});
              panelTree.insert(panelTree.begin() + ctx.focus,   Panel{.isSplit = true, .type = 0});
              ctx.focus += 2;
            }; break;
            case 'h': { // vsplit
              panelTree.insert(panelTree.begin() + ctx.focus, Panel{.isSplit = false, .file = {0}});
              panelTree.insert(panelTree.begin() + ctx.focus,   Panel{.isSplit = true, .type = 1});
              ctx.focus += 2;
            }; break;
            case 'c': {
              size_t parent = findParent(ctx.focus);
              panelTree.erase(panelTree.begin() + ctx.focus);
              panelTree.erase(panelTree.begin() + parent);
              ctx.focus = parent;
              // printf("YAY\n");
              // fixFocus();
              // Split
              //   0
              //   1
              if(parent == 0){
                // endwin();
                            
                // exit(0);
              }
            }
            case KEY_RIGHT:
            case KEY_LEFT:
            case KEY_DOWN:
            case KEY_UP: {
              size_t focusBefore = ctx.focus;
              size_t parentBefore = findParent(ctx.focus);
              bool firstBefore = ctx.focus == parentBefore+1;

              size_t jumpedFrom = moveFocus(ch, ctx.focus);
              move(LINES-1, 0);
  
              size_t focusAfter = ctx.focus;
              size_t parentAfter = findParent(ctx.focus);
              bool firstAfter = ctx.focus == parentAfter+1;

              printw(" %3zu | %zu %zu %zu  ", ctx.focus, parentBefore, jumpedFrom, parentAfter);

              fixFocus();

              // break;

              if(parentBefore != parentAfter){
                if(panelTree[parentBefore].type == panelTree[parentBefore].type){
                  if(firstBefore){
                    ctx.focus = findSibling(ctx.focus);
                  }
                }
              }

              // if(panelTree[p1].type == panelTree[ctx.focus].type){
                // if(panelTree[p1].)
              // }
              // else{
              // }
              // moveFocus(ch, ctx.focus);
              // fixFocus();
              // size_t p2 = findParent(ctx.focus);
              // bool f2 = ctx.focus == p2+1;
              // endwin();
              // printf("%d, %d, %d, %d", p1, f1, p2, f2);
              // exit(0);
              // if(p1 != p2 && panelTree[p1].type == panelTree[p2].type && f1 != f2){
                // ctx.focus = findSibling(ctx.focus);
              // }
              break;
            }
            case 't': {
              if(panelTree[ctx.focus].isSplit){
                panelTree[ctx.focus].type = !panelTree[ctx.focus].type;
              }
              else{
                size_t parent = findParent(ctx.focus);
                panelTree[parent].type = !panelTree[parent].type;
              }
            }; break;
            case 'f': fixFocus(); break;
          }
        };
        refresh();
      }; break;
    }
  }
  endwin();
  printf("Focus: %zu\n", ctx.focus);
  for(auto& s: panelTree){
    printf("%d\n", s.isSplit);
  }
}
