#pragma once
#include "media_process.hpp"

namespace lb::recording::audio {
// Owns WASAPI entirely on one worker. Preparing never starts either endpoint.
// Both inputs use the same QPC origin as the video process launch.
class Capture {
    struct Impl;
    std::shared_ptr<Impl> impl_;
    friend bool lifetime_tests(std::filesystem::path const& directory);
public:
    Capture(std::filesystem::path directory,bool system,bool microphone,bool english,std::shared_ptr<std::atomic_bool> stop);
    ~Capture();
    HRESULT prepare();
    void begin();
    HRESULT finish();
    HRESULT status() const;
    bool abandoned() const;
    std::wstring error_text() const;
    std::vector<std::filesystem::path> paths() const;
};
HRESULT mux(std::filesystem::path const& video,std::vector<std::filesystem::path> const& tracks,std::filesystem::path const& output,int64_t duration_us,bool lossless,media::Control& control);
bool tests(std::filesystem::path const& directory);
bool lifetime_tests(std::filesystem::path const& directory);
void cleanup_abandoned(std::filesystem::path const& directory);
}
