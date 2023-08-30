#include <stdio.h>
#include <string.h>
#include "ppmck_driver.h"
#include "apu.h"
#include "bus.h"

// ROM addresses of various tables
#define DUTYENVE_TABLE			0x8000
#define DUTYENVE_LP_TABLE		0x8010
#define SOFTENVE_TABLE			0x8058
#define SOFTENVE_LP_TABLE		0x806a
#define PITCHENVE_TABLE			0x816f
#define PITCHENVE_LP_TABLE		0x8179
#define ARPEGGIO_TABLE			0x81b2
#define ARPEGGIO_LP_TABLE		0x81c8
#define LFO_DATA				0x8218
#define DPCM_DATA				0x822d
#define SONG_000_TRACK_TABLE	0x8245

// PPMCK driver defines
#define PTR_TRACK_END			5
#define PITCH_CORRECTION		0
#define DPCM_RESTSTOP			0
#define DPCM_BANKSWITCH			0
#define DPCM_EXTRA_BANK_START	0
#define BANK_MAX_IN_4KB			((3 + 0)*2+1)
#define ALLOW_BANKSWITCH		0
#define OVERLOAD_DETECT			0

typedef struct {
	uint16_t		sound_add;
	uint16_t		soft_add;
	uint16_t		pitch_add;
	uint16_t		duty_add;
	uint16_t		arpe_add;

	uint8_t			lfo_reverse_counter;
	uint8_t			lfo_adc_sbc_counter;
	uint8_t			lfo_start_counter;
	uint8_t			lfo_start_time;
	uint8_t			lfo_adc_sbc_time;
	uint8_t			lfo_depth;
	uint8_t			lfo_reverse_time;
	uint8_t			lfo_sel;
	uint8_t			detune_dat;
	uint8_t			register_high;
	uint8_t			register_low;
	uint8_t			duty_sel;
	uint8_t			channel_loop;
	uint8_t			rest_flag;
	uint8_t			softenve_sel;
	uint8_t			pitch_sel;
	uint8_t			arpeggio_sel;
	uint8_t			effect_flag;
	uint8_t			sound_sel;
	uint8_t			sound_counter;
	uint16_t		sound_freq;
	uint8_t			sound_bank;
	uint8_t			pitch_shift_amount;
	uint8_t			n106_volume;
	uint8_t			n106_7c;
	uint8_t			extra_mem2;
} Channel;

static struct {
	Channel			channels[5];
} ppmck;

static uint16_t psg_frequency_table[16] = {
	0x06ae, 0x064e, 0x05f4, 0x059e,
	0x054e, 0x0501, 0x04b9, 0x0476,
	0x0436, 0x03f9, 0x03c0, 0x038a,
	0x0000, 0x07f2, 0x0780, 0x0714
};

static uint16_t
read_word( uint16_t at )
{
	uint8_t lsb = cpu_bus[at];
	uint8_t msb = cpu_bus[at + 1];
	return ( msb << 8 ) | lsb;
}

static void
detune_plus( Channel *c, uint16_t val )
{
	if ( ( val ^ 0 ) == 0 )
		return;

	if ( c->pitch_shift_amount != 0 )
		val <<= 1;

	c->sound_freq += val;
}

static void
detune_minus( Channel *c, uint16_t val )
{
	if ( ( val ^ 0 ) == 0 )
		return;

	if ( c->pitch_shift_amount != 0 )
		val <<= 1;

	c->sound_freq -= val;
}

static void
sound_software_enverope( Channel *c, int i )
{
	uint8_t data;

	for ( ; ; )
	{
		data = cpu_bus[c->soft_add];
		if ( data != 0xff ) break;
		c->soft_add = read_word( SOFTENVE_LP_TABLE + ( c->softenve_sel << 1 ) );
	}

	c->register_low = data;
	apu_write( i << 2, c->register_high | c->register_low );
	c->soft_add++;
}

static void
sound_duty_enverope( Channel *c, int i )
{
	uint8_t data;

	for ( ; ; )
	{
		// if triangle channel
		if ( i == 2 ) return;
		
		data = cpu_bus[c->duty_add];
		if ( data != 0xff ) break;
		c->duty_add = read_word( DUTYENVE_LP_TABLE + ( c->duty_sel << 1 ) );
	}

	c->register_high = ( data << 6 ) | 0x30;
	apu_write( i << 2, c->register_high | c->register_low );
	c->duty_add++;
}

