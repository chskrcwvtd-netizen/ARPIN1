#include "engine/MidiFile.h"

#include "engine/Util.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace phrasearp {
namespace {

void write32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back((uint8_t) ((v >> 24) & 0xFF));
    o.push_back((uint8_t) ((v >> 16) & 0xFF));
    o.push_back((uint8_t) ((v >> 8) & 0xFF));
    o.push_back((uint8_t) (v & 0xFF));
}

void write16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back((uint8_t) ((v >> 8) & 0xFF));
    o.push_back((uint8_t) (v & 0xFF));
}

void writeVlq(std::vector<uint8_t>& o, uint32_t value) {
    uint8_t bytes[4];
    int n = 0;
    bytes[n++] = (uint8_t) (value & 0x7F);
    value >>= 7;
    while (value > 0 && n < 4) {
        bytes[n++] = (uint8_t) ((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (int i = n - 1; i >= 0; --i)
        o.push_back(bytes[i]);
}

uint32_t read32(const std::vector<uint8_t>& b, size_t i) {
    return ((uint32_t) b[i] << 24) | ((uint32_t) b[i + 1] << 16) | ((uint32_t) b[i + 2] << 8) | b[i + 3];
}

uint16_t read16(const std::vector<uint8_t>& b, size_t i) {
    return (uint16_t) (((uint16_t) b[i] << 8) | b[i + 1]);
}

uint32_t readVlq(const std::vector<uint8_t>& b, size_t& i) {
    uint32_t v = 0;
    for (int k = 0; k < 4 && i < b.size(); ++k) {
        const uint8_t byte = b[i++];
        v = (v << 7) | (byte & 0x7F);
        if ((byte & 0x80) == 0)
            break;
    }
    return v;
}

struct RawEvent {
    int tick = 0;
    std::vector<uint8_t> data;
};

std::vector<uint8_t> encodeTrack(std::vector<RawEvent> events) {
    std::sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return a.data < b.data;
    });
    std::vector<uint8_t> body;
    int last = 0;
    for (const auto& e : events) {
        const int tick = std::max(0, e.tick);
        writeVlq(body, (uint32_t) (tick - last));
        last = tick;
        body.insert(body.end(), e.data.begin(), e.data.end());
    }
    RawEvent end;
    end.tick = last;
    // end is written with delta 0 after the last event by appending directly
    writeVlq(body, 0);
    body.push_back(0xFF);
    body.push_back(0x2F);
    body.push_back(0x00);

    std::vector<uint8_t> track;
    track.insert(track.end(), {'M', 'T', 'r', 'k'});
    write32(track, (uint32_t) body.size());
    track.insert(track.end(), body.begin(), body.end());
    return track;
}

int denomLog(int denominator) {
    int d = 0;
    int v = std::max(1, denominator);
    while (v > 1 && (v % 2) == 0) {
        v /= 2;
        ++d;
    }
    return d;
}

void addNote(std::vector<RawEvent>& events, const SmfNote& note) {
    const int channel = std::clamp(note.channel, 0, 15);
    const int pitch = std::clamp(note.note, 0, 127);
    const int vel = std::clamp(note.velocity, 1, 127);
    const int on = std::max(0, note.tick);
    const int off = std::max(on + 1, on + std::max(1, note.duration));
    RawEvent a;
    a.tick = on;
    a.data = {(uint8_t) (0x90 | channel), (uint8_t) pitch, (uint8_t) vel};
    RawEvent b;
    b.tick = off;
    b.data = {(uint8_t) (0x80 | channel), (uint8_t) pitch, 0};
    events.push_back(std::move(a));
    events.push_back(std::move(b));
}

} // namespace

std::vector<uint8_t> writeSmf(const std::vector<SmfNote>& notes, double bpm, int numerator, int denominator,
                              const std::string& trackName, const std::vector<SmfNote>* chordNotes) {
    if (bpm < 1.0)
        bpm = 120.0;
    if (numerator < 1)
        numerator = 4;
    if (denominator < 1)
        denominator = 4;
    const int tracks = chordNotes != nullptr && !chordNotes->empty() ? 3 : 2;

    std::vector<uint8_t> file;
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    write32(file, 6);
    write16(file, 1);
    write16(file, (uint16_t) tracks);
    write16(file, (uint16_t) kPpq);

    const int uspq = std::clamp((int) std::llround(60000000.0 / bpm), 1, 0xFFFFFF);
    std::vector<RawEvent> meta;
    RawEvent name;
    name.tick = 0;
    name.data = {0xFF, 0x03, (uint8_t) std::min<size_t>(127, trackName.size())};
    name.data.insert(name.data.end(), trackName.begin(), trackName.begin() + std::min<size_t>(127, trackName.size()));
    meta.push_back(name);
    RawEvent tempo;
    tempo.tick = 0;
    tempo.data = {0xFF, 0x51, 0x03, (uint8_t) ((uspq >> 16) & 0xFF), (uint8_t) ((uspq >> 8) & 0xFF), (uint8_t) (uspq & 0xFF)};
    meta.push_back(tempo);
    RawEvent sig;
    sig.tick = 0;
    sig.data = {0xFF, 0x58, 0x04, (uint8_t) std::clamp(numerator, 1, 32), (uint8_t) denomLog(denominator), 24, 8};
    meta.push_back(sig);
    auto conductor = encodeTrack(meta);
    file.insert(file.end(), conductor.begin(), conductor.end());

    std::vector<RawEvent> noteEvents;
    for (const auto& n : notes)
        addNote(noteEvents, n);
    auto noteTrack = encodeTrack(noteEvents);
    file.insert(file.end(), noteTrack.begin(), noteTrack.end());

    if (tracks == 3) {
        std::vector<RawEvent> chordEvents;
        RawEvent cname;
        cname.tick = 0;
        const std::string label = "Chords";
        cname.data = {0xFF, 0x03, (uint8_t) label.size()};
        cname.data.insert(cname.data.end(), label.begin(), label.end());
        chordEvents.push_back(cname);
        for (const auto& n : *chordNotes)
            addNote(chordEvents, n);
        auto chordTrack = encodeTrack(chordEvents);
        file.insert(file.end(), chordTrack.begin(), chordTrack.end());
    }
    return file;
}

