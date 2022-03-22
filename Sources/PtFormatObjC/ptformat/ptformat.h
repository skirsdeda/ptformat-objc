/*
 * libptformat - a library to read ProTools sessions
 *
 * Copyright (C) 2015-2019  Damien Zammit
 * Copyright (C) 2015-2019  Robin Gareus
 * Copyright (C) 2021-      Tadas Dailyda
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef PTFFORMAT_H
#define PTFFORMAT_H

#include <string>
#include <cstring>
#include <algorithm>
#include <vector>
#include <stdint.h>
#include "ptformat/visibility.h"

class LIBPTFORMAT_API PTFFormat {
public:
    PTFFormat();
    ~PTFFormat();

    /* Return values:
         0    success
        -1    error decrypting pt session
        -2    error detecting pt session
        -3    incompatible pt version
        -4    error parsing header
        -5    error parsing session rate
        -6    error parsing audio
        -7    error parsing region/track info
        -8    error parsing midi
        -9    error parsing metadata
        -10   error parsing key signatures
        -11   error parsing time signatures
        -12   error parsing tempo changes
    */
    int load(std::string const& path);

    /* Return values:
         0    success
        -1    error decrypting pt session
    */
    int unxor(std::string const& path);
    
    struct block_t {
        uint16_t block_type;        // type of block
        uint32_t block_size;        // size of block
        uint16_t content_type;      // type of content
        uint32_t offset;            // offset in file
        std::vector<block_t> child; // vector of child blocks
    };

    struct wav_t {
        std::string filename;
        uint16_t    index;

        int64_t     posabsolute;
        uint64_t    length;

        bool operator <(const struct wav_t& other) const {
            return (strcasecmp(this->filename.c_str(),
                    other.filename.c_str()) < 0);
        }

        bool operator ==(const struct wav_t& other) const {
            return (this->filename == other.filename ||
                this->index == other.index);
        }

        wav_t (uint16_t idx = 0) : index (idx), posabsolute (0), length (0) {}
    };

    struct midi_ev_t {
        uint64_t pos;
        uint64_t length;
        uint8_t note;
        uint8_t velocity;
        midi_ev_t () : pos (0), length (0), note (0), velocity (0) {}
    };

    struct region_t {
        std::string name;
        uint16_t    index;
        bool        is_startpos_in_ticks; // MIDI timebase if true, samples timebase otherwise
        uint64_t    startpos;
        int64_t     offset;
        // ! 1. For Audio clips, length will be always in samples
        // ! 2. For Audio clips, length can be incorrect when clip is covered by next clip. A correction has to be done, limiting length to start of the next clip!
        // ! 3. For MIDI clips, length will be either in ticks or samples (depending on is_startpos_in_ticks)
        uint64_t    length;
        wav_t       wave;
        std::vector<midi_ev_t> midi;

        bool operator ==(const region_t& other) const {
            return (this->index == other.index);
        }

        bool operator <(const region_t& other) const {
            return (strcasecmp(this->name.c_str(),
                    other.name.c_str()) < 0);
        }
        region_t (uint16_t idx = 0) : index (idx), is_startpos_in_ticks (false), startpos (0), offset (0), length (0) {}
    };

    struct region_range_t {
        // both positions in samples
        uint64_t startpos;
        uint64_t endpos;

        bool operator <(const region_range_t& other) const {
            return this->startpos < other.startpos;
        }
    };

    struct track_t {
        std::string name;
        uint16_t    index;
        uint8_t     playlist;
        region_t    reg;

        bool operator <(const track_t& other) const {
            return (this->index < other.index);
        }

        bool operator ==(const track_t& other) const {
            return (this->index == other.index);
        }
        track_t (uint16_t idx = 0) : index (idx), playlist (0) {}
    };

    struct metadata_t {
        std::string title;
        std::string artist;
        std::vector<std::string> contributors;
        std::string location;
    };

    /** MIDI POSITION (key_signature_t.pos, time_signature_t.pos, tempo_change_t.pos)
        is encoded as PPQN (or ticks) since session start.
        960,000 PPQN resolution is being used, so if we have 4/4 time signature, then second measure (2|1|000)
        is 960,000 * 4 * 1 = 3,840,000
     */

    struct key_signature_t {
        bool     is_major; // otherwise - minor
        bool     is_sharp; // otherwise - flat
        uint8_t  sign_count; // how many alteration signs

        bool operator==(const key_signature_t &other) const {
            return (is_major == other.is_major && is_sharp == other.is_sharp &&
                    sign_count == other.sign_count);
        }
    };

    struct key_signature_ev_t : key_signature_t {
        uint64_t pos; // 8b

        key_signature_ev_t(uint64_t pos, bool is_major, bool is_sharp, uint8_t sign_count): pos(pos) {
            this->is_major = is_major;
            this->is_sharp = is_sharp;
            this->sign_count = sign_count;
        }

        const key_signature_t event_value() const { return static_cast<const key_signature_t>(*this); }
    };

    struct time_signature_t {
        uint8_t nominator; // actual range: 1-99 (takes up 4b in file)
        uint8_t denominator; // possible values 1/2/4/8/16/32/64 (takes up 4b in file)

        bool operator==(const time_signature_t &other) const {
            return (nominator == other.nominator && denominator == other.denominator);
        }
    };

    struct time_signature_ev_t : time_signature_t {
        uint64_t pos; // 8b
        uint32_t measure_num; //4b
        // 1 event: 8b + 4b*3 + 16b (pad, seems to be constant) = 36b

        time_signature_ev_t(uint64_t pos, uint32_t measure_num, uint8_t nom, uint8_t denom): pos(pos), measure_num(measure_num) {
            this->nominator = nom;
            this->denominator = denom;
        }

        const time_signature_t event_value() const { return static_cast<const time_signature_t>(*this); }
    };

    struct tempo_change_t {
        uint64_t pos; // 8b
        uint64_t pos_in_samples; // - derived, not in file
        double tempo; // 8b
        uint64_t beat_len; // 3b used at most, sixteenth note being the shortest possible at 240,000 as decimal
                           // which coincides with 960,000 PPQN being used in Pro Tools
        // 1 event: 4b pad (00x4) + "Const" (5b) + 6b pad (01002e000000) + “TMS” (3b) + 16b pad (010014(00x4)(01|00)(00x8)) +
        //          8b POSITION + 2b pad (00(01|00)) + 8b TEMPO (double) + 8b BEAT LENGTH + 1b pad (0x00) = 61b

        const double event_value() const { return tempo; }
    };

    bool find_track(uint16_t index, track_t& tt) const {
        std::vector<track_t>::const_iterator begin = _tracks.begin();
        std::vector<track_t>::const_iterator finish = _tracks.end();
        std::vector<track_t>::const_iterator found;

        track_t t (index);

        if ((found = std::find(begin, finish, t)) != finish) {
            tt = *found;
            return true;
        }
        return false;
    }

    bool find_region(uint16_t index, region_t& rr) const {
        std::vector<region_t>::const_iterator begin = _regions.begin();
        std::vector<region_t>::const_iterator finish = _regions.end();
        std::vector<region_t>::const_iterator found;

        region_t r;
        r.index = index;

        if ((found = std::find(begin, finish, r)) != finish) {
            rr = *found;
            return true;
        }
        return false;
    }
    
    bool find_miditrack(uint16_t index, track_t& tt) const {
        std::vector<track_t>::const_iterator begin = _miditracks.begin();
        std::vector<track_t>::const_iterator finish = _miditracks.end();
        std::vector<track_t>::const_iterator found;

        track_t t (index);

        if ((found = std::find(begin, finish, t)) != finish) {
            tt = *found;
            return true;
        }
        return false;
    }

    bool find_midiregion(uint16_t index, region_t& rr) const {
        std::vector<region_t>::const_iterator begin = _midiregions.begin();
        std::vector<region_t>::const_iterator finish = _midiregions.end();
        std::vector<region_t>::const_iterator found;

        region_t r (index);

        if ((found = std::find(begin, finish, r)) != finish) {
            rr = *found;
            return true;
        }
        return false;
    }

    bool find_wav(uint16_t index, wav_t& ww) const {
        std::vector<wav_t>::const_iterator begin = _audiofiles.begin();
        std::vector<wav_t>::const_iterator finish = _audiofiles.end();
        std::vector<wav_t>::const_iterator found;

        wav_t w (index);

        if ((found = std::find(begin, finish, w)) != finish) {
            ww = *found;
            return true;
        }
        return false;
    }

    static bool regionexistsin(std::vector<region_t> const& reg, uint16_t index) {
        std::vector<region_t>::const_iterator begin = reg.begin();
        std::vector<region_t>::const_iterator finish = reg.end();

        region_t r (index);

        if (std::find(begin, finish, r) != finish) {
            return true;
        }
        return false;
    }

    static bool wavexistsin (std::vector<wav_t> const& wv, uint16_t index) {
        std::vector<wav_t>::const_iterator begin = wv.begin();
        std::vector<wav_t>::const_iterator finish = wv.end();

        wav_t w (index);

        if (std::find(begin, finish, w) != finish) {
            return true;
        }
        return false;
    }

    std::vector<block_t> blocks () const { return _blocks; }
    uint8_t version () const { return _version; }
    int64_t sessionrate () const { return _sessionrate; }
    uint8_t bitdepth () const { return _bitdepth; }
    const std::string& path () { return _path; }

    const std::vector<wav_t>&    audiofiles () const { return _audiofiles ; }
    const std::vector<region_t>& regions () const { return _regions ; }
    const std::vector<region_t>& midiregions () const { return _midiregions ; }
    const std::vector<region_range_t>& region_ranges(void);
    const std::vector<track_t>&  tracks () const { return _tracks ; }
    const std::vector<track_t>&  miditracks () const { return _miditracks ; }
    const std::vector<key_signature_ev_t>& keysignatures () const { return _keysignatures ; }
    const std::vector<time_signature_ev_t>& timesignatures () const { return _timesignatures; }
    const std::vector<tempo_change_t>& tempochanges () const { return _tempochanges; }

    const key_signature_t main_keysignature();
    const time_signature_t main_timesignature();
    const double main_tempo();
    const uint32_t music_duration_secs(uint8_t max_gap_secs);

    const unsigned char* unxored_data () const { return _ptfunxored; }
    uint64_t             unxored_size () const { return _len; }

    const unsigned char* metadata_base64 () const { return _session_meta_base64; }
    uint32_t             metadata_base64_size () const { return _session_meta_base64_size; }
    const metadata_t&    metadata () const { return _session_meta_parsed; }

