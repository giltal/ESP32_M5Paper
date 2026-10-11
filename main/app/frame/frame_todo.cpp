#include "frame_todo.h"

#include "frame_todoinput.h"
#include "nvs.h"

static const int kRowX       = 4;
static const int kRowY       = 100;
static const int kRowPitch   = 60;  // rows overlap by one line: shared borders
static const int kRowH       = 61;
static const int kItemW      = 472;
static const int kDeleteX    = 476;
static const int kDeleteW    = 60;
static const int kButtonsY   = 790;
static const int kFooterY    = 870;
static const int kTextX      = 60;
static const int kMaxTextW   = kItemW - kTextX - 12;
static const size_t kMaxText = 60;  // bytes per task
// Clock in the title bar, right of the centred title (the title canvas is
// pushed at y = 8 and its rule lines are at y = 70-71).
static const int kClockX = 356;
static const int kClockY = 12;
static const int kClockW = 180;
static const int kClockH = 54;

static int RowY(int idx) {
    return kRowY + idx * kRowPitch;
}

void key_todo_item_cb(epdgui_args_vector_t &args) {
    ((Frame_Todo *)args[0])->Toggle((int)(intptr_t)args[1]);
}

void key_todo_delete_cb(epdgui_args_vector_t &args) {
    ((Frame_Todo *)args[0])->Remove((int)(intptr_t)args[1]);
}

void key_todo_add_cb(epdgui_args_vector_t &args) {
    ((Frame_Todo *)args[0])->OpenInput();
}

void key_todo_clear_cb(epdgui_args_vector_t &args) {
    ((Frame_Todo *)args[0])->ClearDone();
}

Frame_Todo::Frame_Todo(void) {
    _frame_name = "Frame_Todo";
    _language   = GetLanguage();

    for (int i = 0; i < kMaxItems; i++) {
        _key_item[i] = new EPDGUI_Button(kRowX, RowY(i), kItemW, kRowH);
        _key_item[i]->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, this);
        _key_item[i]->AddArgs(EPDGUI_Button::EVENT_RELEASED, 1,
                              (void *)(intptr_t)i);
        _key_item[i]->Bind(EPDGUI_Button::EVENT_RELEASED, key_todo_item_cb);

        _key_delete[i] =
            new EPDGUI_Button("X", kDeleteX, RowY(i), kDeleteW, kRowH);
        _key_delete[i]->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, this);
        _key_delete[i]->AddArgs(EPDGUI_Button::EVENT_RELEASED, 1,
                                (void *)(intptr_t)i);
        _key_delete[i]->Bind(EPDGUI_Button::EVENT_RELEASED, key_todo_delete_cb);
    }

    if (_language == LANGUAGE_JA) {
        _key_add   = new EPDGUI_Button("+ 追加", 4, kButtonsY, 264, 64);
        _key_clear = new EPDGUI_Button("完了を削除", 272, kButtonsY, 264, 64);
        exitbtn("ホーム");
        _canvas_title->drawString("やること", 270, 34);
    } else if (_language == LANGUAGE_ZH) {
        _key_add   = new EPDGUI_Button("+ 添加", 4, kButtonsY, 264, 64);
        _key_clear = new EPDGUI_Button("清除已完成", 272, kButtonsY, 264, 64);
        exitbtn("主页");
        _canvas_title->drawString("待办事项", 270, 34);
    } else {
        _key_add   = new EPDGUI_Button("+ Add", 4, kButtonsY, 264, 64);
        _key_clear = new EPDGUI_Button("Clear done", 272, kButtonsY, 264, 64);
        exitbtn("Home");
        _canvas_title->drawString("To-Do", 270, 34);
    }
    _key_add->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, this);
    _key_add->Bind(EPDGUI_Button::EVENT_RELEASED, key_todo_add_cb);
    _key_clear->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, this);
    _key_clear->Bind(EPDGUI_Button::EVENT_RELEASED, key_todo_clear_cb);

    _key_exit->AddArgs(EPDGUI_Button::EVENT_RELEASED, 0, (void *)(&_is_run));
    _key_exit->Bind(EPDGUI_Button::EVENT_RELEASED, &Frame_Base::exit_cb);

    _blank_row = new EpdCanvas();
    // One line shorter than a row and drawn one line lower (see Refresh), so
    // it never covers the bottom border the row above shares with this slot.
    _blank_row->createCanvas(540, kRowH - 1);
    _blank_row->setTextSize(26);
    _blank_row->setTextColor(8);
    _blank_row->setTextDatum(CC_DATUM);

    _footer = new EpdCanvas();
    _footer->createCanvas(540, 48);
    _footer->setTextSize(26);
    _footer->setTextDatum(CC_DATUM);

    if (!EpdCanvas::isRenderExist(20)) EpdCanvas::createRender(20, 26);
    _clock = new EpdCanvas();
    _clock->createCanvas(kClockW, kClockH);
    _clock->setTextDatum(CR_DATUM);
}

