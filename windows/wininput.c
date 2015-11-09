
#include <assert.h>

#include "putty.h"
#include "terminal.h"
#include "wininput.h"

/*
 * Avoid plink crash (kill) on CtrlC keypress
 */
BOOL WINAPI win_ctrlc_handler(DWORD dwCtrlType)
{
	INPUT_RECORD r;
	DWORD written;
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		memset(&r, 0, sizeof(r));
		r.EventType = KEY_EVENT;
		r.Event.KeyEvent.bKeyDown = TRUE;
		r.Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
		r.Event.KeyEvent.uChar.AsciiChar = 3; // CtrlC
		r.Event.KeyEvent.wVirtualKeyCode = 'C';
		WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &r, 1, &written);
		/* we've handled the signal */
		return TRUE;
	case CTRL_BREAK_EVENT:
		/* we've handled (ignored) the signal */
		return TRUE;
	default:
		/* bypass event to the next handler */
		return FALSE;
	}
}

int format_arrow_key2(char *buf, int vt52_mode, int app_cursor_keys, int no_applic_c, int xkey, int ctrl)
{
    char *p = buf;

    if (vt52_mode)
	p += sprintf((char *) p, "\x1B%c", xkey);
    else {
	int app_flg = (app_cursor_keys && !no_applic_c);
#if 0
	/*
	 * RDB: VT100 & VT102 manuals both state the app cursor
	 * keys only work if the app keypad is on.
	 *
	 * SGT: That may well be true, but xterm disagrees and so
	 * does at least one application, so I've #if'ed this out
	 * and the behaviour is back to PuTTY's original: app
	 * cursor and app keypad are independently switchable
	 * modes. If anyone complains about _this_ I'll have to
	 * put in a configurable option.
	 */
	if (!app_keypad_keys)
	    app_flg = 0;
#endif
	/* Useful mapping of Ctrl-arrows */
	if (ctrl)
	    app_flg = !app_flg;

	if (app_flg)
	    p += sprintf((char *) p, "\x1BO%c", xkey);
	else
	    p += sprintf((char *) p, "\x1B[%c", xkey);
    }

    return p - buf;
}

/*
 * Translate a INPUT_RECORD console event into a string of ASCII
 * codes. Returns number of bytes used, zero to drop the message,
 * -1 to forward the message to Windows, or another negative number
 * to indicate a NUL-terminated "special" string.
 */
int TranslateConsoleEvent(INPUT_RECORD* r,
			Conf *conf, int compose_state, void *ldisc,
			unsigned char *output)
{
	BYTE   keystate[256];
	DWORD  dw;
	UINT   message;
	WPARAM wParam;
	LPARAM lParam;
	int    translated = 0;

	switch (r->EventType)
	{
	case KEY_EVENT:
		/* Process only 'key down' events */
		if (r->Event.KeyEvent.uChar.AsciiChar && !r->Event.KeyEvent.bKeyDown)
			break;

		memset(keystate, 0, sizeof(keystate));

		dw = r->Event.KeyEvent.dwControlKeyState;
		keystate[VK_CAPITAL] = (dw & CAPSLOCK_ON) ? 1 : 0;
		keystate[VK_NUMLOCK] = (dw & NUMLOCK_ON) ? 1 : 0;
		keystate[VK_SCROLL] = (dw & SCROLLLOCK_ON) ? 1 : 0;
		keystate[VK_MENU] = (dw & (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED)) ? 1 : 0;
		keystate[VK_LMENU] = (dw & LEFT_ALT_PRESSED) ? 1 : 0;
		keystate[VK_RMENU] = (dw & RIGHT_ALT_PRESSED) ? 1 : 0;
		keystate[VK_CONTROL] = (dw & (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED)) ? 1 : 0;
		keystate[VK_LCONTROL] = (dw & LEFT_CTRL_PRESSED) ? 1 : 0;
		keystate[VK_RCONTROL] = (dw & RIGHT_CTRL_PRESSED) ? 1 : 0;
		keystate[VK_SHIFT] = (dw & (SHIFT_PRESSED)) ? 1 : 0;
		keystate[VK_LSHIFT] = (dw & SHIFT_PRESSED) ? 1 : 0;
		keystate[VK_RSHIFT] = 0;

		message = (r->Event.KeyEvent.bKeyDown)
			? ((dw & (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED)) ? WM_SYSKEYDOWN : WM_KEYDOWN)
			: ((dw & (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED)) ? WM_SYSKEYUP : WM_KEYUP);
		wParam = r->Event.KeyEvent.wVirtualKeyCode;
		lParam = ((r->Event.KeyEvent.bKeyDown) ? 0 : (KF_UP << 16))
			| ((dw & ENHANCED_KEY) ? (KF_EXTENDED << 16) : 0)
			;

		translated = TranslateWinKey(message, wParam, lParam,
			conf, keystate, compose_state, ldisc, output);
		if (translated == twk_FORWARD)
		{
			//TODO: Unicode (CP1200)?
			if (r->Event.KeyEvent.uChar.AsciiChar)
			{
				output[0] = r->Event.KeyEvent.uChar.AsciiChar;
				translated = 1;
			}
			else
			{
				translated = 0;
			}
		}
		break;
	}

	return translated;
}

