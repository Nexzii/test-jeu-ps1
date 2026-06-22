#include "audio.h"
#include <stdint.h>
#include <psxspu.h>
#include <hwregs_c.h>

/* ----------------------------------------------------------------------------
 * Tiny chiptune engine.
 *
 * We synthesize a single looping square-wave sample directly in ADPCM (no
 * external assets), upload it to SPU RAM, then play a melody + bass line by
 * re-keying two voices at musical pitches each step.
 *
 * One ADPCM block decodes to 28 samples. We make each block exactly one
 * square-wave cycle (14 high, 14 low), so the wave repeats every 28 output
 * samples. Playing the sample at rate = note_hz * 28 therefore produces a tone
 * at note_hz.
 * ------------------------------------------------------------------------- */

#define ALLOC_ADDR   0x1010      /* first usable SPU RAM address */
#define WAVE_PERIOD  28          /* samples per square cycle (1 ADPCM block)   */
#define V_MELODY     0
#define V_BASS       1
#define V_WIND       2           /* looping wind ambience                      */
#define V_SFX        3           /* one-shot sound effects                     */
#define WIND_RATE    11025
#define STEP_FRAMES  9           /* frames per musical step (~150 ms)          */
#define SONG_LEN     32

/* Wind ambience ADPCM (incbin, see CMakeLists.txt). */
extern const uint8_t wind_adpcm[];
extern const int     wind_adpcm_size;

static int sample_addr;
static int wind_addr;
static int frame_ctr;
static int step;
static int sfx_timer;

/* Settings. */
static const short mus_vol[4] = { 0, 0x0a00, 0x1500, 0x2200 };
static int music_level = 2;
static int sfx_on = 1;
static int wind_on = 1;

/* Arpeggiated C - G - Am - F loop (melody), with a pulsing bass root. */
static const short melody[SONG_LEN] = {
    262, 330, 392, 523,  392, 330, 262,   0,   /* C  */
    392, 494, 587, 784,  587, 494, 392,   0,   /* G  */
    440, 523, 659, 880,  659, 523, 440,   0,   /* Am */
    349, 440, 523, 698,  523, 440, 349,   0,   /* F  */
};
static const short bass[SONG_LEN] = {
    131,   0, 131,   0,  131,   0, 131,   0,    /* C3 */
    196,   0, 196,   0,  196,   0, 196,   0,    /* G3 */
    220,   0, 220,   0,  220,   0, 220,   0,    /* A3 */
    175,   0, 175,   0,  175,   0, 175,   0,    /* F3 */
};

/* Build the looping square-wave sample (2 ADPCM blocks) and upload it. */
static void upload_square(void) {
    static uint8_t smp[32];   /* 2 blocks x 16 bytes */
    int b;

    for (b = 0; b < 2; b++) {
        uint8_t *blk = &smp[b * 16];
        blk[0] = 0x00;                       /* shift 0, filter 0 */
        blk[1] = (b == 0) ? 0x04 : 0x03;     /* loop-start ; loop-end+repeat */
        int i;
        for (i = 0; i < 7; i++)  blk[2 + i] = 0x77;   /* 14 samples at +7 */
        for (i = 0; i < 7; i++)  blk[9 + i] = 0x99;   /* 14 samples at -7 */
    }

    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr(ALLOC_ADDR);
    SpuWrite((const uint32_t *) smp, sizeof(smp));
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    sample_addr = ALLOC_ADDR;
}

/* Upload the wind ambience ADPCM right after the square sample. */
static void upload_wind(void) {
    int size = wind_adpcm_size;
    wind_addr = (ALLOC_ADDR + 64 + 63) & ~63;
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    SpuSetTransferStartAddr(wind_addr);
    SpuWrite((const uint32_t *) wind_adpcm, (size + 63) & ~63);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
}