Frame_Todo::~Frame_Todo(void) {
    for (int i = 0; i < kMaxItems; i++) {
        delete _key_item[i];
        delete _key_delete[i];
    }
    delete _key_add;
    delete _key_clear;
    delete _blank_row;
    delete _footer;
    delete _clock;
}

// ---------------------------------------------------------------- storage

// The list lives only on the SD card, in /todo.txt, one task per line:
//   [ ] buy milk
//   [x] call the bank
// Lines without a "[ ] "/"[x] " prefix (typed on a PC) are open tasks.
static const char *kTodoFile = "/todo.txt";

// Older firmware kept the list in NVS (namespace "todo", key "items", one
// task per line prefixed with '0'/'1'). Move it to the card once, then erase.
static void MigrateTodoFromNvs() {
    nvs_handle_t nvs;
    if (nvs_open("todo", NVS_READWRITE, &nvs) != ESP_OK) return;
    size_t len = 0;
    bool done  = true;
    if (nvs_get_str(nvs, "items", NULL, &len) == ESP_OK && len > 1 &&
        !sdcard::exists(kTodoFile)) {
        std::string data(len, '\0');
        if (nvs_get_str(nvs, "items", &data[0], &len) == ESP_OK) {
            data.resize(len - 1);
            std::string text;
            size_t pos = 0;
            while (pos < data.size()) {
                size_t end = data.find('\n', pos);
                if (end == std::string::npos) end = data.size();
                if (end > pos) {
                    text += data[pos] == '1' ? "[x] " : "[ ] ";
                    text += data.substr(pos + 1, end - pos - 1);
                    text += '\n';
                }
                pos = end + 1;
            }
            done = sdcard::write_text(kTodoFile, text);
        }
    }
    if (done) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

bool Frame_Todo::Load() {
    _items.clear();
    if (!sdcard::mounted()) return false;
    MigrateTodoFromNvs();

    std::string text;
    if (!sdcard::read_text(kTodoFile, &text)) return true;  // no list yet
    size_t pos = 0;
    while (pos < text.size() && (int)_items.size() < kMaxItems) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos              = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool done = false;
        if (line.size() >= 4 && line[0] == '[' && line[2] == ']' &&
            line[3] == ' ') {
            done = line[1] == 'x' || line[1] == 'X';
            line.erase(0, 4);
        }
        if (line.empty()) continue;
        _items.push_back({line.substr(0, kMaxText), done});
    }
    return true;
}

bool Frame_Todo::Save() {
    std::string text;
    for (const Item &item : _items) {
        text += item.done ? "[x] " : "[ ] ";
        text += item.text;
        text += '\n';
    }
    return sdcard::write_text(kTodoFile, text);
}

void Frame_Todo::DrawNoCardFooter() {
    if (_language == LANGUAGE_JA) {
        DrawFooter("SDカードに保存できません");
    } else if (_language == LANGUAGE_ZH) {
        DrawFooter("无法保存到SD卡");
    } else {
        DrawFooter("Not saved: no SD card");
    }
}

// ---------------------------------------------------------------- drawing

void Frame_Todo::DrawRow(int idx) {
    const Item &item = _items[idx];
    EpdCanvas *c     = _key_item[idx]->CanvasNormal();
    c->fillCanvas(0);
    c->drawRect(0, 0, kItemW, kRowH, 15);

    // Checkbox, filled with a white tick when done.
    const int bx = 15, by = 14, bs = 32;
    if (item.done) {
        c->fillRect(bx, by, bs, bs, 15);
        for (int t = -2; t <= 2; t++) {
            c->drawLine(bx + 7, by + 16 + t, bx + 13, by + 22 + t, 0);
            c->drawLine(bx + 13, by + 22 + t, bx + 25, by + 9 + t, 0);
        }
    } else {
        for (int t = 0; t < 3; t++) {
            c->drawRect(bx + t, by + t, bs - 2 * t, bs - 2 * t, 15);
        }
    }

    // Text, shortened with "..." to fit; done tasks are gray and struck out.
    c->setTextSize(26);
    c->setTextDatum(CL_DATUM);
    c->setTextColor(item.done ? 8 : 15);
    std::string text = item.text;
    if (c->textWidth(text) > kMaxTextW) {
        while (!text.empty() && c->textWidth(text + "...") > kMaxTextW) {
            // drop one UTF-8 character
            size_t cut = text.size() - 1;
            while (cut > 0 && ((uint8_t)text[cut] & 0xC0) == 0x80) cut--;
            text.erase(cut);
        }
        text += "...";
    }
    c->drawString(text, kTextX, 35);
    if (item.done) {
        c->fillRect(kTextX, 32, c->textWidth(text), 2, 8);
    }

    *(_key_item[idx]->CanvasPressed()) = *c;
    _key_item[idx]->CanvasPressed()->ReverseColor();
}

