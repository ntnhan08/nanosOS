/* NanosOS — apps/media_dashboard.c - Media player UI */
#include "../kernel/kernel.h"
#include "../gui/gui.h"
#include "../audio/audio.h"

static const char *TAG = "MEDIA";
static char s_track[64] = "No media";
static char s_artist[64] = "";
static uint8_t s_volume = 200;
static bool s_playing = false;

void media_set_track(const char *track, const char *artist) {
    strncpy(s_track, track ? track : "", 63);
    strncpy(s_artist, artist ? artist : "", 63);
    KLOGI(TAG, "Now playing: %s — %s", s_artist, s_track);
}

void media_set_playing(bool playing) {
    s_playing = playing;
    KLOGI(TAG, "Playback: %s", playing ? "PLAY" : "PAUSE");
}

void media_set_volume(uint8_t vol) {
    s_volume = vol;
    audio_set_master_vol(vol);
}

bool media_is_playing(void) { return s_playing; }
const char *media_get_track(void)  { return s_track; }
const char *media_get_artist(void) { return s_artist; }
uint8_t media_get_volume(void)     { return s_volume; }
