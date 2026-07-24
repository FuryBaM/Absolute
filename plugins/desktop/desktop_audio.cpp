// Soft audio: load 16-bit PCM WAV + Windows waveOut software mixer.
// Other platforms: stub (isValid = false).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")
#endif

namespace {
    constexpr int32_t kSampleRate = 44100;
    constexpr int32_t kChannels = 2;
    constexpr int32_t kBits = 16;
    constexpr int32_t kFramesPerBuffer = 1024;
    constexpr int32_t kMaxVoices = 32;
    constexpr int32_t kMaxSounds = 64;

    struct SoundClip {
        std::vector<float> samples; // interleaved stereo float -1..1
        int32_t frameCount = 0;
        bool valid = false;
    };

    struct Voice {
        SoundClip* clip = nullptr;
        int32_t framePos = 0;
        float volume = 1.0f;
        bool loop = false;
        bool active = false;
    };

#if defined(_WIN32)
    struct AudioDevice {
        HWAVEOUT waveOut = nullptr;
        WAVEHDR headers[2]{};
        std::vector<int16_t> pcm[2];
        std::mutex mutex;
        std::vector<Voice> voices;
        std::vector<SoundClip*> sounds;
        float master = 1.0f;
        std::atomic<bool> running{false};
        std::thread thread;
        bool valid = false;

        void Destroy();
    };

    AudioDevice* DeviceFromHandle(int64_t handle) {
        return reinterpret_cast<AudioDevice*>(static_cast<intptr_t>(handle));
    }

    SoundClip* SoundFromHandle(int64_t handle) {
        return reinterpret_cast<SoundClip*>(static_cast<intptr_t>(handle));
    }

    int64_t ToHandle(void* p) {
        return static_cast<int64_t>(reinterpret_cast<intptr_t>(p));
    }

    uint32_t ReadU32LE(const uint8_t* p) {
        return static_cast<uint32_t>(p[0])
            | (static_cast<uint32_t>(p[1]) << 8)
            | (static_cast<uint32_t>(p[2]) << 16)
            | (static_cast<uint32_t>(p[3]) << 24);
    }

    uint16_t ReadU16LE(const uint8_t* p) {
        return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
    }

    // Decode PCM WAV into stereo float @ 44100.
    SoundClip* LoadWavFile(const char* path) {
        if (!path || !path[0]) return nullptr;
        std::ifstream file(path, std::ios::binary);
        if (!file) return nullptr;
        file.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        if (size < 44 || size > 64u * 1024u * 1024u) return nullptr;
        std::vector<uint8_t> data(size);
        if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
            return nullptr;
        }
        if (std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
            return nullptr;
        }

        uint16_t audioFormat = 0;
        uint16_t channels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        const uint8_t* pcmData = nullptr;
        uint32_t pcmBytes = 0;