/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used, zero to drop the message,
 * -1 to forward the message to Windows, or another negative number
 * to indicate a NUL-terminated "special" string.
 */
int TranslateWinKey(UINT message, WPARAM wParam, LPARAM lParam,
			Conf *conf, BYTE* keystate/*[256]*/, int compose_state, void *ldisc,
			unsigned char *output)
{
    //BYTE keystate[256];
    int scan, left_alt = 0, key_down, shift_state;
    //int r, i;
    int code;
    unsigned char *p = output;
    static int alt_sum = 0;
    int funky_type = conf_get_int(conf, CONF_funky_type);
    int no_applic_k = conf_get_int(conf, CONF_no_applic_k);
    int ctrlaltkeys = conf_get_int(conf, CONF_ctrlaltkeys);
    int nethack_keypad = conf_get_int(conf, CONF_nethack_keypad);
	int app_cursor_keys = conf_get_int(conf, CONF_app_cursor);
	int no_applic_c = conf_get_int(conf, CONF_no_applic_c);
	int app_keypad_keys = (keystate[VK_NUMLOCK] & 1); //conf_get_int(conf, CONF_app_keypad);
	int vt52_mode = FALSE;
	int cr_lf_return = FALSE;

    //HKL kbd_layout = GetKeyboardLayout(0);

    static wchar_t keys_unicode[3];
    static int compose_char = 0;
    //static WPARAM compose_keycode = 0;

#if 0
    r = GetKeyboardState(keystate);
    if (!r)
	memset(keystate, 0, sizeof(keystate));
    else {
#if 0
#define SHOW_TOASCII_RESULT
	{			       /* Tell us all about key events */
	    static BYTE oldstate[256];
	    static int first = 1;
	    static int scan;
	    int ch;
	    if (first)
		memcpy(oldstate, keystate, sizeof(oldstate));
	    first = 0;

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT) {
		debug(("+"));
	    } else if ((HIWORD(lParam) & KF_UP)
		       && scan == (HIWORD(lParam) & 0xFF)) {
		debug((". U"));
	    } else {
		debug((".\n"));
		if (wParam >= VK_F1 && wParam <= VK_F20)
		    debug(("K_F%d", wParam + 1 - VK_F1));
		else
		    switch (wParam) {
		      case VK_SHIFT:
			debug(("SHIFT"));
			break;
		      case VK_CONTROL:
			debug(("CTRL"));
			break;
		      case VK_MENU:
			debug(("ALT"));
			break;
		      default:
			debug(("VK_%02x", wParam));
		    }
		if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
		    debug(("*"));
		debug((", S%02x", scan = (HIWORD(lParam) & 0xFF)));

		ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
		if (ch >= ' ' && ch <= '~')
		    debug((", '%c'", ch));
		else if (ch)
		    debug((", $%02x", ch));

		if (keys_unicode[0])
		    debug((", KB0=%04x", keys_unicode[0]));
		if (keys_unicode[1])
		    debug((", KB1=%04x", keys_unicode[1]));
		if (keys_unicode[2])
		    debug((", KB2=%04x", keys_unicode[2]));

		if ((keystate[VK_SHIFT] & 0x80) != 0)
		    debug((", S"));
		if ((keystate[VK_CONTROL] & 0x80) != 0)
		    debug((", C"));
		if ((HIWORD(lParam) & KF_EXTENDED))
		    debug((", E"));
		if ((HIWORD(lParam) & KF_UP))
		    debug((", U"));
	    }

	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT);
	    else if ((HIWORD(lParam) & KF_UP))
		oldstate[wParam & 0xFF] ^= 0x80;
	    else
		oldstate[wParam & 0xFF] ^= 0x81;

	    for (ch = 0; ch < 256; ch++)
		if (oldstate[ch] != keystate[ch])
		    debug((", M%02x=%02x", ch, keystate[ch]));

	    memcpy(oldstate, keystate, sizeof(oldstate));
	}