void audio_init(void) {
    SpuInit();
    upload_square();
    upload_wind();
    SpuSetCommonMasterVolume(0x3000, 0x3000);

    /* Music + SFX voices use the square sample with an instant, sustained
     * envelope (notes are shaped manually by re-keying). */
    for (int ch = 0; ch < 4; ch++) {
        SPU_CH_ADDR(ch)  = getSPUAddr(sample_addr);
        SPU_CH_ADSR1(ch) = 0x00ff;
        SPU_CH_ADSR2(ch) = 0x0000;
    }
    SPU_CH_VOL_L(V_MELODY) = 0x1500; SPU_CH_VOL_R(V_MELODY) = 0x1500;
    SPU_CH_VOL_L(V_BASS)   = 0x1a00; SPU_CH_VOL_R(V_BASS)   = 0x1a00;
    SPU_CH_VOL_L(V_SFX)    = 0x2200; SPU_CH_VOL_R(V_SFX)    = 0x2200;

    /* Start the looping wind ambience (quiet, behind the music). */
    SPU_CH_ADDR(V_WIND) = getSPUAddr(wind_addr);
    SPU_CH_FREQ(V_WIND) = getSPUSampleRate(WIND_RATE);
    SPU_CH_VOL_L(V_WIND) = 0x0d00; SPU_CH_VOL_R(V_WIND) = 0x0d00;
    SpuSetKey(1, 1 << V_WIND);

    frame_ctr = 0;
    step = 0;
    sfx_timer = 0;
}

void audio_set_music(int level) {
    if (level < 0) level = 0; if (level > 3) level = 3;
    music_level = level;
    SPU_CH_VOL_L(V_MELODY) = SPU_CH_VOL_R(V_MELODY) = mus_vol[level];
    SPU_CH_VOL_L(V_BASS)   = SPU_CH_VOL_R(V_BASS)   = mus_vol[level];
}
void audio_set_sfx(int on) { sfx_on = on ? 1 : 0; }
void audio_set_wind(int on) {
    wind_on = on ? 1 : 0;
    SPU_CH_VOL_L(V_WIND) = SPU_CH_VOL_R(V_WIND) = wind_on ? 0x0d00 : 0;
}
int audio_music_level(void) { return music_level; }
int audio_sfx_on(void) { return sfx_on; }
int audio_wind_on(void) { return wind_on; }

/* One-shot sound effects, played on V_SFX with the square sample. */
void audio_sfx(int kind) {
    if (!sfx_on) return;
    int hz = 220, frames = 6;
    switch (kind) {
        case AUDIO_SFX_STEP:    hz = 150; frames = 4; break;
        case AUDIO_SFX_CONFIRM: hz = 660; frames = 5; break;
        case AUDIO_SFX_HIT:     hz = 300; frames = 7; break;
        case AUDIO_SFX_MAGIC:   hz = 880; frames = 10; break;
    }
    SpuSetKey(0, 1 << V_SFX);
    SPU_CH_ADDR(V_SFX) = getSPUAddr(sample_addr);
    SPU_CH_FREQ(V_SFX) = getSPUSampleRate(hz * WAVE_PERIOD);
    SpuSetKey(1, 1 << V_SFX);
    sfx_timer = frames;
}

static void play_note(int ch, int hz) {
    SpuSetKey(0, 1 << ch);                       /* key off */
    if (hz <= 0) return;
    SPU_CH_ADDR(ch) = getSPUAddr(sample_addr);
    SPU_CH_FREQ(ch) = getSPUSampleRate(hz * WAVE_PERIOD);
    SpuSetKey(1, 1 << ch);                       /* key on  */
}

void audio_tick(void) {
    if (frame_ctr == 0) {
        play_note(V_MELODY, melody[step]);
        play_note(V_BASS,   bass[step]);
        step = (step + 1) % SONG_LEN;
    }
    frame_ctr++;
    if (frame_ctr >= STEP_FRAMES) frame_ctr = 0;

    /* Auto-stop one-shot SFX after its short duration. */
    if (sfx_timer > 0) {
        sfx_timer--;
        if (sfx_timer == 0) SpuSetKey(0, 1 << V_SFX);
    }
}
