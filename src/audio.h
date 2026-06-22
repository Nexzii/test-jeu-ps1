#ifndef AUDIO_H
#define AUDIO_H

#define AUDIO_SFX_STEP    0
#define AUDIO_SFX_CONFIRM 1
#define AUDIO_SFX_HIT     2
#define AUDIO_SFX_MAGIC   3

void audio_init(void);   /* call once after ResetGraph */
void audio_tick(void);   /* call once per frame to advance the music */
void audio_sfx(int kind);/* play a one-shot sound effect */

/* Settings (for the options menu). */
void audio_set_music(int level);  /* 0=off .. 3=high */
void audio_set_sfx(int on);
void audio_set_wind(int on);
int  audio_music_level(void);
int  audio_sfx_on(void);
int  audio_wind_on(void);

#endif
