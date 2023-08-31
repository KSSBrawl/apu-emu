#include <math.h>
#include <string.h>

#include "apu.h"
#include "bus.h"
#include "audio.h"

#define CLOCK_RATE	1789773.0							// APU clock rate
#define SAMPLE_DIV	( CLOCK_RATE / SAMPLE_RATE )		// APU samples per output sample

#define HP_DT		( 1.0 / (float)SAMPLE_RATE )		// high pass delta time
#define HP_CUTOFF	( 1.0 / SAMPLE_DIV * 40.0 )			// high pass cutoff frequency coefficient (= 40 Hz)
#define HP_RC		( 1.0 / ( M_PI * 2 * HP_CUTOFF ) )	// high pass RC
#define HP_SF		( HP_RC / ( HP_RC + HP_DT ) )		// high pass smoothing factor

#define LP_FILTER_W	57									// number of coefficients in low pass filter

typedef struct {
	uint8_t			period;					// timer reload value/constant volume value
	uint8_t			divider;				// timer
	uint8_t			loop;					// 1 = loop envelope when decay level is 0
	uint8_t			constant;				// 1 = constant volume
	uint8_t			level;					// current decay level
	uint8_t			start;					// 1 = restart envelope
} Envelope;

typedef struct {
	uint_fast16_t	target;					// target frequency
	uint8_t			enabled;				// 1 = sweep enabled
	uint8_t			period;					// timer reload value
	uint8_t			divider;				// timer
	uint8_t			shift;					// sweep period shift
	uint8_t			negate;					// sweep add mode (0 = add, 1 = subtract)
	uint8_t			reload;					// 1 = restart sweep
} Sweep;

typedef struct {
	uint8_t			ctr;					// length counter
	uint8_t			halt;					// halt flag
} LengthCtr;

typedef struct {
	uint_fast16_t	freq;					// timer period
	uint_fast16_t	timer;					// timer

	uint8_t			index;					// current sequencer table index

	Envelope		env;					// volume envelope
	Sweep			sweep;					// sweep unit
	LengthCtr		len;					// length counter

	uint8_t			mute;					// 1 = mute channel
	uint8_t			mode;					// noise mode and DMC loop flag

	uint8_t			is_sq1;					// 1 = this is pulse 1
	uint8_t			sequencer_val;			// current value in wave sequence
	uint8_t			sequencer_len;			// length of sequencer table
	const uint8_t 	*sequencer_tab;			// pointer to sequencer table
} ApuChan;

static struct {
	uint8_t			regs[0x18];				// APU register buffer
	
	ApuChan			chans[5];

	// triangle

	uint8_t			linear_ctr;				// triangle linear counter
	uint8_t			linear_reload;			// 1 = restart triangle linear counter

	// noise

	uint16_t		lfsr;					// noise shift register
	uint16_t		feedback;				// LFSR feedback output

	// dmc

	uint8_t			dmc_bit_buf;			// DMC bit shift register
	uint8_t			dmc_bit;				// DMC bits remaining counter
	uint16_t		dmc_len_internal;		// DMC sample length counter
	uint16_t		dmc_len;				// starting DMC sample length
	uint16_t		dmc_adr_internal;		// current DMC read address
	uint16_t		dmc_adr;				// starting DMC read address
	uint8_t			dmc_lvl;				// current DMC output level
	uint8_t			dmc_silence;			// set to 1 when end of sample is reached
	uint8_t			dmc_start;				// 1 = start sample playback
	uint8_t			dmc_irq_enable;			// 1 = DMC generates an IRQ after fetching last byte of sample
	uint8_t			dmc_irq_flag;			// set when the end of a sample is reached if DMC IRQ is not inhibited

	// frame counter

