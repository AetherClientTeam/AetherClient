#ifndef GAME_CLIENT_COMPONENTS_AETHER_AUDIO_DECODER_H
#define GAME_CLIENT_COMPONENTS_AETHER_AUDIO_DECODER_H

#include <vector>

namespace AetherAudio
{
bool DecodeAudioFileToS16Pcm(const char *pAbsolutePath, std::vector<short> &vInterleavedOut, int &Channels, int &SampleRate, const char *pContextName);
}

#endif
