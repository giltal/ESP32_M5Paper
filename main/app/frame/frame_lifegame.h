#ifndef _FRAME_LIFEGAME_H_
#define _FRAME_LIFEGAME_H_

#include "frame_base.h"
#include "../epdgui/epdgui.h"

class Frame_Lifegame : public Frame_Base {
   public:
    Frame_Lifegame();
    ~Frame_Lifegame();
    int init(epdgui_args_vector_t &args);
    int run();
    void exit();

   private:
    EpdCanvas *cells_0;
    EpdCanvas *cells_1;
    EpdCanvas *canvas_0;
    EpdCanvas *canvas_1;
};

#endif  //_FRAME_LIFEGAME_H_