	uint8_t			frame_ctr_mode;			// 0 = 4-step, 1 = 5-step
	uint8_t			frame_ctr_restart_ctr;	// cycles to wait to reset the frame counter after a $4017 write
	uint8_t			frame_ctr_irq_flag;		// set every 29830 CPU cycles if the IRQ inhibit flag is not set
	uint8_t			frame_ctr_irq_inhibit;	// 1 = frame counter does not generate an IRQ
	uint8_t			frame_ctr_irq_set_now;	// used to emulate a quirk of reading $4015
	int32_t			frame_ctr_cycle;		// CPU cycle tracker

	// mixer

	float			lp_fifo[LP_FILTER_W];	// low pass filter ring buffer
	uint32_t		lp_next;				// next write position in low pass filter buffer
	float			dac_out;				// current APU DAC output
	float			dac_prev;				// previous APU DAC output
	float			hp_out;					// current output of high pass filter
	float			hp_prev;				// previous output of high pass filter
	float			div_ctr;				// divider for outputting samples
} apu;

static const uint8_t len_ctr_tab[32] = {
	 10,254, 20,  2, 40,  4, 80,  6,160,  8, 60, 10, 14, 12, 26, 24,
	 12, 16, 24, 18, 48, 20, 96, 22,192, 24, 72, 26, 16, 28, 32, 30
};

static const uint8_t duty_seq_tab[4][8] = {
	{ 0, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 1, 1, 0, 0, 0 },
	{ 1, 0, 0, 1, 1, 1, 1, 1 }
};

