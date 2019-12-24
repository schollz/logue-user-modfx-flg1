/*
Copyright 2019 Tomoaki Itoh
This software is released under the MIT License, see LICENSE.txt.
//*/

#include "usermodfx.h"
#include "buffer_ops.h"
#include "LCWDelay.h"
#include "LCWFixedMath.h"

static __sdram int32_t s_delay_ram_input[LCW_DELAY_INPUT_SIZE];
static __sdram int32_t s_delay_ram_sampling[LCW_DELAY_SAMPLING_SIZE];

static float s_inputGain;
static float s_time;
static float s_depth;

#define LCW_LFO_TIMER_BITS (30)
#define LCW_LFO_TIMER_MAX (1 << LCW_LFO_TIMER_BITS)
#define LCW_LFO_TIMER_MASK (LCW_LFO_TIMER_MAX - 1)

static struct {
  uint32_t dt;
  uint32_t t;
  uint32_t dir; // 0: +, 1: -
  int32_t th;   // s15.16
  int32_t out;  // s15.16
  int32_t out2; // s15.16
  int32_t center; // s15.16
} s_lfo;

#define LCW_POW2_INV_TABLE_BITS (8)
#define LCW_POW2_INV_TABLE_SIZE (1 << LCW_POW2_INV_TABLE_BITS)
#define LCW_POW2_INV_TABLE_MASK (LCW_POW2_INV_TABLE_MASK - 1)
static const uint16_t pow2InvTable[] = {
    0x0000, 0xFF4E, 0xFE9E, 0xFDED, 0xFD3E, 0xFC8E, 0xFBDF, 0xFB31,
    0xFA83, 0xF9D6, 0xF929, 0xF87C, 0xF7D0, 0xF725, 0xF67A, 0xF5CF,
    0xF525, 0xF47B, 0xF3D2, 0xF329, 0xF281, 0xF1D9, 0xF132, 0xF08B,
    0xEFE4, 0xEF3E, 0xEE99, 0xEDF3, 0xED4F, 0xECAA, 0xEC07, 0xEB63,
    0xEAC0, 0xEA1E, 0xE97C, 0xE8DA, 0xE839, 0xE798, 0xE6F8, 0xE658,
    0xE5B9, 0xE51A, 0xE47B, 0xE3DD, 0xE33F, 0xE2A2, 0xE205, 0xE168,
    0xE0CC, 0xE031, 0xDF96, 0xDEFB, 0xDE60, 0xDDC7, 0xDD2D, 0xDC94,
    0xDBFB, 0xDB63, 0xDACB, 0xDA34, 0xD99D, 0xD906, 0xD870, 0xD7DA,
    0xD744, 0xD6AF, 0xD61B, 0xD587, 0xD4F3, 0xD45F, 0xD3CC, 0xD33A,
    0xD2A8, 0xD216, 0xD184, 0xD0F3, 0xD063, 0xCFD2, 0xCF43, 0xCEB3,
    0xCE24, 0xCD95, 0xCD07, 0xCC79, 0xCBEC, 0xCB5E, 0xCAD2, 0xCA45,
    0xC9B9, 0xC92E, 0xC8A2, 0xC817, 0xC78D, 0xC703, 0xC679, 0xC5F0,
    0xC567, 0xC4DE, 0xC456, 0xC3CE, 0xC346, 0xC2BF, 0xC238, 0xC1B2,
    0xC12C, 0xC0A6, 0xC021, 0xBF9C, 0xBF17, 0xBE93, 0xBE0F, 0xBD8B,
    0xBD08, 0xBC85, 0xBC03, 0xBB81, 0xBAFF, 0xBA7D, 0xB9FC, 0xB97C,
    0xB8FB, 0xB87B, 0xB7FB, 0xB77C, 0xB6FD, 0xB67E, 0xB600, 0xB582,
    0xB504, 0xB487, 0xB40A, 0xB38E, 0xB311, 0xB295, 0xB21A, 0xB19E,
    0xB123, 0xB0A9, 0xB02F, 0xAFB5, 0xAF3B, 0xAEC2, 0xAE49, 0xADD0,
    0xAD58, 0xACE0, 0xAC68, 0xABF1, 0xAB7A, 0xAB03, 0xAA8D, 0xAA17,
    0xA9A1, 0xA92B, 0xA8B6, 0xA842, 0xA7CD, 0xA759, 0xA6E5, 0xA672,
    0xA5FE, 0xA58B, 0xA519, 0xA4A7, 0xA435, 0xA3C3, 0xA352, 0xA2E1,
    0xA270, 0xA1FF, 0xA18F, 0xA11F, 0xA0B0, 0xA041, 0x9FD2, 0x9F63,
    0x9EF5, 0x9E87, 0x9E19, 0x9DAB, 0x9D3E, 0x9CD2, 0x9C65, 0x9BF9,
    0x9B8D, 0x9B21, 0x9AB6, 0x9A4B, 0x99E0, 0x9975, 0x990B, 0x98A1,
    0x9837, 0x97CE, 0x9765, 0x96FC, 0x9694, 0x962B, 0x95C3, 0x955C,
    0x94F4, 0x948D, 0x9426, 0x93C0, 0x935A, 0x92F4, 0x928E, 0x9228,
    0x91C3, 0x915E, 0x90FA, 0x9095, 0x9031, 0x8FCE, 0x8F6A, 0x8F07,
    0x8EA4, 0x8E41, 0x8DDF, 0x8D7C, 0x8D1A, 0x8CB9, 0x8C57, 0x8BF6,
    0x8B95, 0x8B35, 0x8AD4, 0x8A74, 0x8A14, 0x89B5, 0x8955, 0x88F6,
    0x8898, 0x8839, 0x87DB, 0x877D, 0x871F, 0x86C1, 0x8664, 0x8607,
    0x85AA, 0x854E, 0x84F1, 0x8495, 0x843A, 0x83DE, 0x8383, 0x8328,
    0x82CD, 0x8272, 0x8218, 0x81BE, 0x8164, 0x810B, 0x80B1, 0x8058
};