void Frame_Todo::DrawFooter(const char *message) {
    char buf[64];
    if (message == nullptr) {
        int done = 0;
        for (const Item &item : _items) done += item.done;
        if (_language == LANGUAGE_JA) {
            snprintf(buf, sizeof(buf), "完了 %d / %d", done, (int)_items.size());
        } else if (_language == LANGUAGE_ZH) {
            snprintf(buf, sizeof(buf), "已完成 %d / %d", done, (int)_items.size());
        } else {
            snprintf(buf, sizeof(buf), "%d of %d done", done, (int)_items.size());
        }
        message = buf;
    }
    _footer->fillCanvas(0);
    _footer->drawString(message, 270, 26);
    _footer->pushCanvas(0, kFooterY, UPDATE_MODE_NONE);
}

// Redraw the whole list area and refresh it with `mode`
// (UPDATE_MODE_NONE: leave the refresh to the caller).
void Frame_Todo::Refresh(m5epd_update_mode_t mode) {
    for (int i = 0; i < kMaxItems; i++) {
        bool used = i < (int)_items.size();
        _key_item[i]->SetHide(!used);
        _key_delete[i]->SetHide(!used);
        if (used) {
            DrawRow(i);
            _key_item[i]->Draw(UPDATE_MODE_NONE);
            _key_delete[i]->Draw(UPDATE_MODE_NONE);
        } else {
            _blank_row->fillCanvas(0);
            if (i == 0 && !_has_card) {
                if (_language == LANGUAGE_JA) {
                    _blank_row->drawString("SDカードを挿入してください", 270, 32);
                } else if (_language == LANGUAGE_ZH) {
                    _blank_row->drawString("请插入SD卡", 270, 32);
                } else {
                    _blank_row->drawString("Insert an SD card to use the list", 270,
                                           32);
                }
            } else if (i == 0) {
                if (_language == LANGUAGE_JA) {
                    _blank_row->drawString("やることはありません", 270, 32);
                } else if (_language == LANGUAGE_ZH) {
                    _blank_row->drawString("没有待办事项", 270, 32);
                } else {
                    _blank_row->drawString("Nothing to do. Tap \"+ Add\".", 270,
                                           32);
                }
            }
            _blank_row->pushCanvas(0, RowY(i) + 1, UPDATE_MODE_NONE);
        }
    }
    if (_has_card) {
        DrawFooter();
    } else {
        DrawNoCardFooter();
    }
    if (mode != UPDATE_MODE_NONE) {
        EPD.UpdateArea(0, kRowY, 540, kFooterY + 48 - kRowY, mode);
    }
}

// ---------------------------------------------------------------- actions

// After a change: show the "not saved" footer if the card has gone.
void Frame_Todo::ShowSaveResult(bool saved) {
    if (saved) return;
    DrawNoCardFooter();
    EPD.UpdateArea(0, kFooterY, 540, 48, UPDATE_MODE_GL16);
}

void Frame_Todo::Toggle(int idx) {
    if (idx >= (int)_items.size()) return;
    _items[idx].done = !_items[idx].done;
    bool saved       = Save();
    DrawRow(idx);
    _key_item[idx]->Draw(UPDATE_MODE_GL16);
    DrawFooter();
    EPD.UpdateArea(0, kFooterY, 540, 48, UPDATE_MODE_GL16);
    ShowSaveResult(saved);
}

void Frame_Todo::Remove(int idx) {
    if (idx >= (int)_items.size()) return;
    _items.erase(_items.begin() + idx);
    bool saved = Save();
    Refresh(UPDATE_MODE_GL16);
    ShowSaveResult(saved);
}

void Frame_Todo::ClearDone() {
    size_t before = _items.size();
    for (size_t i = _items.size(); i-- > 0;) {
        if (_items[i].done) _items.erase(_items.begin() + i);
    }
    if (_items.size() != before) {
        bool saved = Save();
        Refresh(UPDATE_MODE_GL16);
        ShowSaveResult(saved);
    }
}

