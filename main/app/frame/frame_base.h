#ifndef _FRAME_BASE_H_
#define _FRAME_BASE_H_

#include "m5paper_port.h"
#include "../epdgui/epdgui_button.h"
#include "../global_setting.h"
#include "../resources/ImageResource.h"

class Frame_Base {
   public:
    Frame_Base(bool _has_title = true);
    void exitbtn(std::string title, uint16_t width = 150);
    virtual ~Frame_Base();
    virtual int run();
    virtual void exit();
    virtual int init(epdgui_args_vector_t &args) = 0;
    std::string GetFrameName() {
        return _frame_name;
    }
    int isRun() {
        return _is_run;
    }
    void SetFrameID(uint32_t id) {
        _frame_id = id;
    }
    uint32_t GetFrameID() {
        return _frame_id;
    }

   protected:
    static void exit_cb(epdgui_args_vector_t &args);
    void UpdateLastActiveTime();
    std::string _frame_name;
    int _is_run                  = 1;
    EpdCanvas *_canvas_title  = NULL;
    EpdCanvas *_canvas_footer = NULL;
    EPDGUI_Button *_key_exit     = NULL;
    uint32_t _frame_id;

   private:
    bool _shutdown_prompt_is_shown = false;
    void CheckAutoPowerSave();
};

#endif