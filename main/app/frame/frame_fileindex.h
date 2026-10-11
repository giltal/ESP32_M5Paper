#ifndef _FRAME_FILEINDEX_H_
#define _FRAME_FILEINDEX_H_

#include "frame_base.h"
#include "../epdgui/epdgui.h"
#include <vector>

class Frame_FileIndex : public Frame_Base {
   public:
    Frame_FileIndex(std::string path);
    ~Frame_FileIndex();
    void listDir(const char *dirname);
    int init(epdgui_args_vector_t &args);

   private:
    std::vector<EPDGUI_Button *> _key_files;
    std::string _path;
};

#endif  //_FRAME_FILEINDEX_H_