private:

    std::vector<wav_t>    _audiofiles;
    std::vector<region_t> _regions;
    std::vector<region_t> _midiregions;
    std::vector<track_t>  _tracks;
    std::vector<track_t>  _miditracks;
    std::vector<key_signature_ev_t> _keysignatures;
    std::vector<time_signature_ev_t> _timesignatures;
    std::vector<tempo_change_t> _tempochanges;
    unsigned char* _session_meta_base64;
    uint32_t _session_meta_base64_size;
    metadata_t _session_meta_parsed;
    bool _region_ranges_cached;
    std::vector<region_range_t> _region_ranges;

    std::string _path;

    unsigned char* _ptfunxored;
    uint64_t       _len;
    int64_t        _sessionrate;
    uint8_t        _bitdepth;
    uint8_t        _version;
    uint8_t*       _product;
    bool           is_bigendian;

    std::vector<block_t> _blocks;

    bool jumpback(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen);
    bool jumpto(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen);
    bool foundin(std::string const& haystack, std::string const& needle);
    int64_t foundat(unsigned char *haystack, uint64_t n, const char *needle);

    std::string parsestring(uint32_t pos);
    int parse(void);
    void parseblocks(void);
    bool parseheader(void);
    bool parserest(void);
    bool parseaudio(void);
    bool parsemidi(void);
    bool parsemetadata(void);
    bool parsemetadata_base64(block_t& blk);
    uint32_t parsemetadata_struct(unsigned char* base64_data, uint32_t size, std::string const* outer_field);
    void fill_metadata_field(std::string const& field, std::string const& value);
    bool parsekeysigs(void);
    bool parsekeysig(block_t& blk);
    bool parsetimesigs(void);
    bool parsetimesigs_block(block_t& blk);
    bool parsetempochanges(void);
    bool parsetempochanges_block(block_t& blk);
    void dump(void);
    bool parse_block_at(uint32_t pos, struct block_t *b, struct block_t *parent, int level);
    void dump_block(struct block_t& b, int level);
    bool parse_version();
    void parse_region_info(uint32_t j, block_t& blk, region_t& r);
    void parse_three_point(uint32_t j, int64_t& start, uint64_t& offset, uint64_t& length);
    uint8_t gen_xor_delta(uint8_t xor_value, uint8_t mul, bool negative);
    void cleanup(void);
    void free_block(struct block_t& b);
    void free_all_blocks(void);
    uint64_t ticks_to_samples(uint64_t pos_in_ticks) const;
    uint64_t ticks_to_samples(uint64_t pos_in_ticks, const tempo_change_t& t) const;
    template <class EV, class EV_VAL> const EV_VAL find_main_event_value(const std::vector<EV> &events, std::function<const uint64_t(const EV&)> ev_pos_in_samples);
    void add_region_ranges_from_tracks(const std::vector<track_t> &tracks);
};

template<>
struct std::hash<PTFFormat::key_signature_t> {
    std::size_t operator()(const PTFFormat::key_signature_t &k) const noexcept {
        size_t ret = 17;
        ret = ret * 31 + std::hash<bool>{}(k.is_major);
        ret = ret * 31 + std::hash<bool>{}(k.is_sharp);
        ret = ret * 31 + std::hash<uint8_t>{}(k.sign_count);
        return ret;
    }
};

template<>
struct std::hash<PTFFormat::time_signature_t> {
    std::size_t operator()(const PTFFormat::time_signature_t &t) const noexcept {
        size_t ret = 17;
        ret = ret * 31 + std::hash<uint8_t>{}(t.nominator);
        ret = ret * 31 + std::hash<uint8_t>{}(t.denominator);
        return ret;
    }
};

#endif
