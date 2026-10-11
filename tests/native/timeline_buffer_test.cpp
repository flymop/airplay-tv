#include "timeline_buffer.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr int64_t kMs = 1'000'000;

void writeMs(TimelineBuffer &buffer, int ms, int16_t value, int64_t pts) {
    std::vector<int16_t> pcm(kRate * ms / 1000 * kChannels, value);
    buffer.write(pcm.data(), pcm.size(), pts);
}

void expectRead(TimelineBuffer &buffer, int ms, int16_t expected) {
    std::vector<int16_t> pcm(kRate * ms / 1000 * kChannels, -1);
    buffer.read(pcm.data(), static_cast<int32_t>(pcm.size() / kChannels));
    for (int16_t value : pcm) {
        if (value != expected) {
            throw std::runtime_error("expected PCM " + std::to_string(expected) +
                                     ", got " + std::to_string(value));
        }
    }
}

// Model the output stopped at an episode boundary; no producer/consumer is
// running during flush. Both packets' clocks are valid but need not be adjacent.
void episodeSwitch(int64_t newPts) {
    TimelineBuffer buffer(kRate, kChannels, 40, 95);
    writeMs(buffer, 40, 11, 1000 * kMs);
    expectRead(buffer, 10, 11);
    buffer.flushAndReprime();
    writeMs(buffer, 40, 22, newPts);
    expectRead(buffer, 40, 22);
    if (buffer.debugInfo().backlogMs != 0) throw std::runtime_error("stale episode tail");
}

void flushResetsPrimingWait() {
    TimelineBuffer buffer(kRate, kChannels, 40, 95);
    writeMs(buffer, 10, 11, 1000 * kMs);
    for (int i = 0; i < 6; ++i) expectRead(buffer, 10, 0);
    buffer.flushAndReprime();
    writeMs(buffer, 10, 22, 1010 * kMs);
    // A short sound gets its own full 2*cushion wait, not the old episode's wait.
    for (int i = 0; i < 7; ++i) expectRead(buffer, 10, 0);
    expectRead(buffer, 10, 22);
}

void normalGapStillProducesSilence() {
    TimelineBuffer buffer(kRate, kChannels, 40, 95);
    writeMs(buffer, 40, 11, 1000 * kMs);
    writeMs(buffer, 40, 22, 1080 * kMs);
    expectRead(buffer, 40, 11);
    expectRead(buffer, 40, 0);
    expectRead(buffer, 40, 22);
}

void overlapStillDropsWithoutFlush() {
    TimelineBuffer buffer(kRate, kChannels, 40, 95);
    writeMs(buffer, 40, 11, 1000 * kMs);
    writeMs(buffer, 40, 22, 1000 * kMs);
    expectRead(buffer, 40, 11);
    if (buffer.debugInfo().metrics.drops != 1) throw std::runtime_error("overlap accepted");
}

void unsyncedPacketAfterFlush() {
    TimelineBuffer buffer(kRate, kChannels, 40, 95);
    writeMs(buffer, 40, 11, 1000 * kMs);
    buffer.flushAndReprime();
    writeMs(buffer, 20, 22, 0);
    writeMs(buffer, 20, 33, 800 * kMs);
    expectRead(buffer, 20, 22);
    expectRead(buffer, 20, 33);
}

void flushResetsJitterHistory() {
    TimelineBuffer buffer(kRate, kChannels, 0, 95);
    writeMs(buffer, 40, 11, monoNs() + 1000 * kMs);
    // Simulate a segment whose clock moved back, inflating arrival-delay history.
    for (int i = 0; i < 16; ++i) writeMs(buffer, 40, 11, monoNs() + 600 * kMs);
    if (buffer.debugInfo().tunedCushionMs < 300) throw std::runtime_error("jitter not injected");
    buffer.flushAndReprime();
    writeMs(buffer, 40, 22, monoNs() + 2000 * kMs);
    if (buffer.debugInfo().tunedCushionMs > 60) throw std::runtime_error("old jitter delays new episode");
}
}  // namespace

int main() {
    int failed = 0;
    auto run = [&](const char *name, auto test) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
            ++failed;
        }
    };
    run("episode: forward PTS jump", [] { episodeSwitch(1300 * kMs); });
    run("episode: backward PTS jump", [] { episodeSwitch(800 * kMs); });
    run("episode: continuous PTS", [] { episodeSwitch(1040 * kMs); });
    run("episode: fresh priming wait", flushResetsPrimingWait);
    run("episode: unsynced then synced PTS", unsyncedPacketAfterFlush);
    run("episode: fresh jitter history", flushResetsJitterHistory);
    run("normal stream: timestamp gap", normalGapStillProducesSilence);
    run("normal stream: overlap", overlapStillDropsWithoutFlush);
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
