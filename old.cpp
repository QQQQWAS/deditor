#include <cmath>
#include <exception>
#include <ncurses.h>
#include <vector>
#include <cstring>
#include <string>
#include <cstdint>
// #include <map>
#include <functional>

#include <readFile/readFile.hpp>

std::string printHex(char* data, char* dataEnd, uint32_t columnLen = 16){
  std::string ret;
  ret.reserve(columnLen*3);
  for(uint32_t i = 0; i < columnLen; i++){
    if(data+i >= dataEnd){
      ret += "   ";
      continue;
    }
    uint8_t c = *(uint8_t*)&data[i];
    if(c == 0){
      ret += "00 ";
      continue;
    }
    char buffer[4];
    sprintf(buffer, "%02x ", c);
    ret += buffer;
  }
  ret.resize(ret.size()-1);
  return ret;
}

std::string printChar(char* data, char* dataEnd, uint32_t columnLen = 16, char empty = '.'){
  std::string ret;
  ret.reserve(columnLen);
  for(uint32_t i = 0; i <= columnLen; i++){
    if(data+i >= dataEnd){
      ret += " ";
      continue;
    }
    uint8_t c = *(uint8_t*)&data[i];
    if(c < 32 || c > 126){
      ret += empty;
      continue;
    }
    char buffer[2];
    sprintf(buffer, "%c", c);
    ret += buffer;
  }
  ret.resize(ret.size()-1);
  return ret;
}

#define COLOR_GRAY 9

#define KEY_SUP 337
#define KEY_SDOWN 336

// SETTING
// uint32_t cursorScrollDist = 5;
//distance from cursor to up/down edges of the window at which the view scrolls

// uint32_t columns = 16;
//number of columns in data view

// std::map<std::string, uint32_t*> settings = {
//   {"cursorScrollDist", &cursorScrollDist},
// }; 

uint32_t t = 1;
#define e(str) {str, t++}

struct Context{
  bool shouldRun = true;
  uint32_t cursorScrollDist = 5;
  size_t currentSTN;
  std::string commandPromptOut;
} context;

struct File{
  std::string path;
  std::string data;
};
std::vector<File> files;