bool writeSmfFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize) bytes.size());
    return (bool) out;
}

ParsedMidi parseSmf(const std::vector<uint8_t>& bytes) {
    ParsedMidi parsed;
    if (bytes.size() < 14 || bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'h' || bytes[3] != 'd') {
        parsed.error = "Not a MIDI file";
        return parsed;
    }
    const uint32_t headerLen = read32(bytes, 4);
    if (headerLen < 6 || bytes.size() < 8 + headerLen) {
        parsed.error = "Not a MIDI file";
        return parsed;
    }
    const int format = read16(bytes, 8);
    const int ntrks = read16(bytes, 10);
    parsed.ppq = read16(bytes, 12);
    if ((format != 0 && format != 1) || ntrks < 1 || parsed.ppq <= 0) {
        parsed.error = "Not a MIDI file";
        return parsed;
    }
    size_t pos = 8 + headerLen;
    struct OpenNote { int tick; int vel; int channel; };
    std::vector<OpenNote> open[128];
    for (int track = 0; track < ntrks && pos + 8 <= bytes.size(); ++track) {
        if (bytes[pos] != 'M' || bytes[pos + 1] != 'T' || bytes[pos + 2] != 'r' || bytes[pos + 3] != 'k') {
            parsed.error = "Not a MIDI file";
            return parsed;
        }
        const uint32_t len = read32(bytes, pos + 4);
        pos += 8;
        if (pos + len > bytes.size()) {
            parsed.error = "Not a MIDI file";
            return parsed;
        }
        const size_t end = pos + len;
        int tick = 0;
        int running = -1;
        while (pos < end) {
            tick += (int) readVlq(bytes, pos);
            if (pos >= end)
                break;
            int status = bytes[pos];
            if (status & 0x80) {
                ++pos;
                running = status;
            } else if (running < 0) {
                break;
            } else {
                status = running;
            }
            const int kind = status & 0xF0;
            if (status == 0xFF) {
                if (pos >= end)
                    break;
                const int meta = bytes[pos++];
                const uint32_t mlen = readVlq(bytes, pos);
                if (pos + mlen > end)
                    break;
                if (meta == 0x51 && mlen == 3) {
                    const int us = (bytes[pos] << 16) | (bytes[pos + 1] << 8) | bytes[pos + 2];
                    if (us > 0)
                        parsed.bpm = 60000000.0 / (double) us;
                } else if (meta == 0x58 && mlen >= 2) {
                    parsed.numerator = std::max(1, (int) bytes[pos]);
                    parsed.denominator = 1 << std::clamp((int) bytes[pos + 1], 0, 6);
                } else if (meta == 0x2F) {
                    pos += mlen;
                    break;
                }
                pos += mlen;
            } else if (status == 0xF0 || status == 0xF7) {
                const uint32_t slen = readVlq(bytes, pos);
                pos += slen;
            } else if (kind == 0xC0 || kind == 0xD0) {
                if (pos < end)
                    ++pos;
            } else {
                if (pos + 1 >= end)
                    break;
                const int d1 = bytes[pos++];
                const int d2 = bytes[pos++];
                const int channel = status & 0x0F;
                if (kind == 0x90 && d2 > 0) {
                    open[d1].push_back({tick, d2, channel});
                } else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
                    if (!open[d1].empty()) {
                        const OpenNote started = open[d1].back();
                        open[d1].pop_back();
                        ParsedNote note;
                        note.onTick = started.tick;
                        note.offTick = std::max(started.tick + 1, tick);
                        note.note = d1;
                        note.velocity = started.vel;
                        note.channel = started.channel;
                        parsed.notes.push_back(note);
                    }
                }
            }
        }
        pos = end;
    }
    for (int pitch = 0; pitch < 128; ++pitch) {
        for (const auto& started : open[pitch]) {
            ParsedNote note;
            note.onTick = started.tick;
            note.offTick = started.tick + parsed.ppq;
            note.note = pitch;
            note.velocity = started.vel;
            note.channel = started.channel;
            parsed.notes.push_back(note);
        }
    }
    std::sort(parsed.notes.begin(), parsed.notes.end(), [](const ParsedNote& a, const ParsedNote& b) {
        if (a.onTick != b.onTick)
            return a.onTick < b.onTick;
        return a.note < b.note;
    });
    parsed.ok = true;
    return parsed;
}

} // namespace phrasearp