static void
sound_pitch_enverope( Channel *c, int i )
{
	uint8_t temp = c->sound_freq >> 8;
	uint8_t data;

	for ( ; ; )
	{
		data = cpu_bus[c->pitch_add];

		if ( data != 0xff )
		{
			if ( data & 0x80 )
				detune_minus( c, data & 0x7f );
			else
				detune_plus( c, data );

			break;
		}

		c->pitch_add = read_word( PITCHENVE_LP_TABLE + ( c->pitch_sel << 1 ) );
	}

	apu_write( ( i << 2 ) + 2, c->sound_freq & 0xff );

	if ( c->sound_freq >> 8 != temp )
		apu_write( ( i << 2 ) + 3, c->sound_freq >> 8 );

	c->pitch_add++;
}

static int
note_enve_sub( Channel *c )
{
	uint8_t data;

	for ( ; ; )
	{
		data = cpu_bus[c->arpe_add];
		if ( data != 0xff ) break;
		c->arpe_add = read_word( ARPEGGIO_LP_TABLE + ( c->arpeggio_sel << 1 ) );
	}

	if ( data == 0 || data == 0x80 )
		return 1;

	if ( data & 0x80 )
	{
		data &= 0x7f;

		do
		{
			if ( ( c->sound_sel & 0x0f ) != 0 )
				c->sound_sel--;
			else
				c->sound_sel = ( c->sound_sel + 0x0b ) - 0x10;
		} while ( --data > 0 );
	}
	else
	{
		do
		{
			if ( ( c->sound_sel & 0x0f ) != 0x0b )
				c->sound_sel++;
			else
				c->sound_sel = ( c->sound_sel & 0xf0 ) + 0x10;
		} while ( --data > 0 );
	}

	return 0;
}

static void
sound_lfo( Channel *c, int i )
{
	uint8_t temp = c->sound_freq >> 8;

	if ( c->lfo_start_counter > 0 )
		c->lfo_start_counter--;
	else
	{
		c->lfo_reverse_time <<= 1;

		if ( c->lfo_reverse_counter == c->lfo_reverse_time )
		{
			c->lfo_reverse_counter = 0;
			c->effect_flag ^= 0x20;
		}

		c->lfo_reverse_time >>= 1;

		if ( c->lfo_adc_sbc_counter == c->lfo_adc_sbc_time )
		{
			c->lfo_adc_sbc_counter = 0;
			
			if ( !( c->effect_flag & 0x20 ) )
				detune_minus( c, c->lfo_depth );
			else
				detune_plus( c, c->lfo_depth );
		}

		c->lfo_reverse_counter++;
		c->lfo_adc_sbc_counter++;
	}

	apu_write( ( i << 2 ) + 2, c->sound_freq & 0xff );

	if ( c->sound_freq >> 8 != temp )
		apu_write( ( i << 2 ) + 3, c->sound_freq >> 8 );
}

static void
frequency_set( Channel *c, int i )
{
	if ( i == 3 )
	{
		c->sound_freq = c->sound_sel & 0x0f;

		if ( c->effect_flag & 0x80 )
		{
			if ( c->detune_dat & 0x80 )
				detune_minus( c, c->detune_dat & 0x7f );
			else
				detune_plus( c, c->detune_dat );
		}

		c->sound_freq &= 0x00ff;
		return;
	}

	c->sound_freq = psg_frequency_table[c->sound_sel & 0x0f];

	if ( c->sound_sel >> 4 == 0 )
		return;

	for ( int i = 0; i < c->sound_sel >> 4; i++ )
		c->sound_freq >>= 1;

	if ( c->effect_flag & 0x80 )
	{
		if ( c->detune_dat & 0x80 )
			detune_minus( c, c->detune_dat & 0x7f );
		else
			detune_plus( c, c->detune_dat );
	}
}

static void
sound_high_speed_arpeggio( Channel *c, int i )
{
	uint8_t temp = c->sound_freq >> 8;

	if ( !note_enve_sub( c ) )
	{
		frequency_set( c, i );
		apu_write( ( i << 2 ) + 2, c->sound_freq & 0xff );

		if ( c->sound_freq >> 8 != temp )
			apu_write( ( i << 2 ) + 3, c->sound_freq >> 8 );
	}

	c->arpe_add++;
}

static void
do_effect( Channel *c, int i )
{
	if ( c->rest_flag & 0x01 )
		return;

	if ( c->effect_flag & 0x04 )
		sound_duty_enverope( c, i );

	if ( c->effect_flag & 0x01 )
		sound_software_enverope( c, i );

	if ( c->effect_flag & 0x10 )
		sound_lfo( c, i );

	if ( c->effect_flag & 0x02 )
		sound_pitch_enverope( c, i );

	if ( c->effect_flag & 0x08 )
	{
		if ( !( c->rest_flag & 0x02 ) )
			sound_high_speed_arpeggio( c, i );
		else
		{
			note_enve_sub( c );
			frequency_set( c, i );
			c->arpe_add++;
		}
	}
}

