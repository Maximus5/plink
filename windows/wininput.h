
#ifndef PUTTY_WININPUT_H
#define PUTTY_WININPUT_H

enum TranslateWinKeySpecials
{
	twk_SKIP = 0,
	twk_FORWARD = -1,
	twk_ASCIIZ = -2,
	twk_PAGEUP = -3,
	twk_PAGEDOWN = -4,
	twk_LINEUP = -5,
	twk_LINEDOWN = -6,
	twk_SCROLL2SEL = -7,
	twk_REQPASTE = -8,
	twk_SYSMENU = -9,
	twk_FULLSCREEN = -10,
	twk_CTRLBREAK = -11,
};

extern int TranslateWinKey(UINT message, WPARAM wParam, LPARAM lParam,
			Conf *conf, BYTE* keystate/*[256]*/, int compose_state, void *ldisc,
			unsigned char *output);

#endif
