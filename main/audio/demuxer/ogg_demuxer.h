#ifndef OGG_DEMUXER_H_
#define OGG_DEMUXER_H_

#include <functional>
#include <cstdint>
#include <cstring>
#include <vector>

class OggDemuxer {
private:
    enum ParseState : int8_t {
        FIND_PAGE,
        PARSE_HEADER,
        PARSE_SEGMENTS,
        PARSE_DATA
    };

    struct Opus_t {
        bool    head_seen{false};
        bool    tags_seen{false};
        int     sample_rate{48000};
    };


    // Use fixed-size buffers to avoid dynamic allocation
    struct context_t {
        bool packet_continued{false};   // Whether the current packet spans multiple segments
        uint8_t header[27];             // Ogg page header
        uint8_t seg_table[255];         // Currently stored segment table
        uint8_t packet_buf[8192];       // 8 KB packet buffer
        size_t packet_len = 0;          // Accumulated data length in the buffer
        size_t seg_count = 0;           // Number of segments in the current page
        size_t seg_index = 0;           // Index of the segment being processed
        size_t data_offset = 0;         // Bytes read in the current parse stage
        size_t bytes_needed = 0;        // Bytes still needed to parse the current field
        size_t seg_remaining = 0;       // Remaining bytes to read in the current segment
        size_t body_size = 0;           // Total body size
        size_t body_offset = 0;         // Bytes read from the body so far
    };
    
public:
    OggDemuxer() {
        Reset();
    }
    
    void Reset();
    
    size_t Process(const uint8_t* data, size_t size);

    /// @brief Set callback invoked when demuxing completes
    /// @param on_demuxer_finished 
    void OnDemuxerFinished(std::function<void(const uint8_t* data, int sample_rate, size_t len)> on_demuxer_finished) {
        on_demuxer_finished_ = on_demuxer_finished;
    }
private:

    ParseState  state_ = ParseState::FIND_PAGE;
    context_t   ctx_;
    Opus_t      opus_info_;
    std::function<void(const uint8_t*, int, size_t)> on_demuxer_finished_;
};

#endif
