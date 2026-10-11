#ifndef _FRAME_TODO_H_
#define _FRAME_TODO_H_

#include <vector>

#include "frame_base.h"
#include "../epdgui/epdgui.h"

// To-do list. Tap a task to mark it done, X deletes it, "+ Add" opens
// Frame_TodoInput. The list is stored on the SD card (/todo.txt) only.
class Frame_Todo : public Frame_Base {
   public:
    static const int kMaxItems = 11;

    Frame_Todo();
    ~Frame_Todo();
    int init(epdgui_args_vector_t &args);

    void Toggle(int idx);
    void Remove(int idx);
    void ClearDone();
    void OpenInput();

    // Draw the list (title, tasks, counts) into the panel memory for the
    // power-off screen; the caller refreshes. False if there is no SD card.
    bool DrawPowerOffScreen();

   private:
    struct Item {
        std::string text;
        bool done;
    };

    bool Load();  // false: no SD card
    bool Save();  // false: not written (no SD card)
    void ShowSaveResult(bool saved);
    void DrawNoCardFooter();
    void DrawRow(int idx);
    void DrawFooter(const char *message = nullptr);
    void Refresh(m5epd_update_mode_t mode);

    std::vector<Item> _items;
    EPDGUI_Button *_key_item[kMaxItems];
    EPDGUI_Button *_key_delete[kMaxItems];
    EPDGUI_Button *_key_add;
    EPDGUI_Button *_key_clear;
    EpdCanvas *_blank_row;
    EpdCanvas *_footer;
    uint8_t _language;
    bool _has_card = false;
};

#endif  //_FRAME_TODO_H_