#define LCW_FEEDBACK_GAIN (0.890230f)

static void incLfo(void)
{
  s_lfo.t += s_lfo.dt;
  uint32_t i = s_lfo.t >> (LCW_LFO_TIMER_BITS - LCW_POW2_INV_TABLE_BITS);
  if ( 0 < i ) {
    uint64_t tmp = (uint64_t)(0x10000 - s_lfo.out) * pow2InvTable[i];
    s_lfo.out = 0x10000 - (int32_t)(tmp >> 16);
    if ( s_lfo.th <= s_lfo.out ) {
      s_lfo.dir = ( s_lfo.dir ) ? 0 : 1;
      s_lfo.out = s_lfo.th - s_lfo.out;
    }

    s_lfo.t &= (LCW_LFO_TIMER_MASK >> LCW_POW2_INV_TABLE_BITS);
  }

  s_lfo.out2 = ( s_lfo.dir ) ? (s_lfo.th - s_lfo.out) : s_lfo.out;
}

// limit : equal or more than 1.f
__fast_inline float softlimiter(float c, float x, float limit)
{
  float th = limit - 1.f + c;
  float xf = si_fabsf(x);
  if ( xf < th ) {
    return x;
  }
  else {
    return si_copysignf( th + fx_softclipf(c, xf - th), x );
  }
}

void MODFX_INIT(uint32_t platform, uint32_t api)
{
  const LCWDelayNeededBuffer buffer = {
    s_delay_ram_input,
    s_delay_ram_sampling
  };
  
  LCWDelayInit( &buffer );
  LCWDelayReset();

  s_time = 0.25f;
  s_depth = 0.5f;
  s_inputGain = 0.f;
  
  s_lfo.dt = 0x10000;
  s_lfo.th = (int32_t)(0.5f * 0x10000);
  s_lfo.out = 0.f;
  s_lfo.out2 = 0.f;
}

void MODFX_PROCESS(const float *main_xn, float *main_yn,
                   const float *sub_xn,  float *sub_yn,
                   uint32_t frames)
{
  const float wet = 0.5f;
  const float dry = 1.f - wet;
  const float fb = LCW_FEEDBACK_GAIN;

  const float * mx = main_xn;
  float * __restrict my = main_yn;
  const float * my_e = my + 2*frames;
  const float * sx = sub_xn;
  float * __restrict sy = sub_yn;

  int32_t time = q16_pow2( LCW_SQ15_16(-4.2f + ((1.f - s_time) * 8.2f)) );
  s_lfo.dt = LCW_LFO_TIMER_MAX / (uint32_t)(((time >> 4) * (48000 >> 6)) >> 6);
  int32_t lfoDepth = q16_pow2( LCW_SQ15_16(-3.f + (s_depth * 5.f)) ) >> 8;

  for (; my != my_e; ) {
    float xL = *mx;
    float wL = LCWDelayOutput() / (float)(1 << 24);

    incLfo();
    // s15.16 -> s7.24
    LCWDelayUpdate( ((s_lfo.th >> 1) - s_lfo.out2) * lfoDepth );

    float fbL = softlimiter(0.1f, wL, 1.2f) * fb;
    LCWDelayInput( (int32_t)(((xL * s_inputGain) + fbL) * (1 << 24)) );

    float yL = (dry * xL) + (wet * softlimiter(0.05f, wL, 1.f));

    mx += 2;
    sx += 2;
    *(my++) = yL;
    *(my++) = yL;
    *(sy++) = yL;
    *(sy++) = yL;

    if ( s_inputGain < 0.99998f ) {
      s_inputGain += ( (1.f - s_inputGain) * 0.0625f );
    }
    else { s_inputGain = 1.f; }
  }
}

void MODFX_RESUME(void)
{
  buf_clr_u32(
    (uint32_t * __restrict__)s_delay_ram_input,
    LCW_DELAY_INPUT_SIZE );
  buf_clr_u32(
    (uint32_t * __restrict__)s_delay_ram_sampling,
    LCW_DELAY_SAMPLING_SIZE );
  s_inputGain = 0.f;
}

void MODFX_PARAM(uint8_t index, int32_t value)
{
  const float valf = q31_to_f32(value);
  switch (index) {
  case k_user_modfx_param_time:
    s_time = clip01f(valf);
    break;
  case k_user_modfx_param_depth:
    s_depth = clip01f(valf);
    break;
  default:
    break;
  }
}
