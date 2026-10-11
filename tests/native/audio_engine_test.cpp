// Exercise the real engine + timeline, replacing only Android decoder/output/log
// dependencies. The decoder double emits a final queued frame during shutdown,
// which catches clearing the timeline before joining the decoder's drain thread.
#define AUDIO_DECODER_H
#define AUDIO_OUTPUT_H
#define LOG_SINK_H

#include "timeline_buffer.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

struct LogSink {
    void error(const char *, ...) {}
};

class LatencyReporter {
public:
    struct __attribute__((packed)) Debug { int32_t meanUs, maxUs; uint16_t held; };
    LatencyReporter(const char *, LogSink &) {}
    void setEnableLogging(bool) {}
    void setHeld(size_t) {}
    Debug debugInfo() { return {}; }
};

namespace oboe {
struct DefaultStreamValues {
    inline static int SampleRate = 0, FramesPerBurst = 0;
};
}

class AudioOutput {
public:
    inline static AudioOutput *latest = nullptr;
    bool running = false;
    int starts = 0;
    std::shared_ptr<TimelineBuffer> timeline;
    struct __attribute__((packed)) Debug { int32_t xrun; };
    static std::shared_ptr<AudioOutput> create(int, int, int, bool,
            std::shared_ptr<TimelineBuffer> timeline, std::shared_ptr<LogSink>) {
        auto output = std::make_shared<AudioOutput>();
        output->timeline = std::move(timeline);
        latest = output.get();
        return output;
    }
    bool start() { running = true; ++starts; return true; }
    void stop() { running = false; }
    Debug debugInfo() { return {}; }
};

constexpr int CT_ALAC = 2, CT_AAC_LC = 4, CT_AAC_ELD = 8;
constexpr int kFrames = 44100 * 40 / 1000;
class Decoder {
public:
    inline static int instances = 0, lastSpf = 0;
    explicit Decoder(TimelineBuffer &timeline) : mTimeline(timeline) { ++instances; }
    ~Decoder() {
        if (mPts == 0) return;
        std::vector<int16_t> tail(kFrames * 2, 99);
        mTimeline.write(tail.data(), tail.size(), mPts + 40'000'000);
    }
    bool decode(const uint8_t *data, size_t, int64_t pts) {
        std::vector<int16_t> pcm(kFrames * 2, data[0]);
        mTimeline.write(pcm.data(), pcm.size(), pts);
        mPts = pts;
        return true;
    }
private:
    TimelineBuffer &mTimeline;
    int64_t mPts = 0;
};

static std::unique_ptr<Decoder> makeDecoder(int, int spf, int, int,
        TimelineBuffer &timeline, LatencyReporter &, LogSink &, bool, bool, bool) {
    Decoder::lastSpf = spf;
    return std::make_unique<Decoder>(timeline);
}

#include "../../app/src/main/cpp/audio_engine.cpp"

static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

static void expectOutput(int16_t expected) {
    std::vector<int16_t> pcm(kFrames * 2, -1);
    AudioOutput::latest->timeline->read(pcm.data(), kFrames);
    for (auto sample : pcm) require(sample == expected, "unexpected audio after FLUSH");
}

int main() {
    try {
        auto log = std::make_shared<LogSink>();
        std::unique_ptr<AudioEngine, decltype(&audio_engine_destroy)> engine(
            audio_engine_create(log, 44100, 2), audio_engine_destroy);
        audio_engine_flush(nullptr);
        audio_engine_flush(engine.get()); // safe before configuration
        require(audio_engine_configure(engine.get(), 40, 95, 0, false, true, true, false),
                "configuration failed");
        audio_engine_on_format(engine.get(), CT_AAC_ELD, 512);
        require(audio_engine_start(engine.get()), "start failed");
        const uint8_t oldPacket[] = {11}, newPacket[] = {22};
        audio_engine_decode(engine.get(), oldPacket, 1, CT_AAC_ELD, 1'000'000'000);

        audio_engine_flush(engine.get());
        require(AudioOutput::latest->running, "FLUSH stopped a running session");
        expectOutput(0); // neither queued PCM nor final decoder output may survive
        audio_engine_decode(engine.get(), newPacket, 1, CT_AAC_ELD, 800'000'000);
        require(Decoder::instances == 2, "old decoder reused after FLUSH");
        require(Decoder::lastSpf == 512, "negotiated frame length lost after FLUSH");
        expectOutput(22);
        std::cout << "PASS engine: FLUSH clears decoder and PCM, preserves format, resumes output\n";

        audio_engine_pause(engine.get());
        const int starts = AudioOutput::latest->starts;
        audio_engine_flush(engine.get());
        require(!AudioOutput::latest->running && AudioOutput::latest->starts == starts,
                "FLUSH resumed a paused session");
        expectOutput(0);
        std::cout << "PASS engine: FLUSH preserves paused state\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL engine: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
