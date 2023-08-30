#include <math.h>

#include "display.h"
#include "apu.h"
#include "SDL2/SDL_image.h"
#include "audio.h"

static const Uint32 rmask = 0x000000ff;
static const Uint32 gmask = 0x0000ff00;
static const Uint32 bmask = 0x00ff0000;
static const Uint32 amask = 0xff000000;

SDL_Window		*m_window;
SDL_Renderer	*m_renderer;
SDL_Surface		*m_surface;
SDL_Texture		*m_texture;

static SDL_Rect m_srcrect = { 0, 0, SCREEN_W, SCREEN_H };
static SDL_Rect m_dstrect = { 0, 0, SCREEN_W * 2, SCREEN_H * 2 };
static SDL_Surface *font_tex;

static char *infotext1 	= "     2A03 APU Emulator Demo     ";
static char *infotext2 	= "Now playing: \"Artificial Intelligence Bomb\" by naruto2143     ";
static char *regview	= "         REGISTER VIEW          ";
static char regs_str[32];

static float text_osc_base		= 0.0f;
static float now_playing_x		= 0.0f;
static float now_playing_phase	= 0.0f;
static int now_playing_delay	= 120;

void
display_init()
{
	font_tex = IMG_Load( "font.png" );

	m_window = SDL_CreateWindow( "NES APU Demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			SCREEN_W * 2, SCREEN_H * 2, 0 );
	m_renderer = SDL_CreateRenderer( m_window, -1, SDL_RENDERER_ACCELERATED );
	SDL_RenderClear( m_renderer );

	m_surface = SDL_CreateRGBSurface( 0, SCREEN_W, SCREEN_H, 32, rmask, gmask, bmask, amask );
	SDL_SetClipRect( m_surface, &m_srcrect );
	m_texture = SDL_CreateTextureFromSurface( m_renderer, m_surface );
}

static void
draw_text( char *text, int x, int y )
{
	for ( size_t i = 0; i < strlen( text ); i++ )
	{
		int c = text[i];

		SDL_Rect srcrect = { ( c % 16 ) * 8, ( c / 16 ) * 8, 8, 8 };
		SDL_Rect dstrect = { x, y, 8, 8 };

		SDL_BlitSurface( font_tex, &srcrect, m_surface, &dstrect );
		x += 8;
	}
}

static void
draw_wavy_text( char *text, int x, int y, float phase, float inc )
{
	float osc = phase;

	for ( size_t i = 0; i < strlen( text ); i++ )
	{
		int c = text[i];

		SDL_Rect srcrect = { ( c % 16 ) * 8, ( c / 16 ) * 8, 8, 8 };
		SDL_Rect dstrect = { x, y + 2 * sin( osc ), 8, 8 };

		SDL_BlitSurface( font_tex, &srcrect, m_surface, &dstrect );
		osc += inc;
		x += 8;
	}
}

static void
draw_info_text()
{
	int now_playing_len = 8 * strlen( infotext2 );
	draw_wavy_text( infotext1, 0, 32, text_osc_base, 0.6 );
	draw_wavy_text( infotext2,
			now_playing_x, 48,
			text_osc_base + now_playing_phase + 2.0, 0.6 );
	draw_wavy_text( infotext2,
			now_playing_x + now_playing_len, 48,
			text_osc_base + ( 0.6 * now_playing_len ) + now_playing_phase + 2.0, 0.6 );

	text_osc_base += ( M_PI * 2 ) / 60;

	if ( text_osc_base > M_PI )
		text_osc_base -= M_PI * 2;

	if ( now_playing_delay > 0 )
	{
		if ( now_playing_delay-- != 0 )
			return;
	}
	
	now_playing_x -= 0.4;

	if ( now_playing_x <= -now_playing_len )
	{
		now_playing_x += now_playing_len;
		now_playing_phase += 0.6 * now_playing_len;

		if ( now_playing_phase >= M_PI )
			now_playing_phase -= M_PI * 2;
	}
}

static void
draw_oscilloscope()
{
	SDL_SetRenderDrawColor( m_renderer, 255, 255, 255, 255 );

	for ( int i = 0; i < 512; i++ )
	{
		SDL_RenderDrawPoint( m_renderer, i, 176 + ( 160 * sample_buffer[(int)( 800.0 / 512 ) * i] ) );
	}

	SDL_SetRenderDrawColor( m_renderer, 0, 0, 0, 255 );
}

void
display_update()
{
	SDL_DestroyTexture( m_texture );
	SDL_RenderClear( m_renderer );
	SDL_FillRect( m_surface, &m_srcrect, 0 );

	draw_info_text();
	draw_text( regview, 0, 128 );

	for ( int i = 0; i < 4; i++ )
	{
		sprintf(
			regs_str, "$%02x $%02x $%02x $%02x $%02x",
			apu_read_internal( i ),
			apu_read_internal( i +  4 ),
			apu_read_internal( i +  8 ),
			apu_read_internal( i + 12 ),
			apu_read_internal( i + 16 )
		);
		draw_text( regs_str, 48, 144 + ( 16 * i ) );
	}

	draw_oscilloscope();

	m_texture = SDL_CreateTextureFromSurface( m_renderer, m_surface );
	SDL_RenderCopy( m_renderer, m_texture, &m_srcrect, &m_dstrect );
	SDL_RenderPresent( m_renderer );
}