struct SplitTreeNode{
  bool leaf;
  union{
    struct {
      size_t cursor = 0;
      size_t extent = 1;
      bool focus; // false for hexview, true for charview
      uint32_t scroll = 0;
      size_t index;
      uint32_t numColumns = 16;
    } file;
    bool splitType;
  };
  static int computePrintWidth();
  static int print(size_t i, int depth, int x, int y, size_t cursor, int selected);
  static int descend(size_t i, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
  static void split(int i, bool type);
  static bool close(int i);
};
std::vector<SplitTreeNode> splitTree;

enum ColorPairs{
  COLORPAIR_INV = 1,
  COLORPAIR_SEL,
  COLORPAIR_GRAY,
};

void print_select(char* data, uint32_t start, uint32_t extent, int selectPair){
  if(start > 0) //before selection
    printw("%.*s", start, data);
  attron(COLOR_PAIR(selectPair));
  printw("%.*s", extent, data+start);
  attroff(COLOR_PAIR(selectPair));
  if(start + extent < strlen(data))
    printw("%s", data+start+extent);
}

void SplitTreeNode::split(int i, bool type){
  splitTree.insert(splitTree.begin()+i+1, SplitTreeNode{.leaf = true, .file = {}});
  splitTree.insert(splitTree.begin()+i, SplitTreeNode{.leaf = false, .splitType = type});
}

bool SplitTreeNode::close(int i){
  if(splitTree.size() == 1){
    context.shouldRun = false;
    return false;
  }
  // 0 VSplit
  // 1 |-HSplit
  // 2 | |-test0
  // 3 | |-test1
  // 4 |-test2
  //
  // deleting 4
  
  //delete node
  splitTree.erase(splitTree.begin()+i);

  // 0 VSplit
  // 1 |-HSplit
  // 2 | |-test0
  // 3 | |-test1
  // 
  //delete parent split
  bool ret;
  int parentIdx;
  if(splitTree[i-1].leaf){
    ret = 1;
    //if second
    // find split with 1 child 
    int state = 0;
    for(int j = i-1; j >= 0; j--){
      bool found = 0;
      switch(state){
        case 0:
        case 1: //expect a leaf
          state ++;
          if(splitTree[j].leaf == false){
            found = 1;
            parentIdx = j;
          }
          break;
        case 2: //expect a split
          state = 0;
          if(splitTree[j].leaf){
            exit(0); //3 leaves in a row...
          }
          break;
      }
      if(found) break;
    }
  }
  else{
    ret = 0;
    //if first
    parentIdx = i-1;
  }
  splitTree.erase(splitTree.begin()+parentIdx);
  return ret;

}

int SplitTreeNode::print(size_t i, int depth, int x, int y, size_t cursor, int selected){
  SplitTreeNode& stn = splitTree[i];
  move(y, x);
  if(cursor == i) attron(COLOR_PAIR(COLORPAIR_SEL));
  if(depth != 0) printw("%*s", depth*2, "|-");
  printw(" %ld: ", i);
  int lines = 1;
  if(stn.leaf){
    printw("%s", files[stn.file.index].path.data());
    // printw("%*s%s, %d, %d, %d", depth*2, "", files[stn.fileIndex].path.data(), x, y+i, depth);
    if(cursor == i) attroff(COLOR_PAIR(selected));
  }
  else{
    if(stn.splitType) printw("Vsplit");
    else printw("Hsplit");
    if(cursor == i) attroff(COLOR_PAIR(selected));
    lines += SplitTreeNode::print(i+lines, depth+1, x, y+lines, cursor, selected);
    lines += SplitTreeNode::print(i+lines, depth+1, x, y+lines, cursor, selected);
  }
  return lines;
}

int SplitTreeNode::descend(size_t i, uint32_t x, uint32_t y, uint32_t w, uint32_t h){
  SplitTreeNode& stn = splitTree[i];
  int lines = 1;
  if(stn.leaf){
    auto& file = files[stn.file.index];

    //scroll logic
    uint32_t cursorScrollDist = context.cursorScrollDist;
    while(cursorScrollDist*2+1 > h) cursorScrollDist --;
    while(stn.file.cursor/stn.file.numColumns+2 > h + stn.file.scroll - cursorScrollDist){
      stn.file.scroll ++;
    }
    while(stn.file.cursor/stn.file.numColumns < stn.file.scroll + cursorScrollDist && stn.file.scroll > 0){
      stn.file.scroll --;
    }

    //draw
    uint32_t widthNeeded = 9+4*stn.file.numColumns;
    if(w < widthNeeded){
      attron(COLOR_PAIR(COLORPAIR_SEL));
      for(uint32_t i = 0; i < h; i++){
        move(y+i, x);
        printw("|%*s", w-1, "|");
      }
      move(y+h/2, x);
      printw("|Window isn`t wide enough");
      move(y+h/2+1, x);
      printw("|Needs %d, but has %d", widthNeeded, w);
      attroff(COLOR_PAIR(COLORPAIR_SEL));
      // exit(0);
      return lines;
    }
    move(y, x);

    int windowNum = i;
    for(int ib = i-1; ib > -1; ib--){
      if(splitTree[ib].leaf == 0) windowNum--;
    }
    
    printw(" id: %c ", windowNum+'a');
    attron(COLOR_PAIR(COLORPAIR_GRAY));
    for(uint32_t col = 0; col < stn.file.numColumns; col++){
      printw("%02x ", col);
    }
    attroff(COLOR_PAIR(COLORPAIR_GRAY));
    printw("\n");

    // auto hexView = printHex(file.data, stn.file.numColumns);
    // auto charView = printChar(file.data, stn.file.numColumns);
    uint32_t lineCount = file.data.size()/stn.file.numColumns;
    for(uint32_t termLine = 0; termLine < h-1; termLine++){
      move(y+termLine+1, x);
      size_t lineNum = termLine + stn.file.scroll;
      if(lineNum > lineCount) break;
      auto hv = printHex(file.data.data()+lineNum*stn.file.numColumns, file.data.end().base(), stn.file.numColumns);
      auto cv = printChar(file.data.data()+lineNum*stn.file.numColumns, file.data.end().base(), stn.file.numColumns);

      size_t lineCharNum = lineNum*stn.file.numColumns;
      bool hasCurrentCursor = lineCharNum+stn.file.numColumns > stn.file.cursor && lineCharNum < (stn.file.cursor + stn.file.extent);

      attron(COLOR_PAIR(COLORPAIR_GRAY));
      printw("%06x ", (uint32_t)lineCharNum);
      attroff(COLOR_PAIR(COLORPAIR_GRAY));

      if(hasCurrentCursor){
        uint32_t cursorStartLine = stn.file.cursor/stn.file.numColumns;
        uint32_t cursorVOffset = lineNum - cursorStartLine;
        uint32_t cursorLinePos = stn.file.cursor%stn.file.numColumns - cursorVOffset*stn.file.numColumns;
    
        uint32_t extentLinePos = cursorLinePos+stn.file.extent;
        bool firstLine = (stn.file.cursor/stn.file.numColumns==lineNum);
        int selectPair = (context.currentSTN == i)?COLORPAIR_INV:COLORPAIR_SEL;
        print_select(hv.data(), firstLine?cursorLinePos*3:0, firstLine?stn.file.extent*3:extentLinePos*3, selectPair);
        attron(COLOR_PAIR(COLORPAIR_GRAY));
        printw(" | ");
        attroff(COLOR_PAIR(COLORPAIR_GRAY));
        print_select(cv.data(), firstLine?cursorLinePos:0, firstLine?stn.file.extent:extentLinePos, selectPair);
      }
      else{
        printw("%s", hv.data());
        attron(COLOR_PAIR(COLORPAIR_GRAY));
        printw(" | ");
        attroff(COLOR_PAIR(COLORPAIR_GRAY));
        printw("%s", cv.data());
      }
      // printw("\n");
    }
    // printw("\n_                    ");
  }
  else{
    if(stn.splitType){
      lines += SplitTreeNode::descend(i + lines, x,     y, w/2, h);
      lines += SplitTreeNode::descend(i + lines, x+w/2, y, ceilf(w/2.0f), h);
    }
    else{
      lines += SplitTreeNode::descend(i + lines, x, y,     w, h/2);
      lines += SplitTreeNode::descend(i + lines, x, y+h/2, w, ceilf(h/2.0f));      
    }
  }
  return lines;
}

struct Command{
  std::vector<std::string> names;
  std::function<void(std::vector<std::string>& argv)> act;
};

Command commands[] = {
  {{"q", "quit"}, [](std::vector<std::string> argv){
    context.shouldRun = false;
  }},
  {{"c", "columns"}, [](std::vector<std::string>& argv){
    if(argv.size() != 2){
      context.commandPromptOut = "Expected 1 argument, got ";
      context.commandPromptOut += std::to_string(argv.size()-1);
      return;
    }
    try{
      splitTree[context.currentSTN].file.numColumns = std::stoi(argv[1]);
    }
    catch(std::exception& e){
      context.commandPromptOut = "Invalid argument";
    }
  }},
  {{"hs", "hsplit"}, [](std::vector<std::string>& argv){
    SplitTreeNode::split(context.currentSTN, 0);
    context.currentSTN += 1;
  }},
  {{"vs", "vsplit"}, [](std::vector<std::string> argv){
    SplitTreeNode::split(context.currentSTN, 1);
    context.currentSTN += 1;
  }},
  {{"wc", "wclose"}, [](std::vector<std::string>& argv){
    bool second = SplitTreeNode::close(context.currentSTN);
    context.currentSTN -= second;
    context.currentSTN -= 1;
  }},
  {{"rl", "reload"}, [](std::vector<std::string>& argv){
    auto& cstn = splitTree[context.currentSTN];
    auto& cf = files[cstn.file.index];
    cf.data = readFile(cf.path);
  }},
  {{"o", "open"}, [](std::vector<std::string>& argv){
    if(argv.size() != 2){
      context.commandPromptOut = "Expected 1 argument, got ";
      context.commandPromptOut += std::to_string(argv.size()-1);
      return;
    }
    size_t fileIdx;
    for(fileIdx = 0; fileIdx < files.size(); fileIdx++){
      if(files[fileIdx].path == argv[1]){
        break;
      }
    }
    if(fileIdx == files.size()){
      // open new file
      files.push_back(File{
        .path = argv[1],
        .data = readFile(argv[1]),
      });
    }
    splitTree[context.currentSTN].file = {
      .cursor = 0,
      .extent = 1,
      .focus = 0,
      .scroll = 0,
      .index = fileIdx,
      .numColumns = 16,
    };
  }}
};

std::vector<std::string> commandInput(std::string prefix){
  // command input loop
  std::string command = " ";
  size_t cmdCursor = 0;
  bool cmdInputDone = false;
  while(!cmdInputDone){
    //partial clear
    move(LINES-1, 0);
    printw("%*s", COLS, " ");
    
    move(LINES-1, 0);
    printw("%s", prefix.data());
    print_select(command.data(), cmdCursor, 1, COLORPAIR_INV);
    
    int i = getch();
    switch(i){
      case '\n': {
        cmdInputDone = true;
      }; break;

      case KEY_RIGHT: {
        if(cmdCursor < command.size() -1){
          cmdCursor ++;
        }
      }; break;
      case KEY_LEFT: {
        if(cmdCursor > 0){
          cmdCursor --;
        }
      }; break;
      
      case KEY_BACKSPACE: {
        if(cmdCursor > 0){
          command.erase(cmdCursor-1, 1);
          cmdCursor --;
        }
      }; break;
      case KEY_DC: {
        if(cmdCursor < command.size()-1)
          command.erase(cmdCursor, 1);
      }; break;
      case 27:{ //esc
        cmdInputDone = true;
      }; break;
      default: {
        if(i < 127){
          command.insert(cmdCursor, 1, i);
          cmdCursor ++;
        }
        else{
        }
      }; break;
    }
    refresh();
  }
  if(command != ""){
    command.pop_back();
    std::vector<std::string> command_argv;
    for(size_t i = 0; i < command.size();){
      size_t next = command.find(" ", i+1);
      if(next == std::string::npos) next = command.size();
      command_argv.push_back(command.substr(i, next-i));
      i = next +1;
    }
    return command_argv;
  }
  return {};
}

struct DataType{
  std::string name;
  size_t size;
  std::function<void(char* data, size_t& i)> print;
  std::function<void(char* data, size_t& i)> write;
};

std::vector<DataType> dataTypes = {
  DataType{
    "f32", 4,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(float*)(data + i));
    },
  },
  {
    "u8", 1,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(uint8_t*)(data + i));
    },
  },
  {
    "i8", 1,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(int8_t*)(data + i));
    },
  },
  {
    "u16", 2,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(uint16_t*)(data + i));
    },
  },
  {
    "i16", 2,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(int16_t*)(data + i));
    },
  },
  {
    "u32", 4,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(uint32_t*)(data + i));
    },
  },
  {
    "i32", 4,
    [](char* data, size_t& i){
      context.commandPromptOut += std::to_string(*(int32_t*)(data + i));
    },
  },
};