static void
effect_init( Channel *c, int i )
{
	c->soft_add = read_word( SOFTENVE_TABLE + ( c->softenve_sel << 1 ) );
	c->pitch_add = read_word( PITCHENVE_TABLE + ( c->pitch_sel << 1 ) );
	c->duty_add = read_word( DUTYENVE_TABLE + ( c->duty_sel << 1 ) );
	c->arpe_add = read_word( ARPEGGIO_TABLE + ( c->arpeggio_sel << 1 ) );

	c->lfo_start_counter = c->lfo_start_time;
	c->lfo_adc_sbc_counter = c->lfo_adc_sbc_time;
	c->lfo_reverse_counter = c->lfo_reverse_time;

	if ( i - 4 >= 0 )
		c->effect_flag = ( c->effect_flag & ~0x40 ) | 0x20;
	else
		c->effect_flag &= ~0x60;

	c->rest_flag = 0x02;
}

static void
loop_sub( Channel *c )
{
	uint8_t lsb, msb;
		
	if ( ++c->channel_loop == cpu_bus[c->sound_add] )
	{
		c->channel_loop = 0;
		c->sound_add += 4;
	}
	else
	{
		c->sound_add++;
		lsb = cpu_bus[++c->sound_add];
		msb = cpu_bus[++c->sound_add];
		c->sound_add = ( msb << 8 ) | lsb;
	}
}

static void
loop_sub2( Channel *c )
{
	uint8_t lsb, msb;
			
	if ( ++c->channel_loop != cpu_bus[c->sound_add] )
		c->sound_add += 4;
	else
	{
		c->channel_loop = 0;
		c->sound_add++;
		lsb = cpu_bus[++c->sound_add];
		msb = cpu_bus[++c->sound_add];
		c->sound_add = ( msb << 8 ) | lsb;
	}
}

static void
data_bank_addr( Channel *c )
{
	uint8_t lsb, msb;

	lsb = cpu_bus[++c->sound_add];
	msb = cpu_bus[++c->sound_add];
	c->sound_add = ( msb << 8 ) | lsb;
}

static void
sound_data_read( Channel *c, int i )
{
	for ( ; ; )
	{
		uint8_t data = cpu_bus[c->sound_add++];

		switch ( data )
		{
		case 0xa0:
			loop_sub( c );
			break;
		case 0xa1:
			loop_sub2( c );
			break;
		case 0xee:
			data_bank_addr( c );
			break;
		case 0xfe:
			data = cpu_bus[c->sound_add++];

			if ( data & 0x80 )
			{
				c->effect_flag &= ~0x04;
				c->register_high = ( data << 6 ) | 0x30;
				apu_write( i << 2, c->register_high | c->register_low );
			}
			else
			{
				c->effect_flag |= 0x04;
				c->duty_sel = data;
				c->duty_add = read_word( DUTYENVE_TABLE + ( data << 1 ) );
			}

			break;
		case 0xfd:
			data = cpu_bus[c->sound_add++];

			if ( data & 0x80 )
			{
				c->effect_flag &= ~0x01;
				c->register_low = data & 0x0f;
				apu_write( i << 2, c->register_high | c->register_low );
			}
			else
			{
				c->effect_flag |= 0x01;
				c->softenve_sel = data;
				c->soft_add = read_word( SOFTENVE_TABLE + ( data << 1 ) );
			}

			break;
		case 0xfc:
			c->rest_flag |= 1;
			c->sound_counter = cpu_bus[c->sound_add++];

			if ( i == 2 )
				apu_write( i << 2, 0 );
			else
				apu_write( i << 2, c->register_high );

			return;
		case 0xfb:
			data = cpu_bus[c->sound_add++];
			
			if ( data == 0xff )
				c->effect_flag &= ~0x8f;
			else
			{
				c->lfo_sel = data << 2;
				c->lfo_start_time = cpu_bus[LFO_DATA + ( data << 2 )];
				c->lfo_start_counter = c->lfo_start_time;
				c->lfo_reverse_time = cpu_bus[LFO_DATA + ( data << 2 ) + 1];
				c->lfo_reverse_counter = c->lfo_reverse_time;
				c->lfo_depth = cpu_bus[LFO_DATA + ( data << 2 ) + 2];

				if ( c->lfo_reverse_time == c->lfo_depth )
				{
					c->lfo_depth = 1;
					c->lfo_adc_sbc_time = 1;
					c->lfo_adc_sbc_counter = 1;
				}
				else if ( c->lfo_reverse_time < c->lfo_depth )
				{
					int add_to = ( c->lfo_depth % c->lfo_reverse_time ) != 0;
					c->lfo_depth = c->lfo_depth / c->lfo_reverse_time + add_to;
					c->lfo_adc_sbc_counter = c->lfo_adc_sbc_time = 1;
				}
				else
				{
					c->lfo_adc_sbc_time = c->lfo_reverse_time / c->lfo_depth;
					c->lfo_adc_sbc_counter = c->lfo_adc_sbc_time;
					c->lfo_depth = 1;
				}

				c->effect_flag |= 0x10;
			}

			break;
		case 0xfa:
			data = cpu_bus[c->sound_add++];

			if ( data == 0xff )
				c->effect_flag &= ~0x80;
			else
			{
				c->detune_dat = data;
				c->effect_flag |= 0x80;
			}

			break;
		case 0xf9:
			apu_write( ( i << 2 ) + 1, cpu_bus[c->sound_add++] );
			break;
		// pitch envelope
		case 0xf8:
			data = cpu_bus[c->sound_add++];

			if ( data == 0xff )
				c->effect_flag &= ~0x02;
			else
			{
				c->pitch_sel = data;
				c->pitch_add = read_word( PITCHENVE_TABLE + ( data << 1 ) );
				c->effect_flag |= 0x02;
			}

			break;
		// arpeggio
		case 0xf7:
			data = cpu_bus[c->sound_add++];

			if ( data == 0xff )
				c->effect_flag &= ~0x08;
			else
			{
				c->arpeggio_sel = data;
				c->arpe_add = read_word( ARPEGGIO_TABLE + ( data << 1 ) );
				c->effect_flag |= 0x08;
			}

			break;
		// direct frequency
		case 0xf6:
			c->sound_freq = read_word( c->sound_add );
			c->sound_add += 2;
			effect_init( c, i );
			break;
		// unused by this program
		case 0xf5:
			break;
		// wait
		case 0xf4:
			c->sound_counter = cpu_bus[c->sound_add++];
			break;
		// note
		default:
			c->sound_sel = data;
			c->sound_counter = cpu_bus[c->sound_add++];
			frequency_set( c, i );
			effect_init( c, i );
			return;
		}
	}
}

