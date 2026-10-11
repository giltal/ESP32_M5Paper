#include "frame_todoinput.h"

void key_todoinput_clear_cb(epdgui_args_vector_t &args) {
    ((EPDGUI_Textbox *)(args[0]))->SetText("");
}

Frame_TodoInput::Frame_TodoInput() : Frame_Base() {
    _frame_name      = "Frame_TodoInput";
    uint8_t language = GetLanguage();

    const uint16_t kKeyBaseY = 176;
    inputbox                 = new EPDGUI_Textbox(4, 100, 532, 60);
    if (language == LANGUAGE_JA)
        key_textclear = new EPDGUI_Button("削除", 4, kKeyBaseY, 260, 52);
    else if (language == LANGUAGE_ZH)
        key_textclear = new EPDGUI_Button("清除", 4, kKeyBaseY, 260, 52);
    else
        key_textclear = new EPDGUI_Button("CLR", 4, kKeyBaseY, 260, 52);

    inputbox->SetTextMargin(8, 15, 8, 8);
    inputbox->SetState(EPDGUI_Textbox::EVENT_PRESSED);

    keyboard = new EPDGUI_Keyboard(
        false, EPDGUI_Keyboard::STYLE_INPUTMODE_NEEDCONFIRM);

    key_textclear->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, (void *)inputbox);
    key_textclear->Bind(EPDGUI_Button::EVENT_RELEASED, key_todoinput_clear_cb);

    if (language == LANGUAGE_JA) {
        exitbtn("やること", 200);
        _canvas_title->drawString("新しいタスク", 270, 34);
    } else if (language == LANGUAGE_ZH) {
        exitbtn("待办", 200);
        _canvas_title->drawString("新任务", 270, 34);
    } else {
        exitbtn("To-Do");
        _canvas_title->drawString("New task", 270, 34);
    }

    _key_exit->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, (void *)(&_is_run));
    _key_exit->Bind(EPDGUI_Button::EVENT_RELEASED, &Frame_Base::exit_cb);
}

Frame_TodoInput::~Frame_TodoInput() {
    delete inputbox;
    delete keyboard;
    delete key_textclear;
}

int Frame_TodoInput::init(epdgui_args_vector_t &args) {
    _is_run = 1;
    EPD.Clear();
    _canvas_title->pushCanvas(0, 8, UPDATE_MODE_NONE);
    inputbox->SetText("");
    EPDGUI_AddObject(inputbox);
    EPDGUI_AddObject(keyboard);
    EPDGUI_AddObject(_key_exit);
    EPDGUI_AddObject(key_textclear);
    return 6;
}

int Frame_TodoInput::run(void) {
    Frame_Base::run();
    std::string data = keyboard->getData();
    if (data.find("\n") != std::string::npos) {
        // The keyboard's confirm key: hand the text to the list and go back.
        EPDGUI_AddFrameArg("Frame_Todo", 0, new std::string(inputbox->GetText()));
        inputbox->SetText("");
        EPDGUI_PopFrame();
        _is_run = 0;
        return 0;
    }
    inputbox->AddText(data);
    return 1;
}