void Frame_Todo::OpenInput() {
    if (!sdcard::mounted()) {
        DrawNoCardFooter();
        EPD.UpdateArea(0, kFooterY, 540, 48, UPDATE_MODE_GL16);
        return;
    }
    if ((int)_items.size() >= kMaxItems) {
        if (_language == LANGUAGE_JA) {
            DrawFooter("リストがいっぱいです");
        } else if (_language == LANGUAGE_ZH) {
            DrawFooter("列表已满");
        } else {
            DrawFooter("The list is full");
        }
        EPD.UpdateArea(0, kFooterY, 540, 48, UPDATE_MODE_GL16);
        return;
    }
    Frame_Base *frame = EPDGUI_GetFrame("Frame_TodoInput");
    if (frame == NULL) {
        frame = new Frame_TodoInput();
        EPDGUI_AddFrame("Frame_TodoInput", frame);
    }
    EPDGUI_PushFrame(frame);
    _is_run = 0;
}

void Frame_Todo::DrawClock(m5epd_update_mode_t mode) {
    static const char *kDayEn[]   = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
    static const char *kMonthEn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *kDayJa[]   = {"日", "月", "火", "水", "木", "金", "土"};
    static const char *kDayZh[]   = {"日", "一", "二", "三", "四", "五", "六"};

    struct tm now = {};
    GetRtcLocalTime(&now);
    int wday = (now.tm_wday >= 0 && now.tm_wday < 7) ? now.tm_wday : 0;
    int mon  = (now.tm_mon >= 0 && now.tm_mon < 12) ? now.tm_mon : 0;

    char time_buf[16], date_buf[48];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d", now.tm_hour, now.tm_min);
    if (_language == LANGUAGE_JA) {
        snprintf(date_buf, sizeof(date_buf), "%d月%d日 (%s)", mon + 1,
                 now.tm_mday, kDayJa[wday]);
    } else if (_language == LANGUAGE_ZH) {
        snprintf(date_buf, sizeof(date_buf), "%d月%d日 周%s", mon + 1,
                 now.tm_mday, kDayZh[wday]);
    } else {
        snprintf(date_buf, sizeof(date_buf), "%s %d %s %d", kDayEn[wday],
                 now.tm_mday, kMonthEn[mon], now.tm_year + 1900);
    }

    _clock->fillCanvas(0);
    _clock->setTextSize(26);
    _clock->drawString(time_buf, kClockW - 4, 15);
    _clock->setTextSize(20);
    _clock->drawString(date_buf, kClockW - 4, 42);
    _clock->pushCanvas(kClockX, kClockY, mode);

    _clock_drawn_at = now_ms();
    _clock_next     = (60 - now.tm_sec) * 1000;
}

int Frame_Todo::run() {
    Frame_Base::run();
    if (now_ms() - _clock_drawn_at >= _clock_next) {
        DrawClock(UPDATE_MODE_GL16);
    }
    return 1;
}

bool Frame_Todo::DrawPowerOffScreen() {
    if (!Load()) return false;
    _has_card = true;
    EPD.Clear();
    _canvas_title->pushCanvas(0, 8, UPDATE_MODE_NONE);
    DrawClock(UPDATE_MODE_NONE);
    Refresh(UPDATE_MODE_NONE);
    return true;
}

int Frame_Todo::init(epdgui_args_vector_t &args) {
    _is_run = 1;

    // Reload every time: the card may have been swapped or edited on a PC.
    _has_card = Load();

    // Text handed back by Frame_TodoInput.
    bool saved = true;
    if (args.size() > 0) {
        std::string *text = (std::string *)(args[0]);
        if (!text->empty() && _has_card && (int)_items.size() < kMaxItems) {
            _items.push_back({text->substr(0, kMaxText), false});
            saved = Save();
        }
        delete text;
        args.pop_back();
    }

    EPD.Clear();
    _canvas_title->pushCanvas(0, 8, UPDATE_MODE_NONE);
    DrawClock(UPDATE_MODE_NONE);
    Refresh(UPDATE_MODE_NONE);
    if (!saved) DrawNoCardFooter();

    EPDGUI_AddObject(_key_exit);
    for (int i = 0; i < kMaxItems; i++) {
        EPDGUI_AddObject(_key_item[i]);
        EPDGUI_AddObject(_key_delete[i]);
    }
    EPDGUI_AddObject(_key_add);
    EPDGUI_AddObject(_key_clear);
    return 3;
}
