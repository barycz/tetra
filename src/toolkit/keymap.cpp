#include "keymap.hpp"

#include <imgui.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace tetra::ui {

int keysym_to_imgui_key(uint32_t keysym) {
    if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z) {
        return ImGuiKey_A + static_cast<int>(keysym - XKB_KEY_a);
    }
    if (keysym >= XKB_KEY_A && keysym <= XKB_KEY_Z) {
        return ImGuiKey_A + static_cast<int>(keysym - XKB_KEY_A);
    }
    if (keysym >= XKB_KEY_0 && keysym <= XKB_KEY_9) {
        return ImGuiKey_0 + static_cast<int>(keysym - XKB_KEY_0);
    }
    if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F24) {
        return ImGuiKey_F1 + static_cast<int>(keysym - XKB_KEY_F1);
    }
    if (keysym >= XKB_KEY_KP_0 && keysym <= XKB_KEY_KP_9) {
        return ImGuiKey_Keypad0 + static_cast<int>(keysym - XKB_KEY_KP_0);
    }
    switch (keysym) {
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
        return ImGuiKey_Tab;
    case XKB_KEY_Left:
        return ImGuiKey_LeftArrow;
    case XKB_KEY_Right:
        return ImGuiKey_RightArrow;
    case XKB_KEY_Up:
        return ImGuiKey_UpArrow;
    case XKB_KEY_Down:
        return ImGuiKey_DownArrow;
    case XKB_KEY_Page_Up:
        return ImGuiKey_PageUp;
    case XKB_KEY_Page_Down:
        return ImGuiKey_PageDown;
    case XKB_KEY_Home:
        return ImGuiKey_Home;
    case XKB_KEY_End:
        return ImGuiKey_End;
    case XKB_KEY_Insert:
        return ImGuiKey_Insert;
    case XKB_KEY_Delete:
        return ImGuiKey_Delete;
    case XKB_KEY_BackSpace:
        return ImGuiKey_Backspace;
    case XKB_KEY_space:
        return ImGuiKey_Space;
    case XKB_KEY_Return:
        return ImGuiKey_Enter;
    case XKB_KEY_KP_Enter:
        return ImGuiKey_KeypadEnter;
    case XKB_KEY_Escape:
        return ImGuiKey_Escape;
    case XKB_KEY_apostrophe:
        return ImGuiKey_Apostrophe;
    case XKB_KEY_comma:
        return ImGuiKey_Comma;
    case XKB_KEY_minus:
        return ImGuiKey_Minus;
    case XKB_KEY_period:
        return ImGuiKey_Period;
    case XKB_KEY_slash:
        return ImGuiKey_Slash;
    case XKB_KEY_semicolon:
        return ImGuiKey_Semicolon;
    case XKB_KEY_equal:
        return ImGuiKey_Equal;
    case XKB_KEY_bracketleft:
        return ImGuiKey_LeftBracket;
    case XKB_KEY_backslash:
        return ImGuiKey_Backslash;
    case XKB_KEY_bracketright:
        return ImGuiKey_RightBracket;
    case XKB_KEY_grave:
        return ImGuiKey_GraveAccent;
    case XKB_KEY_Caps_Lock:
        return ImGuiKey_CapsLock;
    case XKB_KEY_Scroll_Lock:
        return ImGuiKey_ScrollLock;
    case XKB_KEY_Num_Lock:
        return ImGuiKey_NumLock;
    case XKB_KEY_Print:
        return ImGuiKey_PrintScreen;
    case XKB_KEY_Pause:
        return ImGuiKey_Pause;
    case XKB_KEY_KP_Decimal:
        return ImGuiKey_KeypadDecimal;
    case XKB_KEY_KP_Divide:
        return ImGuiKey_KeypadDivide;
    case XKB_KEY_KP_Multiply:
        return ImGuiKey_KeypadMultiply;
    case XKB_KEY_KP_Subtract:
        return ImGuiKey_KeypadSubtract;
    case XKB_KEY_KP_Add:
        return ImGuiKey_KeypadAdd;
    case XKB_KEY_KP_Equal:
        return ImGuiKey_KeypadEqual;
    case XKB_KEY_Shift_L:
        return ImGuiKey_LeftShift;
    case XKB_KEY_Shift_R:
        return ImGuiKey_RightShift;
    case XKB_KEY_Control_L:
        return ImGuiKey_LeftCtrl;
    case XKB_KEY_Control_R:
        return ImGuiKey_RightCtrl;
    case XKB_KEY_Alt_L:
        return ImGuiKey_LeftAlt;
    case XKB_KEY_Alt_R:
        return ImGuiKey_RightAlt;
    case XKB_KEY_Super_L:
        return ImGuiKey_LeftSuper;
    case XKB_KEY_Super_R:
        return ImGuiKey_RightSuper;
    case XKB_KEY_Menu:
        return ImGuiKey_Menu;
    default:
        return ImGuiKey_None;
    }
}

} // namespace tetra::ui
