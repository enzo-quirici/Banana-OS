#ifndef GUI_H
#define GUI_H

#include "types.h"

void gui_init(void);
void gui_poll(void);

void gui_set_enabled(int enabled);
int  gui_is_enabled(void);

/* returns 1 if key was consumed by GUI */
int gui_handle_key(char c);

/* pass 'A' (up) or 'B' (down) for ESC [ sequences */
int gui_handle_arrow(char esc_code);

#endif