int main(int argc, char** argv){
  if(argc < 2){
    exit(1);
  }
  for(int i = 1; i < argc; i++){
    files.push_back({
      .path = argv[i],
      .data = readFile(argv[i])
    });
  }
  
  initscr();
  start_color();

  init_color(COLOR_GRAY, 400, 400, 400);
  init_pair(COLORPAIR_INV, COLOR_BLACK, COLOR_WHITE);
  init_pair(COLORPAIR_SEL, COLOR_WHITE, COLOR_GRAY);
  init_pair(COLORPAIR_GRAY, COLOR_GRAY, COLOR_BLACK);
  
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);

  splitTree.push_back({.leaf = true, .file = {.index = 0}});
  // exit(0);

  context.currentSTN = 0;
  std::string commandCountInput;

  while(context.shouldRun){
    clear();

    //draw

    uint32_t x, y, w, h;
    x = 0;
    y = 0;
    w = COLS;
    h = LINES-2;

    // SplitTreeNode::print(0, 0, 0, 0);
    SplitTreeNode::descend(0, x, y, w, h);
    // SplitTreeNode::print(0, 0, COLS/2, 25, context.currentSTN, COLORPAIR_SEL);
    move(LINES-2, 0);
    for(size_t i = 0; i < files.size(); i++){
      std::string filename = files[i].path;
      filename = filename.substr(filename.find_last_of('/'));
      if(splitTree[context.currentSTN].file.index == i){
        attron(COLOR_PAIR(COLORPAIR_SEL));
        printw("|%s|", filename.data());
        attroff(COLOR_PAIR(COLORPAIR_SEL));
      }
      else{
        printw(" %s ", filename.data());
      }
    }
    printw("\n%s      %s", context.commandPromptOut.data(), commandCountInput.data());
    
    auto& stn = splitTree[context.currentSTN];
    //input
    context.commandPromptOut = "";
    int in = getch();
    switch(in){
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': {
        commandCountInput.push_back(in);
      }; break;
      case 'r': { //read
        auto command_argv = commandInput("read:");
        if(command_argv.size() == 0){
          break;
        }
        size_t arg = 0;
        size_t amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        
        std::vector<DataType> one;
        bool found;
        for(; arg < command_argv.size(); arg++){
          found = 0;
          for(size_t i = 0; i < dataTypes.size(); i++){
            if(dataTypes[i].name == command_argv[arg]){
              found = 1;
              one.push_back(dataTypes[i]);
              break;
            }
          }
          if(!found){
            context.commandPromptOut = "No such data type: ";
            context.commandPromptOut += command_argv[arg];
            break;
          }
        }
        if(!found) break;

        context.commandPromptOut = "";
        for(size_t i = 0; i < amount; i++){
          for(size_t j = 0; j < one.size(); j++){
            if(stn.file.cursor + one[j].size >= files[stn.file.index].data.size()){
              context.commandPromptOut += "EOF";
              break;
            }
            else{
              one[j].print(files[stn.file.index].data.data(), stn.file.cursor);
              stn.file.extent = one[j].size;
              stn.file.cursor += one[j].size;
            }
            context.commandPromptOut += "; ";
          }          
        }
        
      }; break;
      case 'g': { //goto window
        move(LINES-1, 0);
        printw("Switching to window:%*s", COLS-20, ".");
        int id = getch();
        if(id >= 'a' && id <= 'z'){
          bool found = 0;
          int index = -1;
          for(size_t i = 0; i < splitTree.size(); i++){
            if(splitTree[i].leaf) index ++;
            if(index == id - 'a'){
              context.currentSTN = i;
              context.commandPromptOut = "Switching to window ";
              context.commandPromptOut += id;
              found = 1;
              break;
            }
          }
          if(!found){
            context.commandPromptOut = "Window ";
            context.commandPromptOut += id;
            context.commandPromptOut += " does not exist";
          }
        }
      }; break;
      //movement
      case KEY_RIGHT: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){
          if(stn.file.cursor+stn.file.extent < files[stn.file.index].data.size()){
            stn.file.cursor ++;
          }
        }
      }; break;
      case KEY_LEFT: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){
          if(stn.file.cursor > 0){
            stn.file.cursor --;
          }
        }
      }; break;
      case KEY_DOWN: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){
          if(stn.file.cursor + stn.file.extent - 1 < files[stn.file.index].data.size()-stn.file.numColumns){
            stn.file.cursor += stn.file.numColumns;
          }
        }
      }; break;
      case KEY_UP: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){
          if(stn.file.cursor > stn.file.numColumns-1){
            stn.file.cursor -= stn.file.numColumns;
          }
        }
      }; break;

      case KEY_SRIGHT: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){        
          if(stn.file.cursor+stn.file.extent < files[stn.file.index].data.size()) stn.file.extent ++;
        }
      }; break;
      case KEY_SLEFT: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){        
          if(stn.file.extent > 1) stn.file.extent --;
        }
      }; break;
      case KEY_SDOWN: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){        
          if(stn.file.cursor+stn.file.extent <= files[stn.file.index].data.size()-stn.file.numColumns) stn.file.extent += stn.file.numColumns;
        }
      }; break;
      case KEY_SUP: {
        int amount = 1;
        try{
          amount = std::stoi(commandCountInput);
          commandCountInput.clear();
        }
        catch(std::exception& e){}
        for(int i = 0; i < amount; i++){        
          if(stn.file.extent > stn.file.numColumns) stn.file.extent -= stn.file.numColumns;
        }
      }; break;
      
      case ':': {
        auto command_argv = commandInput(":");
        if(command_argv.size() == 0){
          break;
        }
        // printw("Command: %s", command.data());
        bool found = 0;
        for(Command& c: commands){
          for(std::string& n: c.names){
            if(n == command_argv[0]){
              int amount = 1;
              try{
                amount = std::stoi(commandCountInput);
                commandCountInput.erase();
              }
              catch(std::exception& e){}
              for(int i = 0; i < amount; i++)
                c.act(command_argv);
              found = 1;
              break;
            } 
          }
          if(found) break;
        }
        if(!found) context.commandPromptOut = "Command does not exist";
      }; break;
      default: {
        printw("Key: %d", in);
      }; break;
    }

    refresh();
  }
  endwin();
}