static void
sound_internal( int i )
{
	Channel *c = &ppmck.channels[i];

	if ( --c->sound_counter > 0 )
	{
		do_effect( c, i );
		return;
	}

	sound_data_read( c, i );
	do_effect( c, i );

	if ( c->rest_flag & 0x02 )
	{
		apu_write( ( i << 2 ) + 0, c->register_low | c->register_high );
		apu_write( ( i << 2 ) + 2, c->sound_freq & 0xff );
		apu_write( ( i << 2 ) + 3, c->sound_freq >> 8 );
		c->rest_flag &= ~0x02;
	}
}

static void
sound_dpcm_play( Channel *c )
{
	uint8_t data, data2;

	for ( ; ; )
	{
		data = cpu_bus[c->sound_add++];

		switch ( data )
		{
		case 0xa0:
			loop_sub( c );
			break;
		case 0xa1:
			loop_sub2( c );
			break;
		case 0xee:
			data_bank_addr( c );
			break;
		case 0xfc:
			c->sound_counter = cpu_bus[c->sound_add++];
			return;
		case 0xf5:
			break;
		case 0xf4:
			c->sound_counter = cpu_bus[c->sound_add++];
			return;
		default:
			apu_write( APU_SNDCHN,  0x0f ); // stop DPCM
			apu_write( APU_DMCFREQ, cpu_bus[DPCM_DATA + ( data << 2 ) + 0] );

			data2 = cpu_bus[DPCM_DATA + ( data << 2 ) + 1];
			
			if ( data2 != 0xff )
				apu_write( APU_DMCRAW, data2 );

			apu_write( APU_DMCADDR, cpu_bus[DPCM_DATA + ( data << 2 ) + 2] );
			apu_write( APU_DMCLEN,  cpu_bus[DPCM_DATA + ( data << 2 ) + 3] );
			apu_write( APU_SNDCHN,  0x1f );

			c->sound_counter = cpu_bus[c->sound_add++];
			return;
		}
	}
}

static void
sound_dpcm()
{
	Channel *c = &ppmck.channels[4];

	if ( --c->sound_counter > 0 )
		return;

	sound_dpcm_play( c );
}

void
sound_init()
{
	memset( &ppmck, 0, sizeof(ppmck) );

	apu_write( APU_SNDCHN,   0x0f );
	apu_write( APU_SQ1SWEEP, 0x08 );
	apu_write( APU_SQ2SWEEP, 0x08 );

	for ( int i = 0; i < PTR_TRACK_END; i++ )
	{
		Channel *c = &ppmck.channels[i];

		c->sound_add     = read_word( SONG_000_TRACK_TABLE + ( i << 1 ) );
		c->effect_flag   = 0;
		c->sound_counter = 1;
	}
}

void
sound_driver_start()
{
	for ( int i = 0; i < 4; i++ )
		sound_internal( i );

	sound_dpcm();
}