        std::size_t offset = 12;
        while (offset + 8 <= size) {
            const char* id = reinterpret_cast<const char*>(data.data() + offset);
            const uint32_t chunkSize = ReadU32LE(data.data() + offset + 4);
            const std::size_t chunkData = offset + 8;
            if (chunkData + chunkSize > size) break;
            if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
                audioFormat = ReadU16LE(data.data() + chunkData);
                channels = ReadU16LE(data.data() + chunkData + 2);
                sampleRate = ReadU32LE(data.data() + chunkData + 4);
                bitsPerSample = ReadU16LE(data.data() + chunkData + 14);
            } else if (std::memcmp(id, "data", 4) == 0) {
                pcmData = data.data() + chunkData;
                pcmBytes = chunkSize;
            }
            offset = chunkData + chunkSize + (chunkSize & 1u); // word align
        }

        if (!pcmData || pcmBytes == 0) return nullptr;
        if (audioFormat != 1) return nullptr; // PCM only
        if (channels != 1 && channels != 2) return nullptr;
        if (bitsPerSample != 8 && bitsPerSample != 16) return nullptr;
        if (sampleRate < 4000 || sampleRate > 192000) return nullptr;

        const int32_t srcChannels = channels;
        const int32_t bytesPerSample = bitsPerSample / 8;
        const int32_t srcFrameBytes = srcChannels * bytesPerSample;
        if (srcFrameBytes <= 0 || pcmBytes < static_cast<uint32_t>(srcFrameBytes)) return nullptr;
        const int32_t srcFrames = static_cast<int32_t>(pcmBytes / static_cast<uint32_t>(srcFrameBytes));

        // Decode to mono float first, then expand to stereo and resample.
        std::vector<float> mono(static_cast<std::size_t>(srcFrames));
        for (int32_t i = 0; i < srcFrames; ++i) {
            const uint8_t* frame = pcmData + static_cast<std::size_t>(i) * srcFrameBytes;
            float sample = 0.0f;
            if (bitsPerSample == 16) {
                if (srcChannels == 1) {
                    const int16_t s = static_cast<int16_t>(frame[0] | (frame[1] << 8));
                    sample = static_cast<float>(s) / 32768.0f;
                } else {
                    const int16_t s0 = static_cast<int16_t>(frame[0] | (frame[1] << 8));
                    const int16_t s1 = static_cast<int16_t>(frame[2] | (frame[3] << 8));
                    sample = (static_cast<float>(s0) + static_cast<float>(s1)) * 0.5f / 32768.0f;
                }
            } else {
                if (srcChannels == 1) {
                    sample = (static_cast<float>(frame[0]) - 128.0f) / 128.0f;
                } else {
                    sample = ((static_cast<float>(frame[0]) + static_cast<float>(frame[1])) * 0.5f - 128.0f) / 128.0f;
                }
            }
            mono[static_cast<std::size_t>(i)] = std::clamp(sample, -1.0f, 1.0f);
        }

        const double ratio = static_cast<double>(kSampleRate) / static_cast<double>(sampleRate);
        const int32_t dstFrames = std::max(1, static_cast<int32_t>(std::llround(srcFrames * ratio)));
        auto* clip = new SoundClip();
        clip->frameCount = dstFrames;
        clip->samples.resize(static_cast<std::size_t>(dstFrames) * 2u);
        for (int32_t i = 0; i < dstFrames; ++i) {
            const double srcPos = static_cast<double>(i) / ratio;
            int32_t i0 = static_cast<int32_t>(srcPos);
            int32_t i1 = std::min(i0 + 1, srcFrames - 1);
            if (i0 < 0) i0 = 0;
            if (i0 >= srcFrames) i0 = srcFrames - 1;
            const float t = static_cast<float>(srcPos - static_cast<double>(i0));
            const float s = mono[static_cast<std::size_t>(i0)] * (1.0f - t)
                + mono[static_cast<std::size_t>(i1)] * t;
            clip->samples[static_cast<std::size_t>(i) * 2u] = s;
            clip->samples[static_cast<std::size_t>(i) * 2u + 1u] = s;
        }
        clip->valid = true;
        return clip;
    }

    void MixBuffer(AudioDevice& device, int16_t* out, int32_t frames) {
        std::fill(out, out + frames * kChannels, static_cast<int16_t>(0));
        std::lock_guard<std::mutex> lock(device.mutex);
        const float master = std::clamp(device.master, 0.0f, 1.0f);
        for (Voice& voice : device.voices) {
            if (!voice.active || !voice.clip || !voice.clip->valid) continue;
            SoundClip& clip = *voice.clip;
            for (int32_t f = 0; f < frames; ++f) {
                if (voice.framePos >= clip.frameCount) {
                    if (voice.loop) {
                        voice.framePos = 0;
                    } else {
                        voice.active = false;
                        break;
                    }
                }
                const float gain = voice.volume * master;
                const float* src = clip.samples.data()
                    + static_cast<std::size_t>(voice.framePos) * 2u;
                for (int32_t c = 0; c < kChannels; ++c) {
                    float mixed = static_cast<float>(out[f * kChannels + c]) / 32768.0f;
                    mixed += src[c] * gain;
                    mixed = std::clamp(mixed, -1.0f, 1.0f);
                    out[f * kChannels + c] = static_cast<int16_t>(mixed * 32767.0f);
                }
                voice.framePos += 1;
            }
        }
    }

    void MixerThreadMain(AudioDevice* device) {
        int bufferIndex = 0;
        while (device->running.load()) {
            WAVEHDR& hdr = device->headers[bufferIndex];
            // Wait until this header is not queued.
            while (device->running.load() && (hdr.dwFlags & WHDR_INQUEUE)) {
                Sleep(1);
            }
            if (!device->running.load()) break;

            if (hdr.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(device->waveOut, &hdr, sizeof(hdr));
            }

            MixBuffer(*device, device->pcm[bufferIndex].data(), kFramesPerBuffer);
            std::memset(&hdr, 0, sizeof(hdr));
            hdr.lpData = reinterpret_cast<LPSTR>(device->pcm[bufferIndex].data());
            hdr.dwBufferLength = static_cast<DWORD>(device->pcm[bufferIndex].size() * sizeof(int16_t));
            if (waveOutPrepareHeader(device->waveOut, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR) {
                waveOutWrite(device->waveOut, &hdr, sizeof(hdr));
            }
            bufferIndex = 1 - bufferIndex;
        }
    }

    void AudioDevice::Destroy() {
        running = false;
        if (thread.joinable()) thread.join();
        if (waveOut) {
            waveOutReset(waveOut);
            for (int i = 0; i < 2; ++i) {
                if (headers[i].dwFlags & WHDR_PREPARED) {
                    waveOutUnprepareHeader(waveOut, &headers[i], sizeof(headers[i]));
                }
            }
            waveOutClose(waveOut);
            waveOut = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (SoundClip* s : sounds) delete s;
            sounds.clear();
            voices.clear();
        }
        valid = false;
    }
#endif
}

