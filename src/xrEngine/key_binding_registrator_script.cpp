#include "stdafx.h"

#include "xr_level_controller.h"

#include "xrScriptEngine/script_space.hpp"

namespace
{
struct script_constant
{
    pcstr name;
    int value;
};

// Plain Lua tables rather than luabind enum classes: a missing name reads as
// nil, as in shipped CoC (luabind built with LUABIND_NO_ERROR_CHECKING).
// CoC scripts rely on it, e.g. axr_keybind probes key_bindings["DIK_NUMPADENTER"].
void register_constants(lua_State* luaState, pcstr table, std::initializer_list<script_constant> constants)
{
    lua_createtable(luaState, 0, int(constants.size()));
    for (const auto& [name, value] : constants)
    {
        lua_pushinteger(luaState, value);
        lua_setfield(luaState, -2, name);
    }
    lua_setglobal(luaState, table);
}
} // namespace

void key_binding_registrator::script_register(lua_State* luaState)
{
    using namespace luabind;
    module(luaState)
    [
        def("dik_to_bind", +[](int dik) -> int { return GetBindedAction(dik); }),
        def("dik_to_bind", +[](int dik, int ctx) -> int { return GetBindedAction(dik, (EKeyContext)ctx); }),
        def("bind_to_dik", +[](int action) { return GetActionDik((EGameActions)action);}),
        def("bind_to_dik", +[](int action, int idx) { return GetActionDik((EGameActions)action, idx);})
    ];

    register_constants(luaState, "key_bindings_context",
            {
                { "undefined",                  int(EKeyContext::Undefined) },
                { "ui",                         int(EKeyContext::UI) },
                { "pda",                        int(EKeyContext::PDA) },
                { "talk",                       int(EKeyContext::Talk) }
            });

    register_constants(luaState, "key_bindings",
            {
                { "kLOOK_AROUND",               int(kLOOK_AROUND) },
                { "kLEFT",                      int(kLEFT) },
                { "kRIGHT",                     int(kRIGHT) },
                { "kUP",                        int(kUP) },
                { "kDOWN",                      int(kDOWN) },

                { "kMOVE_AROUND",               int(kMOVE_AROUND) },
                { "kFWD",                       int(kFWD) },
                { "kBACK",                      int(kBACK) },
                { "kL_STRAFE",                  int(kL_STRAFE) },
                { "kR_STRAFE",                  int(kR_STRAFE) },

                { "kL_LOOKOUT",                 int(kL_LOOKOUT) },
                { "kR_LOOKOUT",                 int(kR_LOOKOUT) },

                { "kJUMP",                      int(kJUMP) },
                { "kCROUCH",                    int(kCROUCH) },
                { "kCROUCH_TOGGLE",             int(kCROUCH_TOGGLE) },
                { "kACCEL",                     int(kACCEL) },
                { "kSPRINT_TOGGLE",             int(kSPRINT_TOGGLE) },

                { "kENGINE",                    int(kENGINE) },

                { "kCAM_1",                     int(kCAM_1) },
                { "kCAM_2",                     int(kCAM_2) },
                { "kCAM_3",                     int(kCAM_3) },
                { "kCAM_4",                     int(kCAM_4) },
                { "kCAM_ZOOM_IN",               int(kCAM_ZOOM_IN) },
                { "kCAM_ZOOM_OUT",              int(kCAM_ZOOM_OUT) },
                { "kCAM_AUTOAIM",               int(kCAM_AUTOAIM) },

                { "kTORCH",                     int(kTORCH) },
                { "kNIGHT_VISION",              int(kNIGHT_VISION) },
                { "kDETECTOR",                  int(kDETECTOR) },
                { "kWPN_1",                     int(kWPN_1) },
                { "kWPN_2",                     int(kWPN_2) },
                { "kWPN_3",                     int(kWPN_3) },
                { "kWPN_4",                     int(kWPN_4) },
                { "kWPN_5",                     int(kWPN_5) },
                { "kWPN_6",                     int(kWPN_6) },
                { "kARTEFACT",                  int(kARTEFACT) },
                { "kWPN_NEXT",                  int(kWPN_NEXT) },
                { "kWPN_FIRE",                  int(kWPN_FIRE) },
                { "kWPN_ZOOM",                  int(kWPN_ZOOM) },
                { "kWPN_ZOOM_INC",              int(kWPN_ZOOM_INC) },
                { "kWPN_ZOOM_DEC",              int(kWPN_ZOOM_DEC) },
                { "kWPN_RELOAD",                int(kWPN_RELOAD) },
                { "kWPN_FUNC",                  int(kWPN_FUNC) },
                { "kWPN_FIREMODE_PREV",         int(kWPN_FIREMODE_PREV) },
                { "kWPN_FIREMODE_NEXT",         int(kWPN_FIREMODE_NEXT) },

                { "kDROP",                      int(kDROP) },
                { "kUSE",                       int(kUSE) },
                { "kSCORES",                    int(kSCORES) },
                { "kCHAT",                      int(kCHAT) },
                { "kCHAT_TEAM",                 int(kCHAT_TEAM) },
                { "kSCREENSHOT",                int(kSCREENSHOT) },
                { "kENTER",                     int(kENTER) },
                { "kQUIT",                      int(kQUIT) },
                { "kCONSOLE",                   int(kCONSOLE) },
                { "kINVENTORY",                 int(kINVENTORY) },
                { "kBUY",                       int(kBUY) },
                { "kSKIN",                      int(kSKIN) },
                { "kTEAM",                      int(kTEAM) },
                { "kACTIVE_JOBS",               int(kACTIVE_JOBS) },
                { "kMAP",                       int(kMAP) },
                { "kCONTACTS",                  int(kCONTACTS) },
                { "kEXT_1",                     int(kEXT_1) },

                { "kVOTE_BEGIN",                int(kVOTE_BEGIN) },
                { "kSHOW_ADMIN_MENU",           int(kSHOW_ADMIN_MENU) },
                { "kVOTE",                      int(kVOTE) },
                { "kVOTEYES",                   int(kVOTEYES) },
                { "kVOTENO",                    int(kVOTENO) },

                { "kNEXT_SLOT",                 int(kNEXT_SLOT) },
                { "kPREV_SLOT",                 int(kPREV_SLOT) },

                { "kSPEECH_MENU_0",             int(kSPEECH_MENU_0) },
                { "kSPEECH_MENU_1",             int(kSPEECH_MENU_1) },
                { "kSPEECH_MENU_2",             int(kSPEECH_MENU_2) },
                { "kSPEECH_MENU_3",             int(kSPEECH_MENU_3) },
                { "kSPEECH_MENU_4",             int(kSPEECH_MENU_4) },
                { "kSPEECH_MENU_5",             int(kSPEECH_MENU_5) },
                { "kSPEECH_MENU_6",             int(kSPEECH_MENU_6) },
                { "kSPEECH_MENU_7",             int(kSPEECH_MENU_7) },
                { "kSPEECH_MENU_8",             int(kSPEECH_MENU_8) },
                { "kSPEECH_MENU_9",             int(kSPEECH_MENU_9) },

                { "kUSE_BANDAGE",               int(kUSE_BANDAGE) },
                { "kUSE_MEDKIT",                int(kUSE_MEDKIT) },

                { "kQUICK_USE_1",               int(kQUICK_USE_1) },
                { "kQUICK_USE_2",               int(kQUICK_USE_2) },
                { "kQUICK_USE_3",               int(kQUICK_USE_3) },
                { "kQUICK_USE_4",               int(kQUICK_USE_4) },

                { "kQUICK_SAVE",                int(kQUICK_SAVE) },
                { "kQUICK_LOAD",                int(kQUICK_LOAD) },
                { "kALIFE_CMD",                 int(kALIFE_CMD) },

                { "kCUSTOM1",                   int(kCUSTOM1) },
                { "kCUSTOM2",                   int(kCUSTOM2) },
                { "kCUSTOM3",                   int(kCUSTOM3) },
                { "kCUSTOM4",                   int(kCUSTOM4) },
                { "kCUSTOM5",                   int(kCUSTOM5) },
                { "kCUSTOM6",                   int(kCUSTOM6) },
                { "kCUSTOM7",                   int(kCUSTOM7) },
                { "kCUSTOM8",                   int(kCUSTOM8) },
                { "kCUSTOM9",                   int(kCUSTOM9) },
                { "kCUSTOM10",                  int(kCUSTOM10) },
                { "kCUSTOM11",                  int(kCUSTOM11) },
                { "kCUSTOM12",                  int(kCUSTOM12) },
                { "kCUSTOM13",                  int(kCUSTOM13) },
                { "kCUSTOM14",                  int(kCUSTOM14) },
                { "kCUSTOM15",                  int(kCUSTOM15) },

                { "kPDA_TAB1",                  int(kPDA_TAB1) },
                { "kPDA_TAB2",                  int(kPDA_TAB2) },
                { "kPDA_TAB3",                  int(kPDA_TAB3) },
                { "kPDA_TAB4",                  int(kPDA_TAB4) },
                { "kPDA_TAB5",                  int(kPDA_TAB5) },
                { "kPDA_TAB6",                  int(kPDA_TAB6) },

                { "kKICK",                      int(kKICK) },

                { "kEDITOR",                    int(kEDITOR) },

                // Contextual actions:
                // UI
                { "kUI_MOVE",                   int(kUI_MOVE) },
                { "kUI_MOVE_LEFT",              int(kUI_MOVE_LEFT) },
                { "kUI_MOVE_RIGHT",             int(kUI_MOVE_RIGHT) },
                { "kUI_MOVE_UP",                int(kUI_MOVE_UP) },
                { "kUI_MOVE_DOWN",              int(kUI_MOVE_DOWN) },

                { "kUI_MOVE_SECONDARY",         int(kUI_MOVE_SECONDARY) },

                { "kUI_CLICK_1",                int(kUI_CLICK_1) },
                { "kUI_CLICK_2",                int(kUI_CLICK_2) },

                { "kUI_ACCEPT",                 int(kUI_ACCEPT) },
                { "kUI_BACK",                   int(kUI_BACK) },
                { "kUI_ACTION_1",               int(kUI_ACTION_1) },
                { "kUI_ACTION_2",               int(kUI_ACTION_2) },

                { "kUI_TAB_PREV",               int(kUI_TAB_PREV) },
                { "kUI_TAB_NEXT",               int(kUI_TAB_NEXT) },

                { "kUI_BUTTON_1",               int(kUI_BUTTON_1) },
                { "kUI_BUTTON_2",               int(kUI_BUTTON_2) },
                { "kUI_BUTTON_3",               int(kUI_BUTTON_3) },
                { "kUI_BUTTON_4",               int(kUI_BUTTON_4) },
                { "kUI_BUTTON_5",               int(kUI_BUTTON_5) },
                { "kUI_BUTTON_6",               int(kUI_BUTTON_6) },
                { "kUI_BUTTON_7",               int(kUI_BUTTON_7) },
                { "kUI_BUTTON_8",               int(kUI_BUTTON_8) },
                { "kUI_BUTTON_9",               int(kUI_BUTTON_9) },
                { "kUI_BUTTON_0",               int(kUI_BUTTON_0) },

                // PDA:
                { "kPDA_MAP_MOVE",              int(kPDA_MAP_MOVE) },
                { "kPDA_MAP_MOVE_LEFT",         int(kPDA_MAP_MOVE_LEFT) },
                { "kPDA_MAP_MOVE_RIGHT",        int(kPDA_MAP_MOVE_RIGHT) },
                { "kPDA_MAP_MOVE_UP",           int(kPDA_MAP_MOVE_UP) },
                { "kPDA_MAP_MOVE_DOWN",         int(kPDA_MAP_MOVE_DOWN) },

                { "kPDA_MAP_ZOOM_IN",           int(kPDA_MAP_ZOOM_IN) },
                { "kPDA_MAP_ZOOM_OUT",          int(kPDA_MAP_ZOOM_OUT) },
                { "kPDA_MAP_ZOOM_RESET",        int(kPDA_MAP_ZOOM_RESET) },

                { "kPDA_MAP_SHOW_ACTOR",        int(kPDA_MAP_SHOW_ACTOR) },
                { "kPDA_MAP_SHOW_LEGEND",       int(kPDA_MAP_SHOW_LEGEND) },

                { "kPDA_FILTER_TOGGLE",         int(kPDA_FILTER_TOGGLE) },

                // Talk:
                { "kTALK_SWITCH_TO_TRADE",      int(kTALK_SWITCH_TO_TRADE) },
                { "kTALK_LOG_SCROLL",           int(kTALK_LOG_SCROLL) },
                { "kTALK_LOG_SCROLL_UP",        int(kTALK_LOG_SCROLL_UP) },
                { "kTALK_LOG_SCROLL_DOWN",      int(kTALK_LOG_SCROLL_DOWN) }
            });

    register_constants(luaState, "DIK_keys",
            {
                { "DIK_A",                      int(SDL_SCANCODE_A) },
                { "DIK_B",                      int(SDL_SCANCODE_B) },
                { "DIK_C",                      int(SDL_SCANCODE_C) },
                { "DIK_D",                      int(SDL_SCANCODE_D) },
                { "DIK_E",                      int(SDL_SCANCODE_E) },
                { "DIK_F",                      int(SDL_SCANCODE_F) },
                { "DIK_G",                      int(SDL_SCANCODE_G) },
                { "DIK_H",                      int(SDL_SCANCODE_H) },
                { "DIK_I",                      int(SDL_SCANCODE_I) },
                { "DIK_J",                      int(SDL_SCANCODE_J) },
                { "DIK_K",                      int(SDL_SCANCODE_K) },
                { "DIK_L",                      int(SDL_SCANCODE_L) },
                { "DIK_M",                      int(SDL_SCANCODE_M) },
                { "DIK_N",                      int(SDL_SCANCODE_N) },
                { "DIK_O",                      int(SDL_SCANCODE_O) },
                { "DIK_P",                      int(SDL_SCANCODE_P) },
                { "DIK_Q",                      int(SDL_SCANCODE_Q) },
                { "DIK_R",                      int(SDL_SCANCODE_R) },
                { "DIK_S",                      int(SDL_SCANCODE_S) },
                { "DIK_T",                      int(SDL_SCANCODE_T) },
                { "DIK_U",                      int(SDL_SCANCODE_U) },
                { "DIK_V",                      int(SDL_SCANCODE_V) },
                { "DIK_W",                      int(SDL_SCANCODE_W) },
                { "DIK_X",                      int(SDL_SCANCODE_X) },
                { "DIK_Y",                      int(SDL_SCANCODE_Y) },
                { "DIK_Z",                      int(SDL_SCANCODE_Z) },

                { "DIK_1",                      int(SDL_SCANCODE_1) },
                { "DIK_2",                      int(SDL_SCANCODE_2) },
                { "DIK_3",                      int(SDL_SCANCODE_3) },
                { "DIK_4",                      int(SDL_SCANCODE_4) },
                { "DIK_5",                      int(SDL_SCANCODE_5) },
                { "DIK_6",                      int(SDL_SCANCODE_6) },
                { "DIK_7",                      int(SDL_SCANCODE_7) },
                { "DIK_8",                      int(SDL_SCANCODE_8) },
                { "DIK_9",                      int(SDL_SCANCODE_9) },
                { "DIK_0",                      int(SDL_SCANCODE_0) },

                { "DIK_RETURN",                 int(SDL_SCANCODE_RETURN) },
                { "DIK_ESCAPE",                 int(SDL_SCANCODE_ESCAPE) },
                { "DIK_BACK",                   int(SDL_SCANCODE_BACKSPACE) },
                { "DIK_TAB",                    int(SDL_SCANCODE_TAB) },
                { "DIK_SPACE",                  int(SDL_SCANCODE_SPACE) },

                { "DIK_MINUS",                  int(SDL_SCANCODE_MINUS) },
                { "DIK_EQUALS",                 int(SDL_SCANCODE_EQUALS) },
                { "DIK_LBRACKET",               int(SDL_SCANCODE_LEFTBRACKET) },
                { "DIK_RBRACKET",               int(SDL_SCANCODE_RIGHTBRACKET) },
                { "DIK_BACKSLASH",              int(SDL_SCANCODE_BACKSLASH) },
                { "DIK_NONUSHASH",              int(SDL_SCANCODE_NONUSHASH) },

                { "DIK_SEMICOLON",              int(SDL_SCANCODE_SEMICOLON) },
                { "DIK_APOSTROPHE",             int(SDL_SCANCODE_APOSTROPHE) },
                { "DIK_GRAVE",                  int(SDL_SCANCODE_GRAVE) },
                { "DIK_COMMA",                  int(SDL_SCANCODE_COMMA) },
                { "DIK_PERIOD",                 int(SDL_SCANCODE_PERIOD) },
                { "DIK_SLASH",                  int(SDL_SCANCODE_SLASH) },

                { "DIK_CAPITAL",                int(SDL_SCANCODE_CAPSLOCK) },

                { "DIK_F1",                     int(SDL_SCANCODE_F1) },
                { "DIK_F2",                     int(SDL_SCANCODE_F2) },
                { "DIK_F3",                     int(SDL_SCANCODE_F3) },
                { "DIK_F4",                     int(SDL_SCANCODE_F4) },
                { "DIK_F5",                     int(SDL_SCANCODE_F5) },
                { "DIK_F6",                     int(SDL_SCANCODE_F6) },
                { "DIK_F7",                     int(SDL_SCANCODE_F7) },
                { "DIK_F8",                     int(SDL_SCANCODE_F8) },
                { "DIK_F9",                     int(SDL_SCANCODE_F9) },
                { "DIK_F10",                    int(SDL_SCANCODE_F10) },
                { "DIK_F11",                    int(SDL_SCANCODE_F11) },
                { "DIK_F12",                    int(SDL_SCANCODE_F12) },

                { "DIK_PRINTSCREEN",            int(SDL_SCANCODE_PRINTSCREEN) },
                { "DIK_SCROLL",                 int(SDL_SCANCODE_SCROLLLOCK) },
                { "DIK_PAUSE",                  int(SDL_SCANCODE_PAUSE) },
                { "DIK_INSERT",                 int(SDL_SCANCODE_INSERT) },

                { "DIK_HOME",                   int(SDL_SCANCODE_HOME) },
                { "DIK_PGDN",                   int(SDL_SCANCODE_PAGEUP) },
                { "DIK_DELETE",                 int(SDL_SCANCODE_DELETE) },
                { "DIK_END",                    int(SDL_SCANCODE_END) },
                { "DIK_PGUP",                   int(SDL_SCANCODE_PAGEDOWN) },

                { "DIK_RIGHT",                  int(SDL_SCANCODE_RIGHT) },
                { "DIK_LEFT",                   int(SDL_SCANCODE_LEFT) },
                { "DIK_DOWN",                   int(SDL_SCANCODE_DOWN) },
                { "DIK_UP",                     int(SDL_SCANCODE_UP) },

                { "DIK_NUMLOCK",                int(SDL_SCANCODE_NUMLOCKCLEAR) },

                { "DIK_DIVIDE",                 int(SDL_SCANCODE_KP_DIVIDE) },
                { "DIK_MULTIPLY",               int(SDL_SCANCODE_KP_MULTIPLY) },
                { "DIK_SUBTRACT",               int(SDL_SCANCODE_KP_MINUS) },
                { "DIK_ADD",                    int(SDL_SCANCODE_KP_PLUS) },
                { "DIK_NUMPADENTER",            int(SDL_SCANCODE_KP_ENTER) },

                { "DIK_NUMPAD1",                int(SDL_SCANCODE_KP_1) },
                { "DIK_NUMPAD2",                int(SDL_SCANCODE_KP_2) },
                { "DIK_NUMPAD3",                int(SDL_SCANCODE_KP_3) },
                { "DIK_NUMPAD4",                int(SDL_SCANCODE_KP_4) },
                { "DIK_NUMPAD5",                int(SDL_SCANCODE_KP_5) },
                { "DIK_NUMPAD6",                int(SDL_SCANCODE_KP_6) },
                { "DIK_NUMPAD7",                int(SDL_SCANCODE_KP_7) },
                { "DIK_NUMPAD8",                int(SDL_SCANCODE_KP_8) },
                { "DIK_NUMPAD9",                int(SDL_SCANCODE_KP_9) },
                { "DIK_NUMPAD0",                int(SDL_SCANCODE_KP_0) },

                { "DIK_NUMPADPERIOD",           int(SDL_SCANCODE_KP_PERIOD) },
                { "DIK_NONUSBACKSLASH",         int(SDL_SCANCODE_NONUSBACKSLASH) },
                { "DIK_APPLICATION",            int(SDL_SCANCODE_APPLICATION) },
                { "DIK_POWER",                  int(SDL_SCANCODE_POWER) },
                { "DIK_NUMPADEQUALS",           int(SDL_SCANCODE_KP_EQUALS) },

                { "DIK_F13",                    int(SDL_SCANCODE_F13) },
                { "DIK_F14",                    int(SDL_SCANCODE_F14) },
                { "DIK_F15",                    int(SDL_SCANCODE_F15) },
                { "DIK_F16",                    int(SDL_SCANCODE_F16) },
                { "DIK_F17",                    int(SDL_SCANCODE_F17) },
                { "DIK_F18",                    int(SDL_SCANCODE_F18) },
                { "DIK_F19",                    int(SDL_SCANCODE_F19) },
                { "DIK_F20",                    int(SDL_SCANCODE_F20) },
                { "DIK_F21",                    int(SDL_SCANCODE_F21) },
                { "DIK_F22",                    int(SDL_SCANCODE_F22) },
                { "DIK_F23",                    int(SDL_SCANCODE_F23) },
                { "DIK_F24",                    int(SDL_SCANCODE_F24) },

                { "DIK_EXECUTE",                int(SDL_SCANCODE_EXECUTE) },
                { "DIK_HELP",                   int(SDL_SCANCODE_HELP) },
                { "DIK_MENU",                   int(SDL_SCANCODE_MENU) },

                { "DIK_SELECT",                 int(SDL_SCANCODE_SELECT) },
                { "DIK_STOP",                   int(SDL_SCANCODE_STOP) },

                { "DIK_REDO",                   int(SDL_SCANCODE_AGAIN) },
                { "DIK_UNDO",                   int(SDL_SCANCODE_UNDO) },

                { "DIK_CUT",                    int(SDL_SCANCODE_CUT) },
                { "DIK_COPY",                   int(SDL_SCANCODE_COPY) },
                { "DIK_PASTE",                  int(SDL_SCANCODE_PASTE) },

                { "DIK_FIND",                   int(SDL_SCANCODE_FIND) },

                { "DIK_MUTE",                   int(SDL_SCANCODE_MUTE) },
                { "DIK_VOLUMEUP",               int(SDL_SCANCODE_VOLUMEUP) },
                { "DIK_VOLUMEDOWN",             int(SDL_SCANCODE_VOLUMEDOWN) },

                { "DIK_NUMPADCOMMA",            int(SDL_SCANCODE_KP_COMMA) },
                { "DIK_NUMPADEQUALSAS400",      int(SDL_SCANCODE_KP_EQUALSAS400) },

                { "DIK_INTERNATIONAL1",         int(SDL_SCANCODE_INTERNATIONAL1) }, // Give a better name?
                { "DIK_INTERNATIONAL2",         int(SDL_SCANCODE_INTERNATIONAL2) }, // Give a better name?
                { "DIK_YEN",                    int(SDL_SCANCODE_INTERNATIONAL3) },
                { "DIK_INTERNATIONAL4",         int(SDL_SCANCODE_INTERNATIONAL4) }, // Give a better name?
                { "DIK_INTERNATIONAL5",         int(SDL_SCANCODE_INTERNATIONAL5) }, // Give a better name?
                { "DIK_INTERNATIONAL6",         int(SDL_SCANCODE_INTERNATIONAL6) }, // Give a better name?
                { "DIK_INTERNATIONAL7",         int(SDL_SCANCODE_INTERNATIONAL7) }, // Give a better name?
                { "DIK_INTERNATIONAL8",         int(SDL_SCANCODE_INTERNATIONAL8) }, // Give a better name?
                { "DIK_INTERNATIONAL9",         int(SDL_SCANCODE_INTERNATIONAL9) }, // Give a better name?

                { "DIK_HANGUL",                 int(SDL_SCANCODE_LANG1) },
                { "DIK_HANJA",                  int(SDL_SCANCODE_LANG2) },
                { "DIK_KATAKANA",               int(SDL_SCANCODE_LANG3) },
                { "DIK_HIRAGANA",               int(SDL_SCANCODE_LANG4) },
                { "DIK_ZENHANKAKU",             int(SDL_SCANCODE_LANG5) },
                { "DIK_LANG6",                  int(SDL_SCANCODE_LANG6) }, // Give a better name?
                { "DIK_LANG7",                  int(SDL_SCANCODE_LANG7) }, // Give a better name?
                { "DIK_LANG8",                  int(SDL_SCANCODE_LANG8) }, // Give a better name?
                { "DIK_LANG9",                  int(SDL_SCANCODE_LANG9) }, // Give a better name?

                { "DIK_ALTERASE",               int(SDL_SCANCODE_ALTERASE) },
                { "DIK_CANCEL",                 int(SDL_SCANCODE_CANCEL) },
                { "DIK_CLEAR",                  int(SDL_SCANCODE_CLEAR) },
                { "DIK_PRIOR",                  int(SDL_SCANCODE_PRIOR) },
                { "DIK_RETURN2",                int(SDL_SCANCODE_RETURN2) },
                { "DIK_SEPARATOR",              int(SDL_SCANCODE_SEPARATOR) },
                { "DIK_OUT",                    int(SDL_SCANCODE_OUT) },
                { "DIK_OPER",                   int(SDL_SCANCODE_OPER) },
                { "DIK_CLEARAGAIN",             int(SDL_SCANCODE_CLEARAGAIN) },
                { "DIK_CRSEL",                  int(SDL_SCANCODE_CRSEL) },
                { "DIK_EXSEL",                  int(SDL_SCANCODE_EXSEL) },

                { "DIK_NUMPAD_00",              int(SDL_SCANCODE_KP_00) },
                { "DIK_NUMPAD_000",             int(SDL_SCANCODE_KP_000) },
                { "DIK_THOUSANDSSEPARATOR",     int(SDL_SCANCODE_THOUSANDSSEPARATOR) },
                { "DIK_DECIMALSEPARATOR",       int(SDL_SCANCODE_DECIMALSEPARATOR) },
                { "DIK_CURRENCYUNIT",           int(SDL_SCANCODE_CURRENCYUNIT) },
                { "DIK_CURRENCYSUBUNIT",        int(SDL_SCANCODE_CURRENCYSUBUNIT) },

                { "DIK_NUMPAD_LEFTPAREN",       int(SDL_SCANCODE_KP_LEFTPAREN) },
                { "DIK_NUMPAD_RIGHTPAREN",      int(SDL_SCANCODE_KP_RIGHTPAREN) },
                { "DIK_NUMPAD_LEFTBRACE",       int(SDL_SCANCODE_KP_LEFTBRACE) },
                { "DIK_NUMPAD_RIGHTBRACE",      int(SDL_SCANCODE_KP_RIGHTBRACE) },
                { "DIK_NUMPAD_TAB",             int(SDL_SCANCODE_KP_TAB) },
                { "DIK_NUMPAD_BACKSPACE",       int(SDL_SCANCODE_KP_BACKSPACE) },

                { "DIK_NUMPAD_A",               int(SDL_SCANCODE_KP_A) },
                { "DIK_NUMPAD_B",               int(SDL_SCANCODE_KP_B) },
                { "DIK_NUMPAD_C",               int(SDL_SCANCODE_KP_C) },
                { "DIK_NUMPAD_D",               int(SDL_SCANCODE_KP_D) },
                { "DIK_NUMPAD_E",               int(SDL_SCANCODE_KP_E) },
                { "DIK_NUMPAD_F",               int(SDL_SCANCODE_KP_F) },

                { "DIK_NUMPAD_XOR",             int(SDL_SCANCODE_KP_XOR) },

                { "DIK_NUMPAD_POWER",           int(SDL_SCANCODE_KP_POWER) },
                { "DIK_NUMPAD_PERCENT",         int(SDL_SCANCODE_KP_PERCENT) },

                { "DIK_NUMPAD_LESS",            int(SDL_SCANCODE_KP_LESS) },
                { "DIK_NUMPAD_GREATER",         int(SDL_SCANCODE_KP_GREATER) },

                { "DIK_NUMPAD_AMPERSAND",       int(SDL_SCANCODE_KP_AMPERSAND) },
                { "DIK_NUMPAD_DBLAMPERSAND",    int(SDL_SCANCODE_KP_DBLAMPERSAND) },

                { "DIK_NUMPAD_VERTICALBAR",     int(SDL_SCANCODE_KP_VERTICALBAR) },
                { "DIK_NUMPAD_DBLVERTICALBAR",  int(SDL_SCANCODE_KP_DBLVERTICALBAR) },

                { "DIK_NUMPAD_COLON",           int(SDL_SCANCODE_KP_COLON) },
                { "DIK_NUMPAD_HASH",            int(SDL_SCANCODE_KP_HASH) },
                { "DIK_NUMPAD_SPACE",           int(SDL_SCANCODE_KP_SPACE) },
                { "DIK_NUMPAD_AT",              int(SDL_SCANCODE_KP_AT) },
                { "DIK_NUMPAD_EXCLAM",          int(SDL_SCANCODE_KP_EXCLAM) },

                { "DIK_NUMPAD_MEMSTORE",        int(SDL_SCANCODE_KP_MEMSTORE) },
                { "DIK_NUMPAD_MEMRECALL",       int(SDL_SCANCODE_KP_MEMRECALL) },
                { "DIK_NUMPAD_MEMCLEAR",        int(SDL_SCANCODE_KP_MEMCLEAR) },
                { "DIK_NUMPAD_MEMADD",          int(SDL_SCANCODE_KP_MEMADD) },
                { "DIK_NUMPAD_MEMSUBTRACT",     int(SDL_SCANCODE_KP_MEMSUBTRACT) },
                { "DIK_NUMPAD_MEMMULTIPLY",     int(SDL_SCANCODE_KP_MEMMULTIPLY) },
                { "DIK_NUMPAD_MEMDIVIDE",       int(SDL_SCANCODE_KP_MEMDIVIDE) },

                { "DIK_NUMPAD_PLUSMINUS",       int(SDL_SCANCODE_KP_PLUSMINUS) },
                { "DIK_NUMPAD_CLEAR",           int(SDL_SCANCODE_KP_CLEAR) },
                { "DIK_NUMPAD_CLEARENTRY",      int(SDL_SCANCODE_KP_CLEARENTRY) },
                { "DIK_NUMPAD_BINARY",          int(SDL_SCANCODE_KP_BINARY) },
                { "DIK_NUMPAD_OCTAL",           int(SDL_SCANCODE_KP_OCTAL) },
                { "DIK_NUMPAD_DECIMAL",         int(SDL_SCANCODE_KP_DECIMAL) },
                { "DIK_NUMPAD_HEXADECIMAL",     int(SDL_SCANCODE_KP_HEXADECIMAL) },

                { "DIK_LCONTROL",               int(SDL_SCANCODE_LCTRL) },
                { "DIK_LSHIFT",                 int(SDL_SCANCODE_LSHIFT) },
                { "DIK_LMENU",                  int(SDL_SCANCODE_LALT) },
                { "DIK_LWIN",                   int(SDL_SCANCODE_LGUI) },
                { "DIK_RCONTROL",               int(SDL_SCANCODE_RCTRL) },
                { "DIK_RSHIFT",                 int(SDL_SCANCODE_RSHIFT) },
                { "DIK_RMENU",                  int(SDL_SCANCODE_RALT) },
                { "DIK_RWIN",                   int(SDL_SCANCODE_RGUI) },

                { "DIK_MODE",                   int(SDL_SCANCODE_MODE) },

                { "DIK_AUDIONEXT",              int(SDL_SCANCODE_AUDIONEXT) },
                { "DIK_AUDIOPREV",              int(SDL_SCANCODE_AUDIOPREV) },
                { "DIK_AUDIOSTOP",              int(SDL_SCANCODE_AUDIOSTOP) },
                { "DIK_AUDIOPLAY",              int(SDL_SCANCODE_AUDIOPLAY) },
                { "DIK_AUDIOMUTE",              int(SDL_SCANCODE_AUDIOMUTE) },

                { "DIK_NUMPAD_MEDIASELECT",     int(SDL_SCANCODE_MEDIASELECT) },
                { "DIK_NUMPAD_WWW",             int(SDL_SCANCODE_WWW) },
                { "DIK_NUMPAD_MAIL",            int(SDL_SCANCODE_MAIL) },
                { "DIK_NUMPAD_CALCULATOR",      int(SDL_SCANCODE_CALCULATOR) },
                { "DIK_NUMPAD_COMPUTER",        int(SDL_SCANCODE_COMPUTER) },

                { "DIK_NUMPAD_AC_SEARCH",       int(SDL_SCANCODE_AC_SEARCH) },
                { "DIK_NUMPAD_AC_HOME",         int(SDL_SCANCODE_AC_HOME) },
                { "DIK_NUMPAD_AC_BACK",         int(SDL_SCANCODE_AC_BACK) },
                { "DIK_NUMPAD_AC_FORWARD",      int(SDL_SCANCODE_AC_FORWARD) },
                { "DIK_NUMPAD_AC_STOP",         int(SDL_SCANCODE_AC_STOP) },
                { "DIK_NUMPAD_AC_REFRESH",      int(SDL_SCANCODE_AC_REFRESH) },
                { "DIK_NUMPAD_AC_BOOKMARKS",    int(SDL_SCANCODE_AC_BOOKMARKS) },

                { "DIK_BRIGHTNESSDOWN",         int(SDL_SCANCODE_BRIGHTNESSDOWN) },
                { "DIK_BRIGHTNESSUP",           int(SDL_SCANCODE_BRIGHTNESSUP) },
                { "DIK_DISPLAYSWITCH",          int(SDL_SCANCODE_DISPLAYSWITCH) },

                { "DIK_KBDILLUMTOGGLE)",        int(SDL_SCANCODE_KBDILLUMTOGGLE) },
                { "DIK_KBDILLUMDOWN",           int(SDL_SCANCODE_KBDILLUMDOWN) },
                { "DIK_KBDILLUMUP",             int(SDL_SCANCODE_KBDILLUMUP) },

                { "DIK_EJECT",                  int(SDL_SCANCODE_EJECT) },
                { "DIK_SLEEP",                  int(SDL_SCANCODE_SLEEP) },

                { "DIK_APP1",                   int(SDL_SCANCODE_APP1) },
                { "DIK_APP2",                   int(SDL_SCANCODE_APP2) },

                { "MOUSE_1",                    int(MOUSE_1) },
                { "MOUSE_2",                    int(MOUSE_2) },
                { "MOUSE_3",                    int(MOUSE_3) },
                { "MOUSE_4",                    int(MOUSE_4) },
                { "MOUSE_5",                    int(MOUSE_5) },

                { "GAMEPAD_A",                  int(XR_CONTROLLER_BUTTON_A) },
                { "GAMEPAD_B",                  int(XR_CONTROLLER_BUTTON_B) },
                { "GAMEPAD_X",                  int(XR_CONTROLLER_BUTTON_X) },
                { "GAMEPAD_Y",                  int(XR_CONTROLLER_BUTTON_Y) },
                { "GAMEPAD_BACK",               int(XR_CONTROLLER_BUTTON_BACK) },
                { "GAMEPAD_GUIDE",              int(XR_CONTROLLER_BUTTON_GUIDE) },
                { "GAMEPAD_START",              int(XR_CONTROLLER_BUTTON_START) },
                { "GAMEPAD_LEFTSTICK",          int(XR_CONTROLLER_BUTTON_LEFTSTICK) },
                { "GAMEPAD_RIGHTSTICK",         int(XR_CONTROLLER_BUTTON_RIGHTSTICK) },
                { "GAMEPAD_LEFTSHOULDER",       int(XR_CONTROLLER_BUTTON_LEFTSHOULDER) },
                { "GAMEPAD_RIGHTSHOULDER",      int(XR_CONTROLLER_BUTTON_RIGHTSHOULDER) },
                { "GAMEPAD_DPAD_UP",            int(XR_CONTROLLER_BUTTON_DPAD_UP) },
                { "GAMEPAD_DPAD_DOWN",          int(XR_CONTROLLER_BUTTON_DPAD_DOWN) },
                { "GAMEPAD_DPAD_LEFT",          int(XR_CONTROLLER_BUTTON_DPAD_LEFT) },
                { "GAMEPAD_DPAD_RIGHT",         int(XR_CONTROLLER_BUTTON_DPAD_RIGHT) },
                { "GAMEPAD_DPAD_MISC1",         int(XR_CONTROLLER_BUTTON_MISC1) },
                { "GAMEPAD_DPAD_PADDLE1",       int(XR_CONTROLLER_BUTTON_PADDLE1) },
                { "GAMEPAD_DPAD_PADDLE2",       int(XR_CONTROLLER_BUTTON_PADDLE2) },
                { "GAMEPAD_DPAD_PADDLE3",       int(XR_CONTROLLER_BUTTON_PADDLE3) },
                { "GAMEPAD_DPAD_PADDLE4",       int(XR_CONTROLLER_BUTTON_PADDLE4) },
                { "GAMEPAD_DPAD_TOUCHPAD",      int(XR_CONTROLLER_BUTTON_TOUCHPAD) }
            });
}