#endif

	if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED)) {
	    keystate[VK_RMENU] = keystate[VK_MENU];
	}


	/* Nastyness with NUMLock - Shift-NUMLock is left alone though */
	if ((funky_type == FUNKY_VT400 ||
	     (funky_type <= FUNKY_LINUX && app_keypad_keys &&
	      !no_applic_k))
	    && wParam == VK_NUMLOCK && !(keystate[VK_SHIFT] & 0x80)) {

	    wParam = VK_EXECUTE;

	    /* UnToggle NUMLock */
	    if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
		keystate[VK_NUMLOCK] ^= 1;
	}

	/* And write back the 'adjusted' state */
	SetKeyboardState(keystate);
    }
#endif

#if 0
    /* Disable Auto repeat if required */
    if (repeat_off &&
	(HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT)
	return twk_SKIP;
#endif

    if ((HIWORD(lParam) & KF_ALTDOWN) && (keystate[VK_RMENU] & 0x80) == 0)
	left_alt = 1;

    key_down = ((HIWORD(lParam) & KF_UP) == 0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL] & 0x80)) {
	if (ctrlaltkeys)
	    keystate[VK_MENU] = 0;
	else {
	    keystate[VK_RMENU] = 0x80;
	    left_alt = 0;
	}
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT] & 0x80) != 0)
	+ ((keystate[VK_CONTROL] & 0x80) != 0) * 2;