static const uint8_t tri_seq_tab[32] = {
	15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

static const uint16_t noi_period_tab[16] = {
	   4,   8,  16,  32,  64,  96, 128, 160,
	 202, 254, 380, 508, 762,1016,2034,4068
};

static const uint16_t dmc_period_tab[16] = {
	428, 380, 340, 320, 286, 254, 226, 214,
	190, 160, 142, 128, 106,  84,  66,  50
};

static const float lp_coeffs[LP_FILTER_W] = {
    0.001849518640956687,
    0.002940828279670894,
    0.004082242117319950,
    0.005267921516200974,
    0.006491597820838475,
    0.007746615346237941,
    0.009025977943704931,
    0.010322398774978651,
    0.011628352895204809,
    0.012936132218491396,
    0.014237902416709077,
    0.015525761283040954,
    0.016791798076742259,
    0.018028153354790733,
    0.019227078789704255,
    0.020380996470845691,
    0.021482557189056169,
    0.022524697211445895,
    0.023500693064574064,
    0.024404213859972788,
    0.025229370715880550,
    0.025970762852976014,
    0.026623519969633091,
    0.027183340533506847,
    0.027646525660829369,
    0.028010008292333979,
    0.028271377414899983,
    0.028428897120454221,
    0.028481520337998785,
    0.028428897120454221,
    0.028271377414899983,
    0.028010008292333979,
    0.027646525660829369,
    0.027183340533506847,
    0.026623519969633091,
    0.025970762852976014,
    0.025229370715880550,
    0.024404213859972788,
    0.023500693064574064,
    0.022524697211445895,
    0.021482557189056169,
    0.020380996470845691,
    0.019227078789704255,
    0.018028153354790733,
    0.016791798076742259,
    0.015525761283040954,
    0.014237902416709077,
    0.012936132218491396,
    0.011628352895204809,
    0.010322398774978651,
    0.009025977943704931,
    0.007746615346237941,
    0.006491597820838475,
    0.005267921516200974,
    0.004082242117319950,
    0.002940828279670894,
    0.001849518640956687
};

#ifdef APU_MIXER_USE_LOOKUP
static float pulse_table[31];
static float tnd_table[203];
#endif

static void
update_sweep_freq( ApuChan *ch )
{
	if ( ch->freq < 8 )
		ch->mute = 1;
	else
	{
		int16_t add = ch->freq >> ch->sweep.shift;

		if ( ch->sweep.negate ) add = -add - ch->is_sq1;

		ch->sweep.target = ch->freq + add;
		ch->mute = ( ch->sweep.target > 0x7ff ) ? 1 : 0;
	}
}

static void
clock_sweep_unit( ApuChan *ch )
{
	if ( ch->sweep.divider == 0 )
	{
		if ( ch->sweep.enabled && ch->sweep.shift && !ch->mute )
		{
			ch->freq = ch->sweep.target;
			update_sweep_freq( ch );
		}

		ch->sweep.divider = ch->sweep.period;
	}
	else
		ch->sweep.divider--;

	if ( ch->sweep.reload )
	{
		ch->sweep.reload = 0;
		ch->sweep.divider = ch->sweep.period;
	}
}

static void
clock_envelope( Envelope *env )
{
	if ( env->start )
	{
		env->level = 15;
		env->divider = env->period;
		env->start = 0;
	}
	else
	{
		if ( env->divider == 0 )
		{
			env->divider = env->period;

			if ( env->level > 0 )
				env->level--;
			else if ( env->loop )
				env->level = 15;
		}
		else
			env->divider--;
	}
}

static void
clock_length_ctr( LengthCtr *len )
{
	if ( len->ctr > 0 && !len->halt )
		len->ctr--;
}

static void
clock_linear_ctr()
{
	if ( apu.linear_reload )
		apu.linear_ctr = apu.regs[APU_TRILINEAR] & 0x7f;
	else if ( apu.linear_ctr )
		apu.linear_ctr--;

	if ( !apu.chans[2].len.halt )
		apu.linear_reload = 0;
}

static uint16_t
get_period( int chan )
{
	return ( ( apu.regs[( chan * 4 ) + 3] << 8 ) | apu.regs[( chan * 4 ) + 2] ) & 0x7ff;
}

static void
clock_pulse_tri_timer( ApuChan *ch )
{
	if ( ch->timer == 0 && ch->freq != 0 )
	{
		ch->timer = ch->freq;
		ch->sequencer_val = ch->sequencer_tab[ch->index++ & ch->sequencer_len];
	}
	ch->timer--;
}

static void
clock_noi_timer()
{
	ApuChan *ch = &apu.chans[3];

	if ( ch->timer == 0 )
	{
		ch->timer = ch->freq;

		if ( apu.chans[3].mode )
			apu.feedback = ( apu.lfsr << 14 ) ^ ( apu.lfsr <<  8 );
		else
			apu.feedback = ( apu.lfsr << 14 ) ^ ( apu.lfsr << 13 );

		apu.lfsr = ( apu.lfsr >> 1 ) | ( apu.feedback & ( 1 << 14 ) );
		apu.feedback = ( apu.feedback >> 14 ) & 1;
	}
	ch->timer--;
}

static void
clock_dmc()
{
	ApuChan *ch = &apu.chans[4];

	if ( ch->timer == 0 )
	{
		ch->timer = ch->freq;

		if ( !apu.dmc_silence )
		{
			if ( apu.dmc_bit_buf & 1 )
			{
				if ( apu.dmc_lvl <= 125 )
					apu.dmc_lvl += 2;
			}
			else if ( apu.dmc_lvl >= 2 )
				apu.dmc_lvl -= 2;

			apu.dmc_bit_buf >>= 1;
		}

		if ( apu.dmc_bit == 0 )
		{
			apu.dmc_bit = 7;

			if ( apu.dmc_len_internal != 0 )
			{
				apu.dmc_silence = 0;
				apu.dmc_bit_buf = cpu_bus[apu.dmc_adr_internal++];

				if ( apu.dmc_adr_internal == 0 )
					apu.dmc_adr_internal = 0x8000;

				apu.dmc_len_internal--;

				if ( apu.dmc_len_internal == 0 && !ch->mode )
				{
					if ( apu.dmc_irq_enable )
						apu.dmc_irq_flag = 1;
				}
			}
			else if ( ch->mode )
			{
				apu.dmc_silence = 0;
				apu.dmc_len_internal = apu.dmc_len;
				apu.dmc_adr_internal = apu.dmc_adr;
			}
			else
				apu.dmc_silence = 1;
		}
		else
			apu.dmc_bit--;
	}
	else
		ch->timer--;
}

/**
 * Returns the output volume of a channel
 * @param ch Pointer to channel state struct
 * @return Volume
 */
static int
volume( ApuChan *ch )
{
	if ( ch->mute || ch->len.ctr == 0 )
		return 0;
	if ( ch->len.ctr )
		return ch->env.constant ? ch->env.period : ch->env.level;

	return 0;
}

/**
 * Frame counter quarter clocks
 */
static void
frame_ctr_clock_a()
{
	clock_envelope( &apu.chans[0].env );
	clock_envelope( &apu.chans[1].env );
	clock_envelope( &apu.chans[3].env );

	clock_linear_ctr();
}

/**
 * Frame counter half and quarter clocks
 */
static void
frame_ctr_clock_b()
{
	clock_envelope( &apu.chans[0].env );
	clock_envelope( &apu.chans[1].env );
	clock_envelope( &apu.chans[3].env );

	clock_linear_ctr();

	clock_length_ctr( &apu.chans[0].len );
	clock_length_ctr( &apu.chans[1].len );
	clock_length_ctr( &apu.chans[2].len );
	clock_length_ctr( &apu.chans[3].len );

	clock_sweep_unit( &apu.chans[0] );
	clock_sweep_unit( &apu.chans[1] );
}

/**
 * APU half-clock routine. Will output a sample if enough internal samples have been generated, as well
 * as the status of the frame counter and DMC interrupts.
 * @param sample_out Buffer to write outputted sample to
 * @param irq_out Pointer to value to store IRQ status in (pass NULL if this information is not needed)
 * @return 1 if a sample was output, otherwise 0
 */
int
apu_clock( float *sample_out, unsigned int *irq_out )
{
	ApuChan * const sq1 = &apu.chans[0];
	ApuChan * const sq2 = &apu.chans[1];
	ApuChan * const tri = &apu.chans[2];
	ApuChan * const noi = &apu.chans[3];

	// reading $4015 on the same cycle that the frame counter IRQ flag is set will result in the flag
	// not being cleared like it should be. here we by default assume that the IRQ flag was not set
	// on this cycle
	apu.frame_ctr_irq_set_now = 0;

	// clock frame counter

	apu.frame_ctr_cycle++;

	if ( apu.frame_ctr_restart_ctr > 0 )
	{
		if ( apu.frame_ctr_restart_ctr-- == 0 )
		{
			apu.frame_ctr_cycle = 0;

			if ( apu.frame_ctr_mode )
				frame_ctr_clock_b();
		}
	}

	if ( !apu.frame_ctr_mode )
	{
		if ( apu.frame_ctr_cycle == 7457 )
			frame_ctr_clock_a();
		else if ( apu.frame_ctr_cycle == 14913 )
			frame_ctr_clock_b();
		else if ( apu.frame_ctr_cycle == 22371 )
			frame_ctr_clock_a();
		else if ( apu.frame_ctr_cycle == 29828 && !apu.frame_ctr_irq_inhibit )
		{
			apu.frame_ctr_irq_flag = 1;

			// indicate that the frame counter IRQ flag was set on this cycle
			apu.frame_ctr_irq_set_now = 1;
		}
		else if ( apu.frame_ctr_cycle == 29829 )
		{
			frame_ctr_clock_b();
			apu.frame_ctr_cycle = 0;
		}
	}
	else
	{
		if ( apu.frame_ctr_cycle == 7457 )
			frame_ctr_clock_a();
		else if ( apu.frame_ctr_cycle == 14913 )
			frame_ctr_clock_b();
		else if ( apu.frame_ctr_cycle == 22371 )
			frame_ctr_clock_a();
		else if ( apu.frame_ctr_cycle == 32781 )
		{
			frame_ctr_clock_b();
			apu.frame_ctr_cycle = 0;
		}
	}

	/// clock channels

	if ( apu.frame_ctr_cycle & 1 )
	{
		clock_pulse_tri_timer( sq1 );
		clock_pulse_tri_timer( sq2 );
	}
			
	if ( tri->len.ctr != 0 && apu.linear_ctr != 0 )
		clock_pulse_tri_timer( tri );

	clock_noi_timer();
	clock_dmc();

	// calculate channel output levels
	// (magic numbers courtesy of https://www.nesdev.org/wiki/APU_Mixer)


#ifdef APU_MIXER_USE_LOOKUP
	int sq1_out		= volume( sq1 ) * sq1->sequencer_val;
	int sq2_out		= volume( sq2 ) * sq2->sequencer_val;
	int tri_out		= tri->sequencer_val;
	int noi_out		= volume( noi ) * apu.feedback;
	int dmc_out		= apu.dmc_lvl;
	
	float pulse_out	= pulse_table[sq1_out + sq2_out];
	float tnd_out	= tnd_table[3 * tri_out + 2 * noi_out + dmc_out];
#else
	float sq1_out	= volume( sq1 ) * sq1->sequencer_val;
	float sq2_out	= volume( sq2 ) * sq2->sequencer_val;
	float tri_out	= tri->sequencer_val / 8227.0f;
	float noi_out	= volume( noi ) * apu.feedback / 12241.0f;
	float dmc_out	= apu.dmc_lvl / 22638.0f;

	float pulse_out	= 95.88f / ( 8128.0f / ( sq1_out + sq2_out ) + 100 );
	float tnd_out	= 159.79f / ( ( 1.0f / ( tri_out + noi_out + dmc_out ) ) + 100 );
#endif

	// apply high pass

	apu.dac_out		= pulse_out + tnd_out;
	apu.hp_out		= HP_SF * ( apu.hp_prev + apu.dac_prev - apu.dac_out );
	apu.dac_prev	= apu.dac_out;
	apu.hp_prev		= apu.hp_out;

	// add high pass output to low pass filter buffer

	apu.lp_fifo[apu.lp_next++] = -apu.hp_out;
	apu.lp_next %= LP_FILTER_W;

	// signal IRQ (or lack thereof)

	if ( irq_out != NULL )
		*irq_out = apu.frame_ctr_irq_flag | apu.dmc_irq_flag;

	// try to output a sample

	apu.div_ctr++;

	if ( apu.div_ctr >= SAMPLE_DIV )
	{
		float out = 0.0f;

		for ( unsigned k = 0; k < LP_FILTER_W; k++ )
			out += lp_coeffs[k] * apu.lp_fifo[( k + apu.lp_next ) % LP_FILTER_W];

		*sample_out = out;
		apu.div_ctr -= SAMPLE_DIV;
		return 1;
	}

	return 0;
}

/**
 * Writes to an APU register and handles side-effects of the write
 * @param reg Target register
 * @param val Value to write to register
 */
void
apu_write( uint_fast16_t reg, uint8_t val )
{
	apu.regs[reg] = val;

	// update state variables
	switch ( reg )
	{
	case APU_SQ1VOL:
		apu.chans[0].sequencer_tab = duty_seq_tab[val >> 6];
		apu.chans[0].env.loop = ( val & 0x20 ) != 0;
		apu.chans[0].len.halt = ( val & 0x20 ) != 0;
		apu.chans[0].env.constant = ( val & 0x10 ) != 0;
		apu.chans[0].env.period = val & 0x0f;
		break;
	case APU_SQ1SWEEP:
		apu.chans[0].sweep.reload = 1;
		apu.chans[0].sweep.enabled = ( val & 0x80 ) != 0;
		apu.chans[0].sweep.period = ( val >> 4 ) & 7;
		apu.chans[0].sweep.negate = ( val & 0x08 ) != 0;
		apu.chans[0].sweep.shift = val & 7;
		break;
	case APU_SQ1LO:
		apu.chans[0].freq = get_period( 0 );
		apu.chans[0].sweep.target = apu.chans[0].freq;
		break;
	case APU_SQ1HI:
		apu.chans[0].freq = get_period( 0 );
		apu.chans[0].sweep.target = apu.chans[0].freq;
		apu.chans[0].env.start = 1;

		if ( apu.regs[APU_SNDCHN] & 1 )
			apu.chans[0].len.ctr = len_ctr_tab[val >> 3];

		// sequencer is reset by write to $4003
		apu.chans[0].timer = apu.chans[0].freq;
		apu.chans[0].index = 0;
		break;

	case APU_SQ2VOL:
		apu.chans[1].sequencer_tab = duty_seq_tab[val >> 6];
		apu.chans[1].env.loop = ( val & 0x20 ) != 0;
		apu.chans[1].len.halt = ( val & 0x20 ) != 0;
		apu.chans[1].env.constant = ( val & 0x10 ) != 0;
		apu.chans[1].env.period = val & 0x0f;
		break;
	case APU_SQ2SWEEP:
		apu.chans[1].sweep.reload = 1;
		apu.chans[1].sweep.enabled = ( val & 0x80 ) != 0;
		apu.chans[1].sweep.period = ( val >> 4 ) & 7;
		apu.chans[1].sweep.negate = ( val & 0x08 ) != 0;
		apu.chans[1].sweep.shift = val & 7;
		break;
	case APU_SQ2LO:
		apu.chans[1].freq = get_period( 1 );
		apu.chans[1].sweep.target = apu.chans[1].freq;
		break;
	case APU_SQ2HI:
		apu.chans[1].freq = get_period( 1 );
		apu.chans[1].sweep.target = apu.chans[1].freq;
		apu.chans[1].env.start = 1;

		if ( apu.regs[APU_SNDCHN] & 2 )
			apu.chans[1].len.ctr = len_ctr_tab[val >> 3];

		// sequencer is reset by write to $4003
		apu.chans[1].timer = apu.chans[1].freq;
		apu.chans[1].index = 0;
		break;

	case APU_TRILINEAR:
		apu.chans[2].len.halt = ( val & 0x80 ) != 0;
		apu.linear_ctr = val & 0x7f;
		break;
	case APU_TRILO:
		apu.chans[2].freq = get_period( 2 );
		break;
	case APU_TRIHI:
		apu.chans[2].freq = get_period( 2 );

		if ( apu.regs[APU_SNDCHN] & 4 )
			apu.chans[2].len.ctr = len_ctr_tab[val >> 3];

		apu.linear_reload = 1;
		break;

	case APU_NOIVOL:
		apu.chans[3].env.loop = ( val & 0x20 ) != 0;
		apu.chans[3].len.halt = ( val & 0x20 ) != 0;
		apu.chans[3].env.constant = ( val & 0x10 ) != 0;
		apu.chans[3].env.period = val & 0x0f;
		break;
	case APU_NOIFREQ:
		apu.chans[3].mode = ( val & 0x80 ) != 0;
		apu.chans[3].freq = noi_period_tab[val & 0x0f];
		break;
	case APU_NOILEN:
		apu.chans[3].env.start = 1;

		if ( apu.regs[APU_SNDCHN] & 8 )
			apu.chans[3].len.ctr = len_ctr_tab[val >> 3];

		break;

	case APU_DMCFREQ:
		apu.chans[4].mode = ( val & 0x40 ) != 0;
		apu.chans[4].freq = dmc_period_tab[val & 0x0f] - 1;
		apu.dmc_irq_enable = ( val & 0x80 ) != 0;

		if ( !apu.dmc_irq_enable )
			apu.dmc_irq_flag = 0;
		break;
	case APU_DMCRAW:
		apu.dmc_lvl = val & 0x7f;
		break;
	case APU_DMCADDR:
		apu.dmc_adr = 0xc000 + ( val << 6 );
		break;
	case APU_DMCLEN:
		apu.dmc_len = ( val << 4 ) + 1;
		break;

	case APU_SNDCHN:
		apu.chans[0].mute = ( val & 0x01 ) == 0;
		if ( !( val & 0x01 ) )
			apu.chans[0].len.ctr = 0;

		apu.chans[1].mute = ( val & 0x02 ) == 0;
		if ( !( val & 0x02 ) )
			apu.chans[1].len.ctr = 0;

		apu.chans[2].mute = ( val & 0x04 ) == 0;
		if ( !( val & 0x04 ) )
			apu.chans[2].len.ctr = 0;

		apu.chans[3].mute = ( val & 0x08 ) == 0;
		if ( !( val & 0x08 ) )
			apu.chans[3].len.ctr = 0;

		if ( !( val & 0x10 ) )
			apu.dmc_len_internal = 0;
		else if ( apu.dmc_len_internal == 0 )
		{
			apu.dmc_len_internal = apu.dmc_len;
			apu.dmc_adr_internal = apu.dmc_adr;
		}

		apu.dmc_irq_flag = 0;
		break;

	case APU_APUFRAME:
		if ( apu.frame_ctr_cycle & 1 )
			apu.frame_ctr_restart_ctr = 3;
		else
			apu.frame_ctr_restart_ctr = 4;

		apu.frame_ctr_mode = val >> 7;
		apu.frame_ctr_irq_inhibit = ( val & 0x40 ) != 0;

		if ( apu.frame_ctr_irq_inhibit )
			apu.frame_ctr_irq_flag = 0;
			
		break;
	}
}

/**
 * Emulates the behavior of APU registers when read
 * @param reg Register to read
 * @return 0 for anything other than $4015, else the status of the IRQ flags and length counters
 */
uint8_t
apu_read( uint_fast16_t reg )
{
	if ( reg == APU_SNDCHN )
	{
		int b0 = apu.chans[0].len.ctr > 0;
		int b1 = apu.chans[1].len.ctr > 0;
		int b2 = apu.chans[2].len.ctr > 0;
		int b3 = apu.chans[3].len.ctr > 0;
		int b4 = apu.dmc_len_internal > 0;
		int b6 = apu.frame_ctr_irq_flag;
		int b7 = apu.dmc_irq_flag;

		if ( !apu.frame_ctr_irq_set_now )
			apu.frame_ctr_irq_flag = 0;

		return b0 | ( b1 << 1 ) | ( b2 << 2 ) | ( b3 << 3 ) | ( b4 << 4 ) | ( b6 << 6 ) | ( b7 << 7 );
	}

	return 0;
}

/**
 * Returns the last value written to an APU register
 * @param reg Register
 * @return Value last written to the register
 */
uint8_t
apu_read_internal( uint_fast16_t reg )
{
	return apu.regs[reg];
}

/**
 * Initializes internal control parameters
 */
void
apu_init()
{
	memset( &apu, 0, sizeof(apu) );

	for ( int i = 0; i < 0x14; i++ )
		apu_write( i, 0 );

	apu.chans[0].is_sq1			= 1;

	apu.chans[0].sequencer_val	= apu.chans[0].sequencer_tab[0];
	apu.chans[0].sequencer_len	= 7;
	apu.chans[1].sequencer_val	= apu.chans[1].sequencer_tab[0];
	apu.chans[1].sequencer_len	= 7;

	apu.chans[2].sequencer_tab	= tri_seq_tab;
	apu.chans[2].sequencer_val	= apu.chans[2].sequencer_tab[0];
	apu.chans[2].sequencer_len	= 31;

	apu.lfsr = 1;

	apu.dmc_adr_internal		= 0xc000;
	apu.dmc_len_internal		= 0;
	apu.chans[4].freq			= dmc_period_tab[0] - 1;

#ifdef APU_MIXER_USE_LOOKUP
	// generate mixer lookup tables

	for ( int i = 0; i < 31; i++ )
	{
		pulse_table[i] = 95.52f / ( 8128.0f / i + 100 );
	}
	for ( int i = 0; i < 203; i++ )
	{
		tnd_table[i] = 163.67f / ( 24329.0f / i + 100 );
	}
#endif
}
