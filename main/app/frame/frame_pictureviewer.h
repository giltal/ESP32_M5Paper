#ifndef _FRAME_PICTUREVIEWER_H_
#define _FRAME_PICTUREVIEWER_H_

#include "frame_base.h"
#include "../epdgui/epdgui.h"

class Frame_PictureViewer : public Frame_Base {
   public:
    Frame_PictureViewer(std::string path);
    ~Frame_PictureViewer();
    int init(epdgui_args_vector_t &args);
    int run();
    void err(std::string info);

   private:
    EpdCanvas *_canvas_picture;
    std::string _pic_path;
    bool _is_first;
};

#endif  //_FRAME_PICTUREVIEWER_H_