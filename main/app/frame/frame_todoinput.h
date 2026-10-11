#ifndef _FRAME_TODOINPUT_H_
#define _FRAME_TODOINPUT_H_

#include "frame_base.h"
#include "../epdgui/epdgui.h"

// Keyboard entry for a new task; hands the text to Frame_Todo on confirm.
class Frame_TodoInput : public Frame_Base {
   public:
    Frame_TodoInput();
    ~Frame_TodoInput();
    int run();
    int init(epdgui_args_vector_t &args);

   private:
    EPDGUI_Textbox *inputbox;
    EPDGUI_Keyboard *keyboard;
    EPDGUI_Button *key_textclear;
};

#endif  //_FRAME_TODOINPUT_H_