#if 0
    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!compose_state) {
	compose_keycode = 0x100;
	if (conf_get_int(conf, CONF_compose_key)) {
	    if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED))
		compose_keycode = wParam;
	}
	if (wParam == VK_APPS)
	    compose_keycode = wParam;
    }

    if (wParam == compose_keycode) {
	if (compose_state == 0
	    && (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0) compose_state =
		1;
	else if (compose_state == 1 && (HIWORD(lParam) & KF_UP))
	    compose_state = 2;
	else
	    compose_state = 0;
    } else if (compose_state == 1 && wParam != VK_CONTROL)
	compose_state = 0;

    if (compose_state > 1 && left_alt)
	compose_state = 0;
#endif

    /* Sanitize the number pad if not using a PC NumPad */
    if (left_alt || (app_keypad_keys && !no_applic_k
		     && funky_type != FUNKY_XTERM)
	|| funky_type == FUNKY_VT400 || nethack_keypad || compose_state) {
	if ((HIWORD(lParam) & KF_EXTENDED) == 0) {
	    int nParam = 0;
	    switch (wParam) {
	      case VK_INSERT:
		nParam = VK_NUMPAD0;
		break;
	      case VK_END:
		nParam = VK_NUMPAD1;
		break;
	      case VK_DOWN:
		nParam = VK_NUMPAD2;
		break;
	      case VK_NEXT:
		nParam = VK_NUMPAD3;
		break;
	      case VK_LEFT:
		nParam = VK_NUMPAD4;
		break;
	      case VK_CLEAR:
		nParam = VK_NUMPAD5;
		break;
	      case VK_RIGHT:
		nParam = VK_NUMPAD6;
		break;
	      case VK_HOME:
		nParam = VK_NUMPAD7;
		break;
	      case VK_UP:
		nParam = VK_NUMPAD8;
		break;
	      case VK_PRIOR:
		nParam = VK_NUMPAD9;
		break;
	      case VK_DELETE:
		nParam = VK_DECIMAL;
		break;
	    }
	    if (nParam) {
		if (keystate[VK_NUMLOCK] & 1)
		    shift_state |= 1;
		wParam = nParam;
	    }
	}
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU] & 0x80) == 0 && !compose_state) {
	/* Okay, prepare for most alts then ... */
	if (left_alt)
	    *p++ = '\033';

	/* Lets see if it's a pattern we know all about ... */
	if (wParam == VK_PRIOR && shift_state == 1) {
	    //SendMessage(hwnd, WM_VSCROLL, SB_PAGEUP, 0);
	    return twk_PAGEUP;
	}
	if (wParam == VK_PRIOR && shift_state == 2) {
	    //SendMessage(hwnd, WM_VSCROLL, SB_LINEUP, 0);
	    return twk_LINEUP;
	}
	if (wParam == VK_NEXT && shift_state == 1) {
	    //SendMessage(hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
	    return twk_PAGEDOWN;
	}
	if (wParam == VK_NEXT && shift_state == 2) {
	    //SendMessage(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
	    return twk_LINEDOWN;
	}
	if ((wParam == VK_PRIOR || wParam == VK_NEXT) && shift_state == 3) {
	    //term_scroll_to_selection(term, (wParam == VK_PRIOR ? 0 : 1));
	    return twk_SCROLL2SEL;
	}
	if (wParam == VK_INSERT && shift_state == 1) {
	    //request_paste(NULL);
	    return twk_REQPASTE;
	}
	if (left_alt && wParam == VK_F4 && conf_get_int(conf, CONF_alt_f4)) {
	    return twk_FORWARD;
	}
	if (left_alt && wParam == VK_SPACE && conf_get_int(conf,
							   CONF_alt_space)) {
	    //SendMessage(hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
	    return twk_SYSMENU;
	}
	if (left_alt && wParam == VK_RETURN &&
	    conf_get_int(conf, CONF_fullscreenonaltenter) &&
	    (conf_get_int(conf, CONF_resize_action) != RESIZE_DISABLED)) {
 	    //if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) != KF_REPEAT)
 		//flip_full_screen();
	    //return twk_FORWARD;
	    return twk_FULLSCREEN;
	}
	/* Control-Numlock for app-keypad mode switch */
	if (wParam == VK_PAUSE && shift_state == 2) {
	    app_keypad_keys ^= 1;
	    return twk_SKIP;
	}

	/* Nethack keypad */
	if (nethack_keypad && !left_alt) {
	    switch (wParam) {
	      case VK_NUMPAD1:
		*p++ = "bB\002\002"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD2:
		*p++ = "jJ\012\012"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD3:
		*p++ = "nN\016\016"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD4:
		*p++ = "hH\010\010"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD5:
		*p++ = shift_state ? '.' : '.';
		return p - output;
	      case VK_NUMPAD6:
		*p++ = "lL\014\014"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD7:
		*p++ = "yY\031\031"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD8:
		*p++ = "kK\013\013"[shift_state & 3];
		return p - output;
	      case VK_NUMPAD9:
		*p++ = "uU\025\025"[shift_state & 3];
		return p - output;
	    }
	}

	/* Application Keypad */
	if (!left_alt) {
	    int xkey = 0;

	    if (funky_type == FUNKY_VT400 ||
		(funky_type <= FUNKY_LINUX &&
		 app_keypad_keys && !no_applic_k)) switch (wParam) {
		  case VK_EXECUTE:
		    xkey = 'P';
		    break;
		  case VK_DIVIDE:
		    xkey = 'Q';
		    break;
		  case VK_MULTIPLY:
		    xkey = 'R';
		    break;
		  case VK_SUBTRACT:
		    xkey = 'S';
		    break;
		}
	    if (app_keypad_keys && !no_applic_k)
		switch (wParam) {
		  case VK_NUMPAD0:
		    xkey = 'p';
		    break;
		  case VK_NUMPAD1:
		    xkey = 'q';
		    break;
		  case VK_NUMPAD2:
		    xkey = 'r';
		    break;
		  case VK_NUMPAD3:
		    xkey = 's';
		    break;
		  case VK_NUMPAD4:
		    xkey = 't';
		    break;
		  case VK_NUMPAD5:
		    xkey = 'u';
		    break;
		  case VK_NUMPAD6:
		    xkey = 'v';
		    break;
		  case VK_NUMPAD7:
		    xkey = 'w';
		    break;
		  case VK_NUMPAD8:
		    xkey = 'x';
		    break;
		  case VK_NUMPAD9:
		    xkey = 'y';
		    break;

		  case VK_DECIMAL:
		    xkey = 'n';
		    break;
		  case VK_ADD:
		    if (funky_type == FUNKY_XTERM) {
			if (shift_state)
			    xkey = 'l';
			else
			    xkey = 'k';
		    } else if (shift_state)
			xkey = 'm';
		    else
			xkey = 'l';
		    break;

		  case VK_DIVIDE:
		    if (funky_type == FUNKY_XTERM)
			xkey = 'o';
		    break;
		  case VK_MULTIPLY:
		    if (funky_type == FUNKY_XTERM)
			xkey = 'j';
		    break;
		  case VK_SUBTRACT:
		    if (funky_type == FUNKY_XTERM)
			xkey = 'm';
		    break;

		  case VK_RETURN:
		    if (HIWORD(lParam) & KF_EXTENDED)
			xkey = 'M';
		    break;
		}
	    if (xkey) {
		if (vt52_mode) {
		    if (xkey >= 'P' && xkey <= 'S')
			p += sprintf((char *) p, "\x1B%c", xkey);
		    else
			p += sprintf((char *) p, "\x1B?%c", xkey);
		} else
		    p += sprintf((char *) p, "\x1BO%c", xkey);
		return p - output;
	    }
	}

	if (wParam == VK_BACK && shift_state == 0) {	/* Backspace */
	    *p++ = (conf_get_int(conf, CONF_bksp_is_delete) ? 0x7F : 0x08);
	    *p++ = 0;
	    return twk_ASCIIZ;
	}
	if (wParam == VK_BACK && shift_state == 1) {	/* Shift Backspace */
	    /* We do the opposite of what is configured */
	    *p++ = (conf_get_int(conf, CONF_bksp_is_delete) ? 0x08 : 0x7F);
	    *p++ = 0;
	    return twk_ASCIIZ;
	}
	if (wParam == VK_TAB && shift_state == 1) {	/* Shift tab */
	    *p++ = 0x1B;
	    *p++ = '[';
	    *p++ = 'Z';
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 2) {	/* Ctrl-Space */
	    *p++ = 0;
	    return p - output;
	}
	if (wParam == VK_SPACE && shift_state == 3) {	/* Ctrl-Shift-Space */
	    *p++ = 160;
	    return p - output;
	}
	if (wParam == VK_CANCEL && shift_state == 2) {	/* Ctrl-Break */
	    //if (back)
		//back->special(backhandle, TS_BRK);
	    return twk_CTRLBREAK;
	}
	if (wParam == VK_PAUSE) {      /* Break/Pause */
	    *p++ = 26;
	    *p++ = 0;
	    return twk_ASCIIZ;
	}
	/* Control-2 to Control-8 are special */
	if (shift_state == 2 && wParam >= '2' && wParam <= '8') {
	    *p++ = "\000\033\034\035\036\037\177"[wParam - '2'];
	    return p - output;
	}
	if (shift_state == 2 && (wParam == 0xBD || wParam == 0xBF)) {
	    *p++ = 0x1F;
	    return p - output;
	}
	if (shift_state == 2 && (wParam == 0xDF || wParam == 0xDC)) {
	    *p++ = 0x1C;
	    return p - output;
	}
	if (shift_state == 3 && wParam == 0xDE) {
	    *p++ = 0x1E;	       /* Ctrl-~ == Ctrl-^ in xterm at least */
	    return p - output;
	}
	if (shift_state == 0 && wParam == VK_RETURN && cr_lf_return) {
	    *p++ = '\r';
	    *p++ = '\n';
	    return p - output;
	}

	/*
	 * Next, all the keys that do tilde codes. (ESC '[' nn '~',
	 * for integer decimal nn.)
	 *
	 * We also deal with the weird ones here. Linux VCs replace F1
	 * to F5 by ESC [ [ A to ESC [ [ E. rxvt doesn't do _that_, but
	 * does replace Home and End (1~ and 4~) by ESC [ H and ESC O w
	 * respectively.
	 */
	code = 0;
	switch (wParam) {
	  case VK_F1:
	    code = (keystate[VK_SHIFT] & 0x80 ? 23 : 11);
	    break;
	  case VK_F2:
	    code = (keystate[VK_SHIFT] & 0x80 ? 24 : 12);
	    break;
	  case VK_F3:
	    code = (keystate[VK_SHIFT] & 0x80 ? 25 : 13);
	    break;
	  case VK_F4:
	    code = (keystate[VK_SHIFT] & 0x80 ? 26 : 14);
	    break;
	  case VK_F5:
	    code = (keystate[VK_SHIFT] & 0x80 ? 28 : 15);
	    break;
	  case VK_F6:
	    code = (keystate[VK_SHIFT] & 0x80 ? 29 : 17);
	    break;
	  case VK_F7:
	    code = (keystate[VK_SHIFT] & 0x80 ? 31 : 18);
	    break;
	  case VK_F8:
	    code = (keystate[VK_SHIFT] & 0x80 ? 32 : 19);
	    break;
	  case VK_F9:
	    code = (keystate[VK_SHIFT] & 0x80 ? 33 : 20);
	    break;
	  case VK_F10:
	    code = (keystate[VK_SHIFT] & 0x80 ? 34 : 21);
	    break;
	  case VK_F11:
	    code = 23;
	    break;
	  case VK_F12:
	    code = 24;
	    break;
	  case VK_F13:
	    code = 25;
	    break;
	  case VK_F14:
	    code = 26;
	    break;
	  case VK_F15:
	    code = 28;
	    break;
	  case VK_F16:
	    code = 29;
	    break;
	  case VK_F17:
	    code = 31;
	    break;
	  case VK_F18:
	    code = 32;
	    break;
	  case VK_F19:
	    code = 33;
	    break;
	  case VK_F20:
	    code = 34;
	    break;
	}
	if ((shift_state&2) == 0) switch (wParam) {
	  case VK_HOME:
	    code = 1;
	    break;
	  case VK_INSERT:
	    code = 2;
	    break;
	  case VK_DELETE:
	    code = 3;
	    break;
	  case VK_END:
	    code = 4;
	    break;
	  case VK_PRIOR:
	    code = 5;
	    break;
	  case VK_NEXT:
	    code = 6;
	    break;
	}
	/* Reorder edit keys to physical order */
	if (funky_type == FUNKY_VT400 && code <= 6)
	    code = "\0\2\1\4\5\3\6"[code];

	if (vt52_mode && code > 0 && code <= 6) {
	    p += sprintf((char *) p, "\x1B%c", " HLMEIG"[code]);
	    return p - output;
	}

	if (funky_type == FUNKY_SCO && code >= 11 && code <= 34) {
	    /* SCO function keys */
	    char codes[] = "MNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@[\\]^_`{";
	    int index = 0;
	    switch (wParam) {
	      case VK_F1: index = 0; break;
	      case VK_F2: index = 1; break;
	      case VK_F3: index = 2; break;
	      case VK_F4: index = 3; break;
	      case VK_F5: index = 4; break;
	      case VK_F6: index = 5; break;
	      case VK_F7: index = 6; break;
	      case VK_F8: index = 7; break;
	      case VK_F9: index = 8; break;
	      case VK_F10: index = 9; break;
	      case VK_F11: index = 10; break;
	      case VK_F12: index = 11; break;
	    }
	    if (keystate[VK_SHIFT] & 0x80) index += 12;
	    if (keystate[VK_CONTROL] & 0x80) index += 24;
	    p += sprintf((char *) p, "\x1B[%c", codes[index]);
	    return p - output;
	}
	if (funky_type == FUNKY_SCO &&     /* SCO small keypad */
	    code >= 1 && code <= 6) {
	    char codes[] = "HL.FIG";
	    if (code == 3) {
		*p++ = '\x7F';
	    } else {
		p += sprintf((char *) p, "\x1B[%c", codes[code-1]);
	    }
	    return p - output;
	}
	if ((vt52_mode || funky_type == FUNKY_VT100P) && code >= 11 && code <= 24) {
	    int offt = 0;
	    if (code > 15)
		offt++;
	    if (code > 21)
		offt++;
	    if (vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11 - offt);
	    else
		p +=
		    sprintf((char *) p, "\x1BO%c", code + 'P' - 11 - offt);
	    return p - output;
	}
	if (funky_type == FUNKY_LINUX && code >= 11 && code <= 15) {
	    p += sprintf((char *) p, "\x1B[[%c", code + 'A' - 11);
	    return p - output;
	}
	if (funky_type == FUNKY_XTERM && code >= 11 && code <= 14) {
	    if (vt52_mode)
		p += sprintf((char *) p, "\x1B%c", code + 'P' - 11);
	    else
		p += sprintf((char *) p, "\x1BO%c", code + 'P' - 11);
	    return p - output;
	}
	if ((code == 1 || code == 4) &&
	    conf_get_int(conf, CONF_rxvt_homeend)) {
	    p += sprintf((char *) p, code == 1 ? "\x1B[H" : "\x1BOw");
	    return p - output;
	}
	if (code) {
	    p += sprintf((char *) p, "\x1B[%d~", code);
	    return p - output;
	}

	/*
	 * Now the remaining keys (arrows and Keypad 5. Keypad 5 for
	 * some reason seems to send VK_CLEAR to Windows...).
	 */
	{
	    char xkey = 0;
	    switch (wParam) {
	      case VK_UP:
		xkey = 'A';
		break;
	      case VK_DOWN:
		xkey = 'B';
		break;
	      case VK_RIGHT:
		xkey = 'C';
		break;
	      case VK_LEFT:
		xkey = 'D';
		break;
	      case VK_CLEAR:
		xkey = 'G';
		break;
	    }
	    if (xkey) {
		p += format_arrow_key2(p, vt52_mode, app_cursor_keys, no_applic_c, xkey, shift_state);
		return p - output;
	    }
	}

	/*
	 * Finally, deal with Return ourselves. (Win95 seems to
	 * foul it up when Alt is pressed, for some reason.)
	 */
	if (wParam == VK_RETURN) {     /* Return */
	    *p++ = 0x0D;
	    *p++ = 0;
	    return twk_ASCIIZ;
	}

	if (left_alt && wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
	    alt_sum = alt_sum * 10 + wParam - VK_NUMPAD0;
	else
	    alt_sum = 0;
    }

#if 0
    /* Okay we've done everything interesting; let windows deal with 
     * the boring stuff */
    {
	BOOL capsOn=0;

	/* helg: clear CAPS LOCK state if caps lock switches to cyrillic */
	if(keystate[VK_CAPITAL] != 0 &&
	   conf_get_int(conf, CONF_xlat_capslockcyr)) {
	    capsOn= !left_alt;
	    keystate[VK_CAPITAL] = 0;
	}

	/* XXX how do we know what the max size of the keys array should
	 * be is? There's indication on MS' website of an Inquire/InquireEx
	 * functioning returning a KBINFO structure which tells us. */
	if (osVersion.dwPlatformId == VER_PLATFORM_WIN32_NT && p_ToUnicodeEx) {
	    r = p_ToUnicodeEx(wParam, scan, keystate, keys_unicode,
                              lenof(keys_unicode), 0, kbd_layout);
	} else {
	    /* XXX 'keys' parameter is declared in MSDN documentation as
	     * 'LPWORD lpChar'.
	     * The experience of a French user indicates that on
	     * Win98, WORD[] should be passed in, but on Win2K, it should
	     * be BYTE[]. German WinXP and my Win2K with "US International"
	     * driver corroborate this.
	     * Experimentally I've conditionalised the behaviour on the
	     * Win9x/NT split, but I suspect it's worse than that.
	     * See wishlist item `win-dead-keys' for more horrible detail
	     * and speculations. */
	    int i;
	    static WORD keys[3];
	    static BYTE keysb[3];
	    r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
	    if (r > 0) {
	        for (i = 0; i < r; i++) {
	            keysb[i] = (BYTE)keys[i];
	        }
	        MultiByteToWideChar(CP_ACP, 0, (LPCSTR)keysb, r,
                                    keys_unicode, lenof(keys_unicode));
	    }
	}
#ifdef SHOW_TOASCII_RESULT
	if (r == 1 && !key_down) {
	    if (alt_sum) {
		if (in_utf(term) || ucsdata.dbcs_screenfont)
		    debug((", (U+%04x)", alt_sum));
		else
		    debug((", LCH(%d)", alt_sum));
	    } else {
		debug((", ACH(%d)", keys_unicode[0]));
	    }
	} else if (r > 0) {
	    int r1;
	    debug((", ASC("));
	    for (r1 = 0; r1 < r; r1++) {
		debug(("%s%d", r1 ? "," : "", keys_unicode[r1]));
	    }
	    debug((")"));
	}
#endif
	if (r > 0) {
	    WCHAR keybuf;

	    p = output;
	    for (i = 0; i < r; i++) {
		wchar_t wch = keys_unicode[i];

		if (compose_state == 2 && wch >= ' ' && wch < 0x80) {
		    compose_char = wch;
		    compose_state++;
		    continue;
		}
		if (compose_state == 3 && wch >= ' ' && wch < 0x80) {
		    int nc;
		    compose_state = 0;

		    if ((nc = check_compose(compose_char, wch)) == -1) {
			MessageBeep(MB_ICONHAND);
			return twk_SKIP;
		    }
		    keybuf = nc;
		    term_seen_key_event(term);
		    if (ldisc)
			luni_send(ldisc, &keybuf, 1, 1);
		    continue;
		}

		compose_state = 0;

		if (!key_down) {
		    if (alt_sum) {
			if (in_utf(term) || ucsdata.dbcs_screenfont) {
			    keybuf = alt_sum;
			    term_seen_key_event(term);
			    if (ldisc)
				luni_send(ldisc, &keybuf, 1, 1);
			} else {
			    char ch = (char) alt_sum;
			    /*
			     * We need not bother about stdin
			     * backlogs here, because in GUI PuTTY
			     * we can't do anything about it
			     * anyway; there's no means of asking
			     * Windows to hold off on KEYDOWN
			     * messages. We _have_ to buffer
			     * everything we're sent.
			     */
			    term_seen_key_event(term);
			    if (ldisc)
				ldisc_send(ldisc, &ch, 1, 1);
			}
			alt_sum = 0;
		    } else {
			term_seen_key_event(term);
			if (ldisc)
			    luni_send(ldisc, &wch, 1, 1);
		    }
		} else {
		    if(capsOn && wch < 0x80) {
			WCHAR cbuf[2];
			cbuf[0] = 27;
			cbuf[1] = xlat_uskbd2cyrllic(wch);
			term_seen_key_event(term);
			if (ldisc)
			    luni_send(ldisc, cbuf+!left_alt, 1+!!left_alt, 1);
		    } else {
			WCHAR cbuf[2];
			cbuf[0] = '\033';
			cbuf[1] = wch;
			term_seen_key_event(term);
			if (ldisc)
			    luni_send(ldisc, cbuf +!left_alt, 1+!!left_alt, 1);
		    }
		}
		//show_mouseptr(0);
	    }

	    /* This is so the ALT-Numpad and dead keys work correctly. */
	    keys_unicode[0] = 0;

	    return p - output;
	}
	/* If we're definitly not building up an ALT-54321 then clear it */
	if (!left_alt)
	    keys_unicode[0] = 0;
	/* If we will be using alt_sum fix the 256s */
	else if (keys_unicode[0] && (in_utf(term) || ucsdata.dbcs_screenfont))
	    keys_unicode[0] = 10;
    }

    /*
     * ALT alone may or may not want to bring up the System menu.
     * If it's not meant to, we return 0 on presses or releases of
     * ALT, to show that we've swallowed the keystroke. Otherwise
     * we return -1, which means Windows will give the keystroke
     * its default handling (i.e. bring up the System menu).
     */
    if (wParam == VK_MENU && !conf_get_int(conf, CONF_alt_only))
	return twk_SKIP;
#endif

    return twk_FORWARD;
}