#if defined(_WIN32)

extern "C" int64_t absolute_desktop_audio_create() {
    auto* device = new AudioDevice();
    device->voices.resize(kMaxVoices);
    device->pcm[0].assign(static_cast<std::size_t>(kFramesPerBuffer * kChannels), 0);
    device->pcm[1].assign(static_cast<std::size_t>(kFramesPerBuffer * kChannels), 0);

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(kChannels);
    format.nSamplesPerSec = static_cast<DWORD>(kSampleRate);
    format.wBitsPerSample = static_cast<WORD>(kBits);
    format.nBlockAlign = static_cast<WORD>(kChannels * kBits / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const MMRESULT openResult = waveOutOpen(
        &device->waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (openResult != MMSYSERR_NOERROR || !device->waveOut) {
        delete device;
        return 0;
    }

    device->running = true;
    device->valid = true;
    device->thread = std::thread(MixerThreadMain, device);
    return ToHandle(device);
}

extern "C" void absolute_desktop_audio_destroy(int64_t handle) {
    AudioDevice* device = DeviceFromHandle(handle);
    if (!device) return;
    device->Destroy();
    delete device;
}

extern "C" int32_t absolute_desktop_audio_is_valid(int64_t handle) {
    const AudioDevice* device = DeviceFromHandle(handle);
    return device && device->valid ? 1 : 0;
}

extern "C" void absolute_desktop_audio_set_master(int64_t handle, float volume) {
    AudioDevice* device = DeviceFromHandle(handle);
    if (!device) return;
    std::lock_guard<std::mutex> lock(device->mutex);
    device->master = std::clamp(volume, 0.0f, 1.0f);
}

extern "C" float absolute_desktop_audio_master(int64_t handle) {
    AudioDevice* device = DeviceFromHandle(handle);
    if (!device) return 0.0f;
    std::lock_guard<std::mutex> lock(device->mutex);
    return device->master;
}

extern "C" void absolute_desktop_audio_stop_all(int64_t handle) {
    AudioDevice* device = DeviceFromHandle(handle);
    if (!device) return;
    std::lock_guard<std::mutex> lock(device->mutex);
    for (Voice& v : device->voices) {
        v.active = false;
        v.clip = nullptr;
        v.framePos = 0;
    }
}

extern "C" int64_t absolute_desktop_sound_load_wav(int64_t audioHandle, const char* path) {
    AudioDevice* device = DeviceFromHandle(audioHandle);
    if (!device || !device->valid) return 0;
    SoundClip* clip = LoadWavFile(path);
    if (!clip) return 0;
    std::lock_guard<std::mutex> lock(device->mutex);
    if (static_cast<int32_t>(device->sounds.size()) >= kMaxSounds) {
        delete clip;
        return 0;
    }
    device->sounds.push_back(clip);
    return ToHandle(clip);
}

extern "C" void absolute_desktop_sound_destroy(int64_t audioHandle, int64_t soundHandle) {
    AudioDevice* device = DeviceFromHandle(audioHandle);
    SoundClip* clip = SoundFromHandle(soundHandle);
    if (!device || !clip) return;
    std::lock_guard<std::mutex> lock(device->mutex);
    for (Voice& v : device->voices) {
        if (v.clip == clip) {
            v.active = false;
            v.clip = nullptr;
        }
    }
    auto it = std::find(device->sounds.begin(), device->sounds.end(), clip);
    if (it != device->sounds.end()) device->sounds.erase(it);
    delete clip;
}

extern "C" int32_t absolute_desktop_sound_is_valid(int64_t soundHandle) {
    const SoundClip* clip = SoundFromHandle(soundHandle);
    return clip && clip->valid ? 1 : 0;
}

extern "C" float absolute_desktop_sound_duration(int64_t soundHandle) {
    const SoundClip* clip = SoundFromHandle(soundHandle);
    if (!clip || !clip->valid || clip->frameCount <= 0) return 0.0f;
    return static_cast<float>(clip->frameCount) / static_cast<float>(kSampleRate);
}

// Returns 1 if a free voice was started, 0 otherwise.
extern "C" int32_t absolute_desktop_sound_play(
    int64_t audioHandle, int64_t soundHandle, float volume, int32_t loop) {
    AudioDevice* device = DeviceFromHandle(audioHandle);
    SoundClip* clip = SoundFromHandle(soundHandle);
    if (!device || !device->valid || !clip || !clip->valid) return 0;
    std::lock_guard<std::mutex> lock(device->mutex);
    for (Voice& v : device->voices) {
        if (!v.active) {
            v.clip = clip;
            v.framePos = 0;
            v.volume = std::clamp(volume, 0.0f, 1.0f);
            v.loop = loop != 0;
            v.active = true;
            return 1;
        }
    }
    return 0;
}

extern "C" void absolute_desktop_sound_stop(int64_t audioHandle, int64_t soundHandle) {
    AudioDevice* device = DeviceFromHandle(audioHandle);
    SoundClip* clip = SoundFromHandle(soundHandle);
    if (!device || !clip) return;
    std::lock_guard<std::mutex> lock(device->mutex);
    for (Voice& v : device->voices) {
        if (v.clip == clip) {
            v.active = false;
            v.clip = nullptr;
            v.framePos = 0;
        }
    }
}

extern "C" int32_t absolute_desktop_sound_is_playing(int64_t audioHandle, int64_t soundHandle) {
    AudioDevice* device = DeviceFromHandle(audioHandle);
    SoundClip* clip = SoundFromHandle(soundHandle);
    if (!device || !clip) return 0;
    std::lock_guard<std::mutex> lock(device->mutex);
    for (const Voice& v : device->voices) {
        if (v.active && v.clip == clip) return 1;
    }
    return 0;
}

extern "C" int32_t absolute_desktop_audio_active_voices(int64_t handle) {
    AudioDevice* device = DeviceFromHandle(handle);
    if (!device) return 0;
    std::lock_guard<std::mutex> lock(device->mutex);
    int32_t n = 0;
    for (const Voice& v : device->voices) {
        if (v.active) ++n;
    }
    return n;
}

#else

extern "C" int64_t absolute_desktop_audio_create() { return 0; }
extern "C" void absolute_desktop_audio_destroy(int64_t) {}
extern "C" int32_t absolute_desktop_audio_is_valid(int64_t) { return 0; }
extern "C" void absolute_desktop_audio_set_master(int64_t, float) {}
extern "C" float absolute_desktop_audio_master(int64_t) { return 0.0f; }
extern "C" void absolute_desktop_audio_stop_all(int64_t) {}
extern "C" int64_t absolute_desktop_sound_load_wav(int64_t, const char*) { return 0; }
extern "C" void absolute_desktop_sound_destroy(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_sound_is_valid(int64_t) { return 0; }
extern "C" float absolute_desktop_sound_duration(int64_t) { return 0.0f; }
extern "C" int32_t absolute_desktop_sound_play(int64_t, int64_t, float, int32_t) { return 0; }
extern "C" void absolute_desktop_sound_stop(int64_t, int64_t) {}
extern "C" int32_t absolute_desktop_sound_is_playing(int64_t, int64_t) { return 0; }
extern "C" int32_t absolute_desktop_audio_active_voices(int64_t) { return 0; }

#endif
