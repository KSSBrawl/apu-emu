#ifndef APU_H
#define APU_H

#include <stdint.h>

#define APU_SQ1VOL		0x00
#define APU_SQ1SWEEP	0x01
#define APU_SQ1LO		0x02
#define APU_SQ1HI		0x03

#define APU_SQ2VOL		0x04
#define APU_SQ2SWEEP	0x05
#define APU_SQ2LO		0x06
#define APU_SQ2HI		0x07

#define APU_TRILINEAR	0x08
#define APU_TRILO		0x0a
#define APU_TRIHI		0x0b

#define APU_NOIVOL		0x0c
#define APU_NOIFREQ		0x0e
#define APU_NOILEN		0x0f

#define APU_DMCFREQ		0x10
#define APU_DMCRAW		0x11
#define APU_DMCADDR		0x12
#define APU_DMCLEN		0x13

#define APU_SNDCHN		0x15
#define APU_APUFRAME	0x17

void 		apu_init();
void		apu_write( uint_fast16_t reg, uint8_t val );
int			apu_clock( float *sample_out, unsigned int *irq_out );
uint8_t		apu_read( uint_fast16_t reg );
uint8_t		apu_read_internal( uint_fast16_t reg );

#endif // APU_H
