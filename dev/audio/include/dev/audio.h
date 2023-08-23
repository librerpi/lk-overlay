#pragma once

void audio_start(uint32_t samplerate, bool stereo);
void audio_push_stereo(const int16_t *data, int samples);
void audio_push_mono_16bit(const int16_t *data, int samples);
void audio_push_mono_8bit(const uint8_t *data, int samples);
