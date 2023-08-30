#ifndef DISPLAY_H
#define DISPLAY_H

#include "SDL2/SDL_video.h"
#include "SDL2/SDL_render.h"

#define SCREEN_W	256
#define SCREEN_H	240

extern SDL_Window	*m_window;
extern SDL_Renderer	*m_renderer;
extern SDL_Surface	*m_surface;
extern SDL_Texture	*m_texture;

extern void display_init();
extern void display_update();

#endif // DISPLAY_H
