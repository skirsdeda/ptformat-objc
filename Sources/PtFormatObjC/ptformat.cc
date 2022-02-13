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

#include <regex>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <assert.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>

#ifdef HAVE_GLIB
# include <glib/gstdio.h>
# define ptf_open	g_fopen
#else
# define ptf_open	fopen
#endif

#include "ptformat/ptformat.h"

#define BITCODE			"0010111100101011"
#define ZMARK			'\x5a'
// ZERO_TICKS - start position for all MIDI events is 1,000,000,000,000 (1 trillion)
#define ZERO_TICKS		0xe8d4a51000ULL
#define MAX_CONTENT_TYPE	0x3000
#define MAX_CHANNELS_PER_TRACK	8
#define THIRTY_SECOND   120000
#define QUARTER         960000

using namespace std;

PTFFormat::PTFFormat()
    : _ptfunxored(0)
    , _len(0)
    , _sessionrate(0)
    , _version(0)
    , _product(NULL)
    , _session_meta_base64(NULL)
    , is_bigendian(false)
    , _region_ranges_cached(false)
{
}

PTFFormat::~PTFFormat() {
    cleanup();
}

/**
 Reads n bytes into unsigned integer type T swapping bytes for endianness if necessary.
 */
template <class T>
static T
u_endian_read(unsigned char *buf, bool bigendian, int n) {
    int bit_shift_start = bigendian ? (n - 1) * 8 : 0;
    int bit_shift_change = bigendian ? -8 : 8;

    T ret = 0;
    for (int in_i = 0, bit_shift = bit_shift_start; in_i < n; in_i++, bit_shift += bit_shift_change) {
        ret |= (T)buf[in_i] << bit_shift;
    }
    return ret;
}

template <class T, int n = sizeof(T)>
static T
u_endian_read(unsigned char *buf, bool bigendian) {
    static_assert(n <= sizeof(T), "Cannot read more bytes than sizeof(T)");

    return u_endian_read<T>(buf, bigendian, n);
}

static uint16_t
u_endian_read2(unsigned char *buf, bool bigendian) {
    return u_endian_read<uint16_t>(buf, bigendian);
}

static uint32_t
u_endian_read4(unsigned char *buf, bool bigendian) {
    return u_endian_read<uint32_t>(buf, bigendian);
}

static uint64_t
u_endian_read5(unsigned char *buf, bool bigendian) {
    return u_endian_read<uint64_t, 5>(buf, bigendian);
}

static uint64_t
u_endian_read6(unsigned char *buf, bool bigendian) {
    return u_endian_read<uint64_t, 6>(buf, bigendian);
}

static uint64_t
u_endian_read8(unsigned char *buf, bool bigendian) {
    return u_endian_read<uint64_t>(buf, bigendian);
}

void
PTFFormat::cleanup(void) {
    _len = 0;
    _sessionrate = 0;
    _bitdepth = 0;
    _version = 0;
    free(_ptfunxored);
    _ptfunxored = NULL;
    free (_product);
    _product = NULL;
    free(_session_meta_base64);
    _session_meta_base64 = NULL;
    _session_meta_parsed = {};
    _audiofiles.clear();
    _regions.clear();
    _midiregions.clear();
    _tracks.clear();
    _miditracks.clear();
    _keysignatures.clear();
    _timesignatures.clear();
    _tempochanges.clear();
    free_all_blocks();
    _region_ranges_cached = false;
    _region_ranges.clear();
}

int64_t
PTFFormat::foundat(unsigned char *haystack, uint64_t n, const char *needle) {
    int64_t found = 0;
    uint64_t i, j, needle_n;
    needle_n = strlen(needle);

    for (i = 0; i < n; i++) {
        found = i;
        for (j = 0; j < needle_n; j++) {
            if (haystack[i+j] != needle[j]) {
                found = -1;
                break;
            }
        }
        if (found > 0)
            return found;
    }
    return -1;
}

bool
PTFFormat::jumpto(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen) {
    uint64_t i;
    uint64_t k = *currpos;
    while (k + needlelen < maxoffset) {
        bool foundall = true;
        for (i = 0; i < needlelen; i++) {
            if (buf[k+i] != needle[i]) {
                foundall = false;
                break;
            }
        }
        if (foundall) {
            *currpos = k;
            return true;
        }
        k++;
    }
    return false;
}

bool
PTFFormat::jumpback(uint32_t *currpos, unsigned char *buf, const uint32_t maxoffset, const unsigned char *needle, const uint32_t needlelen) {
    uint64_t i;
    uint64_t k = *currpos;
    while (k > 0 && k + needlelen < maxoffset) {
        bool foundall = true;
        for (i = 0; i < needlelen; i++) {
            if (buf[k+i] != needle[i]) {
                foundall = false;
                break;
            }
        }
        if (foundall) {
            *currpos = k;
            return true;
        }
        k--;
    }
    return false;
}

bool
PTFFormat::foundin(std::string const& haystack, std::string const& needle) {
    return haystack.find(needle) != std::string::npos;
}

/* Return values:
     0    success
    -1    error decrypting pt session
*/
int
PTFFormat::unxor(std::string const& path) {
    FILE *fp;
    unsigned char xxor[256];
    unsigned char ct;
    uint64_t i;
    uint8_t xor_type;
    uint8_t xor_value;
    uint8_t xor_delta;
    uint16_t xor_len;

    if (! (fp = ptf_open(path.c_str(), "rb"))) {
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    _len = ftell(fp);
    if (_len < 0x14) {
        fclose(fp);
        return -1;
    }

    if (! (_ptfunxored = (unsigned char*) malloc(_len * sizeof(unsigned char)))) {
        /* Silently fail -- out of memory*/
        fclose(fp);
        _ptfunxored = 0;
        return -1;
    }

    /* The first 20 bytes are always unencrypted */
    fseek(fp, 0x00, SEEK_SET);
    i = fread(_ptfunxored, 1, 0x14, fp);
    if (i < 0x14) {
        fclose(fp);
        return -1;
    }

    xor_type = _ptfunxored[0x12];
    xor_value = _ptfunxored[0x13];
    xor_len = 256;

    // xor_type 0x01 = ProTools 5, 6, 7, 8 and 9
    // xor_type 0x05 = ProTools 10, 11, 12
    switch(xor_type) {
    case 0x01:
        xor_delta = gen_xor_delta(xor_value, 53, false);
        break;
    case 0x05:
        xor_delta = gen_xor_delta(xor_value, 11, true);
        break;
    default:
        fclose(fp);
        return -1;
    }

    /* Generate the xor_key */
    for (i=0; i < xor_len; i++)
        xxor[i] = (i * xor_delta) & 0xff;

    /* Read file and decrypt rest of file */
    i = 0x14;
    fseek(fp, i, SEEK_SET);
    while (fread(&ct, 1, 1, fp) != 0) {
        uint8_t xor_index = (xor_type == 0x01) ? i & 0xff : (i >> 12) & 0xff;
        _ptfunxored[i++] = ct ^ xxor[xor_index];
    }
    fclose(fp);
    return 0;
}

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
int
PTFFormat::load(std::string const& ptf) {
    cleanup();
    _path = ptf;

    if (unxor(_path))
        return -1;

    if (parse_version())
        return -2;

    if (_version < 5 || _version > 12)
        return -3;

    int err = 0;
    if ((err = parse())) {
        return err - 3; // -4, -5, -6, -7, -8, ...
    }

    return 0;
}

bool
PTFFormat::parse_version() {
    bool failed = true;
    struct block_t b;

    if (_ptfunxored[0] != '\x03' && foundat(_ptfunxored, 0x100, BITCODE) != 1) {
        return failed;
    }

    is_bigendian = !!_ptfunxored[0x11];

    if (!parse_block_at(0x1f, &b, NULL, 0)) {
        _version = _ptfunxored[0x40];
        if (_version == 0) {
            _version = _ptfunxored[0x3d];
        }
        if (_version == 0) {
            _version = _ptfunxored[0x3a] + 2;
        }
        if (_version != 0)
            failed = false;
        return failed;
    } else {
        if (b.content_type == 0x0003) {
            // old
            uint16_t skip = parsestring(b.offset + 3).size() + 8;
            _version = u_endian_read4(&_ptfunxored[b.offset + 3 + skip], is_bigendian);
            failed = false;
        } else if (b.content_type == 0x2067) {
            // new
            _version = 2 + u_endian_read4(&_ptfunxored[b.offset + 20], is_bigendian);
            failed = false;
        }
        return failed;
    }
}

uint8_t
PTFFormat::gen_xor_delta(uint8_t xor_value, uint8_t mul, bool negative) {
    uint16_t i;
    for (i = 0; i < 256; i++) {
        if (((i * mul) & 0xff) == xor_value) {
                return (negative) ? i * (-1) : i;
        }
    }
    // Should not occur
    return 0;
}

bool
PTFFormat::parse_block_at(uint32_t pos, struct block_t *block, struct block_t *parent, int level) {
    struct block_t b;
    int childjump = 0;
    uint32_t i;
    uint32_t max = _len;

    if (_ptfunxored[pos] != ZMARK)
        return false;

    if (parent)
        max = parent->block_size + parent->offset;

    b.block_type = u_endian_read2(&_ptfunxored[pos+1], is_bigendian);
    b.block_size = u_endian_read4(&_ptfunxored[pos+3], is_bigendian);
    b.content_type = u_endian_read2(&_ptfunxored[pos+7], is_bigendian);
    b.offset = pos + 7;

    if (b.block_size + b.offset > max)
        return false;
    if (b.block_type & 0xff00)
        return false;

    block->block_type = b.block_type;
    block->block_size = b.block_size;
    block->content_type = b.content_type;
    block->offset = b.offset;
    block->child.clear();

    for (i = 1; (i < block->block_size) && (pos + i + childjump < max); i += childjump ? childjump : 1) {
        int p = pos + i;
        struct block_t bchild;
        childjump = 0;
        if (parse_block_at(p, &bchild, block, level+1)) {
            block->child.push_back(bchild);
            childjump = bchild.block_size + 7;
        }
    }
    return true;
}

void
PTFFormat::free_block(struct block_t& b)
{
    for (vector<PTFFormat::block_t>::iterator c = b.child.begin();
            c != b.child.end(); ++c) {
        free_block(*c);
    }

    b.child.clear();
}

void
PTFFormat::free_all_blocks(void)
{
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        free_block(*b);
    }

    _blocks.clear();
}

void
PTFFormat::parseblocks(void) {
    uint32_t i = 20;

    while (i < _len) {
        struct block_t b;
        if (parse_block_at(i, &b, NULL, 0)) {
            _blocks.push_back(b);
        }
        i += b.block_size ? b.block_size + 7 : 1;
    }
}

int
PTFFormat::parse(void) {
    parseblocks();
    if (!parseheader())
        return -1;
    if (_sessionrate < 44100 || _sessionrate > 192000)
        return -2;
    if (!parseaudio())
        return -3;
    if (!parserest())
        return -4;
    if (!parsemidi())
        return -5;
    if (!parsemetadata())
        return -6;
    if (!parsekeysigs())
        return -7;
    if (!parsetimesigs())
        return -8;
    if (!parsetempochanges())
        return -9;
    return 0;
}

bool
PTFFormat::parseheader(void) {
    bool found = false;
    uint8_t bitdepthotherblk = 0;

    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x1028) {
            _bitdepth = _ptfunxored[b->offset+3];
            _sessionrate = u_endian_read4(&_ptfunxored[b->offset+4], is_bigendian);
            found = true;
        } else if (b->content_type == 0x204b) {
            // Seems to be available in all versions of format and works not only for 16 / 24 bits
            // but also for 32bit(float) - reported as 24bit in sample rate info block
            bitdepthotherblk = _ptfunxored[b->offset+6];
        }
    }
    if (bitdepthotherblk != 0) {
        _bitdepth = bitdepthotherblk;
    }
    return found;
}

std::string
PTFFormat::parsestring (uint32_t pos) {
    uint32_t length = u_endian_read4(&_ptfunxored[pos], is_bigendian);
    pos += 4;
    return std::string((const char *)&_ptfunxored[pos], length);
}

bool
PTFFormat::parseaudio(void) {
    bool found = false;
    uint32_t nwavs = 0;
    uint32_t i, n;
    uint32_t pos = 0;
    std::string wavtype;
    std::string wavname;

    // Parse wav names
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x1004) {

            nwavs = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);

            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x103a) {
                    //nstrings = u_endian_read4(&_ptfunxored[c->offset+1], is_bigendian);
                    pos = c->offset + 11;
                    // Found wav list
                    for (i = n = 0; (pos < c->offset + c->block_size) && (n < nwavs); i++) {
                        wavname = parsestring(pos);
                        pos += wavname.size() + 4;
                        wavtype = std::string((const char*)&_ptfunxored[pos], 4);
                        pos += 9;
                        if (foundin(wavname, std::string(".grp")))
                            continue;

                        if (foundin(wavname, std::string("Audio Files"))) {
                            continue;
                        }
                        if (foundin(wavname, std::string("Fade Files"))) {
                            continue;
                        }
                        if (_version < 10) {
                            if (!(foundin(wavtype, std::string("WAVE")) ||
                                    foundin(wavtype, std::string("EVAW")) ||
                                    foundin(wavtype, std::string("AIFF")) ||
                                    foundin(wavtype, std::string("FFIA"))) ) {
                                continue;
                            }
                        } else {
                            if (wavtype[0] != '\0') {
                                if (!(foundin(wavtype, std::string("WAVE")) ||
                                        foundin(wavtype, std::string("EVAW")) ||
                                        foundin(wavtype, std::string("AIFF")) ||
                                        foundin(wavtype, std::string("FFIA"))) ) {
                                    continue;
                                }
                            } else if (!(foundin(wavname, std::string(".wav")) || 
                                    foundin(wavname, std::string(".aif"))) ) {
                                continue;
                            }
                        }
                        found = true;
                        wav_t f (n);
                        f.filename = wavname;
                        n++;
                        _audiofiles.push_back(f);
                    }
                }
            }
        }
    }

    if (!found) {
        if (nwavs > 0) {
            return false;
        } else {
            return true;
        }
    }

    // Add wav length information
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x1004) {

            vector<PTFFormat::wav_t>::iterator wav = _audiofiles.begin();

            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x1003) {
                    for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
                            d != c->child.end(); ++d) {
                        if (d->content_type == 0x1001) {
                            (*wav).length = u_endian_read8(&_ptfunxored[d->offset+8], is_bigendian);
                            wav++;
                        }
                    }
                }
            }
        }
    }

    return found;
}


void
PTFFormat::parse_three_point(uint32_t j, uint64_t& start, uint64_t& offset, uint64_t& length) {
    uint32_t indexes[] = {1, 2, 3, 4}; // offset b, length b, start b, skip b (if !is_big_endian, otherwise - reversed)
    if (is_bigendian) {
        std::reverse(indexes, indexes + 4);
    }
    uint8_t offsetbytes = _ptfunxored[j + indexes[0]] >> 4;
    uint8_t lengthbytes = _ptfunxored[j + indexes[1]] >> 4;
    uint8_t startbytes = _ptfunxored[j + indexes[2]] >> 4;

    offset = u_endian_read<uint64_t>(&_ptfunxored[j+5], false, offsetbytes);
    j += offsetbytes;
    length = u_endian_read<uint64_t>(&_ptfunxored[j+5], false, lengthbytes);
    j += lengthbytes;
    start = u_endian_read<uint64_t>(&_ptfunxored[j+5], false, lengthbytes);
}

void
PTFFormat::parse_region_info(uint32_t j, block_t& blk, region_t& r) {
    uint64_t findex, start, sampleoffset, length;

    parse_three_point(j, start, sampleoffset, length);

    findex = u_endian_read4(&_ptfunxored[blk.offset + blk.block_size], is_bigendian);
    wav_t f (findex);
    f.posabsolute = start;
    f.length = length;

    wav_t found;
    if (find_wav(findex, found)) {
        f.filename = found.filename;
    }

    std::vector<midi_ev_t> m;
    r.is_startpos_in_ticks = start > ZERO_TICKS;
    r.startpos = r.is_startpos_in_ticks ? start - ZERO_TICKS : start;
    r.sampleoffset = sampleoffset;
    r.length = length;
    r.wave = f;
    r.midi = m;
}

bool
PTFFormat::parserest(void) {
    uint32_t i, j, count;
    uint64_t start;
    uint16_t rindex, rawindex, tindex, mindex;
    uint32_t nch;
    uint16_t ch_map[MAX_CHANNELS_PER_TRACK];
    bool found = false;
    bool region_is_fade = false;
    std::string regionname, trackname, midiregionname;
    rindex = 0;

    // Parse sources->regions
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x100b || b->content_type == 0x262a) {
            //nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x1008 || c->content_type == 0x2629) {
                    vector<PTFFormat::block_t>::iterator d = c->child.begin();
                    region_t r;

                    // FIXME: this is actually always parsing child block (0x2628)
                    //        and duplicates code which is parsing 0x2628 (at least in .ptx files)
                    found = true;
                    j = c->offset + 11;
                    regionname = parsestring(j);
                    j += regionname.size() + 4;

                    r.name = regionname;
                    r.index = rindex;
                    parse_region_info(j, *d, r);

                    _regions.push_back(r);
                    rindex++;
                }
            }
            found = true;
        }
    }

    // Parse tracks
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x1015) {
            //ntracks = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x1014) {
                    j = c->offset + 2;
                    trackname = parsestring(j);
                    j += trackname.size() + 5;
                    nch = u_endian_read4(&_ptfunxored[j], is_bigendian);
                    j += 4;
                    for (i = 0; i < nch; i++) {
                        ch_map[i] = u_endian_read2(&_ptfunxored[j], is_bigendian);

                        track_t ti;
                        if (!find_track(ch_map[i], ti)) {
                            // Add a dummy region for now
                            region_t r (65535);
                            track_t t (ch_map[i]);
                            t.name = trackname;
                            t.reg = r;
                            _tracks.push_back(t);
                        }
                        j += 2;
                    }
                }
            }
        }
    }

    // Reparse from scratch to exclude audio tracks from all tracks to get midi tracks
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x2519) {
            tindex = 0;
            mindex = 0;
            //ntracks = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x251a) {
                    j = c->offset + 4;
                    trackname = parsestring(j);
                    j += trackname.size() + 4 + 18;
                    //tindex = u_endian_read4(&_ptfunxored[j], is_bigendian);

                    // Add a dummy region for now
                    region_t r (65535);
                    track_t t (mindex);
                    t.name = trackname;
                    t.reg = r;

                    track_t ti;
                    // If the current track is not an audio track, insert as midi track
                    if (!(find_track(tindex, ti) && foundin(trackname, ti.name))) {
                        _miditracks.push_back(t);
                        mindex++;
                    }
                    tindex++;
                }
            }
        }
    }

    // Parse regions->tracks
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        tindex = 0;
        if (b->content_type == 0x1012) {
            //nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
            count = 0;
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x1011) {
                    regionname = parsestring(c->offset + 2);
                    for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
                            d != c->child.end(); ++d) {
                        if (d->content_type == 0x100f) {
                            for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
                                    e != d->child.end(); ++e) {
                                if (e->content_type == 0x100e) {
                                    // Region->track
                                    track_t ti;
                                    j = e->offset + 4;
                                    rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
                                    if (!find_track(count, ti))
                                        continue;
                                    if (!find_region(rawindex, ti.reg))
                                        continue;
                                    if (ti.reg.index != 65535) {
                                        _tracks.push_back(ti);
                                    }
                                }
                            }
                        }
                    }
                    found = true;
                    count++;
                }
            }
        } else if (b->content_type == 0x1054) {
            //nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
            count = 0;
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x1052) {
                    trackname = parsestring(c->offset + 2);
                    for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
                            d != c->child.end(); ++d) {
                        if (d->content_type == 0x1050) {
                            region_is_fade = (_ptfunxored[d->offset + 46] == 0x01);
                            if (region_is_fade) {
                                continue;
                            }
                            for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
                                    e != d->child.end(); ++e) {
                                if (e->content_type == 0x104f) {
                                    // Region->track
                                    j = e->offset + 4;
                                    rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
                                    j += 4 + 1;
                                    start = u_endian_read6(&_ptfunxored[j], is_bigendian);
                                    tindex = count;
                                    track_t ti;
                                    if (!find_track(tindex, ti) || !find_region(rawindex, ti.reg)) {
                                        continue;
                                    }
                                    ti.reg.is_startpos_in_ticks = start > ZERO_TICKS;
                                    ti.reg.startpos = ti.reg.is_startpos_in_ticks ? start - ZERO_TICKS : start;
                                    if (ti.reg.index != 65535) {
                                        _tracks.push_back(ti);
                                    }
                                }
                            }
                        }
                    }
                    found = true;
                    count++;
                }
            }
        }
    }
    for (std::vector<track_t>::iterator tr = _tracks.begin();
            tr != _tracks.end(); /* noop */) {
        if ((*tr).reg.index == 65535) {
            tr = _tracks.erase(tr);
        } else {
            tr++;
        }
    }
    return found;
}

struct mchunk {
    mchunk (uint64_t zt, uint64_t ml, std::vector<PTFFormat::midi_ev_t> const& c)
    : zero (zt)
    , maxlen (ml)
    , chunk (c)
    {}
    uint64_t zero;
    uint64_t maxlen;
    std::vector<PTFFormat::midi_ev_t> chunk;
};

bool
PTFFormat::parsemidi(void) {
    uint32_t i, j, k, n, rindex, tindex, mindex, count, rawindex;
    uint64_t n_midi_events, zero_ticks, start, offset, length, start2, stop2;
    uint64_t midi_pos, midi_len, max_pos, region_pos;
    uint8_t midi_velocity, midi_note;
    uint16_t regionnumber = 0;
    std::string midiregionname;

    std::vector<mchunk> midichunks;
    midi_ev_t m;

    std::string regionname, trackname;
    rindex = 0;

    // Parse MIDI events
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x2000) {

            k = b->offset;

            // Parse all midi chunks, not 1:1 mapping to regions yet
            while (k + 35 < b->block_size + b->offset) {
                max_pos = 0;
                std::vector<midi_ev_t> midi;

                if (!jumpto(&k, _ptfunxored, _len, (const unsigned char *)"MdNLB", 5)) {
                    break;
                }
                k += 11;
                n_midi_events = u_endian_read4(&_ptfunxored[k], is_bigendian);

                k += 4;
                zero_ticks = u_endian_read5(&_ptfunxored[k], is_bigendian);
                for (i = 0; i < n_midi_events && k < _len; i++, k += 35) {
                    midi_pos = u_endian_read5(&_ptfunxored[k], is_bigendian);
                    midi_pos -= zero_ticks;
                    midi_note = _ptfunxored[k+8];
                    midi_len = u_endian_read5(&_ptfunxored[k+9], is_bigendian);
                    midi_velocity = _ptfunxored[k+17];

                    if (midi_pos + midi_len > max_pos) {
                        max_pos = midi_pos + midi_len;
                    }

                    m.pos = midi_pos;
                    m.length = midi_len;
                    m.note = midi_note;
                    m.velocity = midi_velocity;
                    midi.push_back(m);
                }
                midichunks.push_back(mchunk (zero_ticks, max_pos, midi));
            }

        // Put chunks onto regions
        } else if ((b->content_type == 0x2002) || (b->content_type == 0x2634)) {
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if ((c->content_type == 0x2001) || (c->content_type == 0x2633)) {
                    for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
                            d != c->child.end(); ++d) {
                        if ((d->content_type == 0x1007) || (d->content_type == 0x2628)) {
                            j = d->offset + 2;
                            midiregionname = parsestring(j);
                            j += 4 + midiregionname.size();
                            parse_three_point(j, region_pos, zero_ticks, midi_len);
                            j = d->offset + d->block_size;
                            rindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
                            struct mchunk mc = *(midichunks.begin()+rindex);

                            region_t r (regionnumber++);
                            r.name = midiregionname;
                            r.is_startpos_in_ticks = region_pos > ZERO_TICKS;
                            r.startpos = r.is_startpos_in_ticks ? region_pos - ZERO_TICKS : region_pos;
                            r.sampleoffset = 0;
                            r.length = mc.maxlen;
                            r.midi = mc.chunk;

                            _midiregions.push_back(r);
                        }
                    }
                }
            }
        } 
    }
    
    // COMPOUND MIDI regions
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x262c) {
            mindex = 0;
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x262b) {
                    for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
                            d != c->child.end(); ++d) {
                        if (d->content_type == 0x2628) {
                            count = 0;
                            j = d->offset + 2;
                            regionname = parsestring(j);
                            j += 4 + regionname.size();
                            parse_three_point(j, start, offset, length);
                            j = d->offset + d->block_size + 2;
                            n = u_endian_read2(&_ptfunxored[j], is_bigendian);

                            for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
                                    e != d->child.end(); ++e) {
                                if (e->content_type == 0x2523) {
                                    // FIXME Compound MIDI region
                                    j = e->offset + 39;
                                    rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
                                    j += 12; 
                                    start2 = u_endian_read5(&_ptfunxored[j], is_bigendian);
                                    int64_t signedval = (int64_t)start2;
                                    signedval -= ZERO_TICKS;
                                    if (signedval < 0) {
                                        signedval = -signedval;
                                    }
                                    start2 = signedval;
                                    j += 8;
                                    stop2 = u_endian_read5(&_ptfunxored[j], is_bigendian);
                                    signedval = (int64_t)stop2;
                                    signedval -= ZERO_TICKS;
                                    if (signedval < 0) {
                                        signedval = -signedval;
                                    }
                                    stop2 = signedval;
                                    j += 16;
                                    //nn = u_endian_read4(&_ptfunxored[j], is_bigendian);
                                    count++;
                                }
                            }
                            if (!count) {
                                // Plain MIDI region
                                struct mchunk mc = *(midichunks.begin()+n);

                                region_t r (n);
                                r.name = midiregionname;
                                r.is_startpos_in_ticks = true;
                                r.startpos = 0;
                                r.length = mc.maxlen;
                                r.midi = mc.chunk;
                                _midiregions.push_back(r);
                                mindex++;
                            }
                        }
                    }
                }
            }
        } 
    }
    
    // Put midi regions onto midi tracks
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x1058) {
            //nregions = u_endian_read4(&_ptfunxored[b->offset+2], is_bigendian);
            count = 0;
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin();
                    c != b->child.end(); ++c) {
                if (c->content_type == 0x1057) {
                    regionname = parsestring(c->offset + 2);
                    for (vector<PTFFormat::block_t>::iterator d = c->child.begin();
                            d != c->child.end(); ++d) {
                        if (d->content_type == 0x1056) {
                            for (vector<PTFFormat::block_t>::iterator e = d->child.begin();
                                    e != d->child.end(); ++e) {
                                if (e->content_type == 0x104f) {
                                    // MIDI region->MIDI track
                                    track_t ti;
                                    j = e->offset + 4;
                                    rawindex = u_endian_read4(&_ptfunxored[j], is_bigendian);
                                    j += 4 + 1;
                                    start = u_endian_read6(&_ptfunxored[j], is_bigendian);
                                    tindex = count;
                                    if (!find_miditrack(tindex, ti) || !find_midiregion(rawindex, ti.reg)) {
                                        continue;
                                    }

                                    ti.reg.is_startpos_in_ticks = start > ZERO_TICKS;
                                    ti.reg.startpos = ti.reg.is_startpos_in_ticks ? start - ZERO_TICKS : start;
                                    if (ti.reg.index != 65535) {
                                        _miditracks.push_back(ti);
                                    }
                                }
                            }
                        }
                    }
                    count++;
                }
            }
        }
    }
    for (std::vector<track_t>::iterator tr = _miditracks.begin();
            tr != _miditracks.end(); /* noop */) {
        if ((*tr).reg.index == 65535) {
            tr = _miditracks.erase(tr);
        } else {
            tr++;
        }
    }
    return true;
}

bool
PTFFormat::parsemetadata(void) {
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin();
            b != _blocks.end(); ++b) {
        if (b->content_type == 0x2716) {
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin(); c != b->child.end(); ++c) {
                if (c->content_type == 0x2715) {
                    if (parsemetadata_base64(*c)) {
                        return parsemetadata_struct(_session_meta_base64, _session_meta_base64_size, NULL) != 0;
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

bool
PTFFormat::parsemetadata_base64(block_t& blk) {
    static const std::string BASE64_CHARS =
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";
    static const int BASE64_GROUP_LEN = 64;
    static const int BASE64_GROUP_LEN_W_PAD = BASE64_GROUP_LEN + 2;
    static const int BYTES_IN = 4;
    static const int BYTES_OUT = 3;

    uint32_t pos = blk.offset + 2;
    std::string meta_header = parsestring(pos);
    if (!foundin(meta_header, std::string("sessionMetadataBase64"))) {
        return false;
    }
    pos += 4 + meta_header.size();
    // read base64 data length
    uint32_t length_with_pad = u_endian_read4(&_ptfunxored[pos], is_bigendian);
    pos += 4;
    // base64 data is layed out in groups of 64 bytes padded by 2 bytes in-between
    uint32_t whole_groups = length_with_pad / BASE64_GROUP_LEN_W_PAD;
    uint32_t last_group_len = length_with_pad % BASE64_GROUP_LEN_W_PAD;
    // last group length must be divisible by 4
    if (last_group_len % BYTES_IN != 0) {
        return false;
    }
    // expected decoded bytes length (might be shorter due to '=' padding at the end
    uint32_t decoded_len = (whole_groups * BASE64_GROUP_LEN + last_group_len) / BYTES_IN * BYTES_OUT;
    _session_meta_base64 = (unsigned char*) malloc(decoded_len * sizeof(unsigned char));
    unsigned char enc_bytes[BYTES_IN], output_bytes[BYTES_OUT];

    uint32_t end_pos = pos + length_with_pad;
    uint32_t output_pos = 0; // track bytes actually decoded for final length
    for (uint32_t p = pos; p < end_pos; p += BASE64_GROUP_LEN_W_PAD) {
        for (uint32_t i = p; i < min(p + BASE64_GROUP_LEN, end_pos); i += BYTES_IN) {
            int pad_found_at = BYTES_OUT + 1;
            for (int j = 0; j < BYTES_IN; j++) {
                unsigned char input_char = _ptfunxored[i + j];

                if (input_char != '=') {
                    enc_bytes[j] = BASE64_CHARS.find(input_char);
                } else {
                    enc_bytes[j] = 0;
                    pad_found_at = j;
                }
            }

            output_bytes[0] = (enc_bytes[0] << 2) + ((enc_bytes[1] & 0x30) >> 4);
            output_bytes[1] = ((enc_bytes[1] & 0xf) << 4) + ((enc_bytes[2] & 0x3c) >> 2);
            output_bytes[2] = ((enc_bytes[2] & 0x3) << 6) + enc_bytes[3];
            for (int o = 0; o < min(BYTES_OUT, pad_found_at); o++) {
                _session_meta_base64[output_pos] = output_bytes[o];
                output_pos++;
            }
        }
    }
    _session_meta_base64_size = output_pos;
    return true;
}

uint32_t
PTFFormat::parsemetadata_struct(unsigned char* base64_data_base, uint32_t size, std::string const* outer_field) {
    unsigned char* base64_data = base64_data_base;
    uint32_t struct_head = u_endian_read4(base64_data, is_bigendian); // CONSTANT (1)
    base64_data += 4;
    if (struct_head != 1) {
        return 0;
    }
    uint32_t field_count = u_endian_read4(base64_data, is_bigendian);
    base64_data += 4;
    for (int f = 0; f < field_count; f++) {
        uint32_t field_name_len = u_endian_read4(base64_data, is_bigendian);
        base64_data += 4;
        std::string field = std::regex_replace(std::string((const char *)base64_data, field_name_len), std::regex("\t"), "/");
        base64_data += field_name_len;
        uint32_t field_type = u_endian_read4(base64_data, is_bigendian);
        base64_data += 4;
        if (field_type == 0) {
            // simple string value
            uint32_t value_len = u_endian_read4(base64_data, is_bigendian);
            base64_data += 4;
            std::string value = std::string((const char *)base64_data, value_len);
            base64_data += value_len;

            fill_metadata_field(outer_field != NULL ? *outer_field : field, value);
        } else if (field_type == 3) {
            // nested struct
            uint32_t pos = base64_data - base64_data_base;
            uint32_t bytes_inner_read = parsemetadata_struct(base64_data, size - pos, &field);
            if (bytes_inner_read == 0) {
                return 0;
            }
            base64_data += bytes_inner_read;
        }
    }
    return base64_data - base64_data_base;
}

void
PTFFormat::fill_metadata_field(std::string const& field, std::string const& value) {
    static const std::string FIELD_TITLE = "http://purl.org/dc/elements/1.1/:title";
    static const std::string FIELD_ARTIST = "http://www.id3.org/id3v2.3.0#:TPE1";
    static const std::string FIELD_CONTRIBUTORS = "http://purl.org/dc/elements/1.1/:contributor";
    static const std::string FIELD_LOCATION = "http://meta.avid.com/everywhere/1.0#:location";

    if (field == FIELD_TITLE) {
        _session_meta_parsed.title = value;
    } else if (field == FIELD_ARTIST) {
        _session_meta_parsed.artist = value;
    } else if (field == FIELD_CONTRIBUTORS) {
        _session_meta_parsed.contributors.push_back(value);
    } else if (field == FIELD_LOCATION) {
        _session_meta_parsed.location = value;
    }
}

bool
PTFFormat::parsekeysigs() {
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin(); b != _blocks.end(); ++b) {
        if (b->content_type == 0x2433) {
            for (vector<PTFFormat::block_t>::iterator c = b->child.begin(); c != b->child.end(); ++c) {
                if (c->content_type == 0x2432) {
                    if (!parsekeysig(*c))
                        return false;
                }
            }
        }
    }
    return true;
}

bool
PTFFormat::parsekeysig(block_t& blk) {
    if (blk.block_size < 13)
        return false;

    uint8_t *data = &_ptfunxored[blk.offset];
    data += 2;
    uint64_t pos = u_endian_read8(data, is_bigendian) - ZERO_TICKS;
    data += 8;
    uint8_t is_major = *data++;
    uint8_t is_sharp = *data++;
    uint8_t signs = *data;

    if (is_major > 1 || is_sharp > 1 || signs > 7)
        return false;

    key_signature_ev_t parsed_sig = key_signature_ev_t(pos, (bool)is_major, (bool)is_sharp, signs);
    _keysignatures.push_back(parsed_sig);
    return true;
}

bool
PTFFormat::parsetimesigs() {
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin(); b != _blocks.end(); ++b) {
        if (b->content_type == 0x2029) {
            return parsetimesigs_block(*b);
        }
    }
    return true;
}

bool
PTFFormat::parsetimesigs_block(block_t &blk) {
    static const uint32_t HEADER_SIZE = 17;
    static const uint32_t EV_SIZE = 36;

    if (blk.block_size < HEADER_SIZE)
        return false;
    uint8_t *data = &_ptfunxored[blk.offset];
    data += 13;
    uint32_t event_count = u_endian_read4(data, is_bigendian);
    data += 4;
    if (blk.block_size < HEADER_SIZE + event_count * EV_SIZE)
        return false;

    for (int i = 0; i < event_count; i++) {
        uint64_t pos = u_endian_read8(data, is_bigendian) - ZERO_TICKS;
        data += 8;
        uint32_t measure_num = u_endian_read4(data, is_bigendian);
        data += 4;
        uint32_t nom = u_endian_read4(data, is_bigendian);
        data += 4;
        uint32_t denom = u_endian_read4(data, is_bigendian);
        data += 4 + 16; // 16 trailing bytes

        // check that nom and denom are non-zero and are in range, check that denom is power of 2
        if (nom == 0 || denom == 0 || nom > 255 || denom > 255 || floor(log2(denom)) != ceil(log2(denom)))
            return false;

        time_signature_ev_t parsed_sig = time_signature_ev_t { pos, measure_num, (uint8_t)nom, (uint8_t)denom };
        _timesignatures.push_back(parsed_sig);
    }
    return true;
}

bool
PTFFormat::parsetempochanges() {
    for (vector<PTFFormat::block_t>::iterator b = _blocks.begin(); b != _blocks.end(); ++b) {
        if (b->content_type == 0x2028) {
            return parsetempochanges_block(*b);
        }
    }
    return true;
}

bool
PTFFormat::parsetempochanges_block(block_t &blk) {
    static const uint32_t HEADER_SIZE = 17;
    static const uint32_t EV_SIZE = 61;

    if (blk.block_size < HEADER_SIZE)
        return false;
    uint8_t *data = &_ptfunxored[blk.offset];
    data += 13;
    uint32_t event_count = u_endian_read4(data, is_bigendian);
    data += 4;
    if (blk.block_size < HEADER_SIZE + event_count * EV_SIZE)
        return false;

    for (int i = 0; i < event_count; i++) {
        data += 34; // (....Const......TMS................)
        uint64_t pos = u_endian_read8(data, is_bigendian) - ZERO_TICKS;
        data += 10; // 8b + 2b (pad)
        uint64_t tempo_bytes = u_endian_read8(data, is_bigendian);
        double tempo;
        memcpy(&tempo, &tempo_bytes, sizeof(double));
        data += 8;
        uint64_t beat_length = u_endian_read8(data, is_bigendian);
        data += 9; // 8b + 1b (pad)

        // check that tempo is within range (5 - 500) and beat length is divisible by 1/32 note length
        if (tempo < 5. || tempo > 500. || beat_length % THIRTY_SECOND != 0)
            return false;

        tempo_change_t tempo_change { pos, 0, tempo, beat_length };
        if (!_tempochanges.empty()) {
            auto prev = _tempochanges.back();
            tempo_change.pos_in_samples = ticks_to_samples(tempo_change.pos, prev);
        }

        _tempochanges.push_back(tempo_change);
    }

    // if no tempos found, insert a single default tempo
    if (_tempochanges.empty()) {
        _tempochanges.push_back({ 0, 0, 120., QUARTER });
    }
    return true;
}

const PTFFormat::key_signature_t
PTFFormat::main_keysignature() {
    if (_keysignatures.empty()) {
        return {true, true, 0};
    }
    return find_main_event_value<key_signature_ev_t, key_signature_t>
    (_keysignatures, [this](const key_signature_ev_t &e){ return ticks_to_samples(e.pos); });
}

const PTFFormat::time_signature_t
PTFFormat::main_timesignature() {
    if (_timesignatures.empty()) {
        return {4, 4};
    }
    return find_main_event_value<time_signature_ev_t, time_signature_t>
    (_timesignatures, [this](const time_signature_ev_t &e){ return ticks_to_samples(e.pos); });
}

const double
PTFFormat::main_tempo() {
    // _tempochanges always contains at least one item!
    if (_tempochanges.size() < 2 || this->region_ranges().empty()) {
        return _tempochanges[0].event_value();
    }
    return find_main_event_value<tempo_change_t, double>(_tempochanges, [](const tempo_change_t &t){ return t.pos_in_samples; });
}

// finds main event value (for tempo / time sig / key sig changes) by looking up event value
// which is used for the longest region-covered time in entire session
template <class EV, class EV_VAL>
const EV_VAL
PTFFormat::find_main_event_value(const std::vector<EV> &events, std::function<const uint64_t(const EV&)> ev_pos_in_samples) {
    // TODO: assert that events is non-empty!
    if (region_ranges().empty()) {
        return events[0].event_value();
    }
    
    using iter_type = typename std::vector<EV>::const_iterator;
    std::function<const uint64_t(const iter_type&)> safe_pos_in_samples = [events, ev_pos_in_samples](const iter_type &i){
        return i != events.cend() ? ev_pos_in_samples(*i) : UINT64_MAX;
    };
    // map of usage where event value is mapped to amount of region-covered samples where it's used
    std::unordered_map<EV_VAL, uint64_t> usage;
    // iterators of events i, next_i: next_i will always be i + 1; i starts at first event
    auto i = events.cbegin();
    auto next_i = std::next(i);
    uint64_t length_from_prev = 0; // to track length from current range unused by previous event (when previous event ends before range ends)
    uint64_t pos = safe_pos_in_samples(i); // position in samples for `i`
    uint64_t next_pos = safe_pos_in_samples(next_i); // position in samples for `next_i`

    auto r = this->region_ranges().cbegin();
    while (r != this->region_ranges().cend()) {
        // advance both iterators if:
        // * range starts beyond next event position in samples AND
        // * advanced iterator `i` will still be valid (!= cend())
        if (r->startpos >= next_pos) {
            if (next_i == events.cend()) {
                break;
            }
            i++;
            next_i++;
            pos = next_pos;
            next_pos = safe_pos_in_samples(next_i);
        }
        if (r->startpos >= next_pos) {
            const uint64_t usage_length = min(length_from_prev, min(r->endpos, next_pos) - pos);
            usage[i->event_value()] += usage_length;
            length_from_prev -= usage_length;
        } else {
            usage[i->event_value()] += min(r->endpos, next_pos) - pos;
            length_from_prev = max(0ULL, r->endpos - next_pos);
            r++;
        }
    }

    // find and return event value with max usage
    using pair_type = std::pair<EV_VAL, uint64_t>;
    const auto &main = std::max_element(usage.cbegin(), usage.cend(),
                                        [](const pair_type &v1, const pair_type &v2){ return v1.second < v2.second; });
    return main->first;
}

uint64_t
PTFFormat::ticks_to_samples(uint64_t pos_in_ticks) const {
    const auto &next_tempo = std::lower_bound(_tempochanges.cbegin(), _tempochanges.cend(), pos_in_ticks,
                                              [](const tempo_change_t& t, uint64_t pos){ return t.pos < pos; });
    // lower_bound will return either valid pointer to tempo change event or end(),
    // so if it's not the first one, then it's safe to go one back
    const auto &tempo = next_tempo == _tempochanges.cbegin() ? next_tempo : std::prev(next_tempo);
    return ticks_to_samples(pos_in_ticks, *tempo);
}

uint64_t
PTFFormat::ticks_to_samples(uint64_t pos_in_ticks, const tempo_change_t& t) const {
    double beats = double(pos_in_ticks - t.pos) / t.beat_len;
    // the rounding (instead of flooring) is done by PT itself, confirmed by tests
    return t.pos_in_samples + uint64_t(round(beats * _sessionrate * 60 / t.tempo));
}

void
PTFFormat::add_region_ranges_from_tracks(const std::vector<track_t> &tracks) {
    int last_tidx = -1;
    for (auto t = tracks.cbegin(); t != tracks.cend(); ++t) {
        if (t->reg.length == 0) continue;
        region_range_t range { t->reg.startpos, t->reg.startpos + t->reg.length };
        if (t->reg.is_startpos_in_ticks) {
            range.startpos = ticks_to_samples(range.startpos);
            // !! if this is audio clip, r->length is in samples, hence a different endpos conversion
            range.endpos = t->reg.wave.filename.empty() ? ticks_to_samples(range.endpos) : range.startpos + t->reg.length;
        }
        // check last region overlap (if it's on the same track) and fix length
        if (last_tidx == t->index && _region_ranges.back().endpos > range.startpos) {
            _region_ranges.back().endpos = range.startpos;
        }
        _region_ranges.push_back(range);
        last_tidx = t->index;
    }
}

const std::vector<PTFFormat::region_range_t>&
PTFFormat::region_ranges(void) {
    if (_region_ranges_cached) {
        return _region_ranges;
    }
    _region_ranges.clear();

    // 1. build vector of all region ranges with all start positions / end positions in samples
    add_region_ranges_from_tracks(_tracks);
    add_region_ranges_from_tracks(_miditracks);
    // 2. sort all region ranges by start position
    std::sort(_region_ranges.begin(), _region_ranges.end());
    // 3. merge overlapping ranges
    auto last_to_keep = _region_ranges.begin(); // last range to keep
    auto next_r = last_to_keep == _region_ranges.end() ? last_to_keep : std::next(last_to_keep);
    for (auto i = next_r; i != _region_ranges.end(); ++i) {
        // If endpos of last range overlaps with startpos
        if (last_to_keep->endpos >= i->startpos) {
            // Merge previous and current range
            last_to_keep->endpos = max(last_to_keep->endpos, i->endpos);
        } else {
            last_to_keep++;
            if (last_to_keep != i) {
                *last_to_keep = *i;
            }
        }
    }
    // 4. discard ranges beyond last_to_keep
    if (last_to_keep != _region_ranges.end()) {
        _region_ranges.erase(++last_to_keep, _region_ranges.end());
    }

    _region_ranges_cached = true;
    return _region_ranges;
}

const uint32_t
PTFFormat::music_duration_secs(uint8_t max_gap_secs) {
    const uint64_t max_gap = max_gap_secs * _sessionrate;

    uint64_t end_at = 0, duration_agg = 0, duration_max = 0;
    for (auto &r : region_ranges()) {
        if (r.startpos > end_at + max_gap) {
            if (duration_agg > duration_max) {
                duration_max = duration_agg;
            }
            duration_agg = 0;
            end_at = 0;
        }
        duration_agg += r.endpos - r.startpos; // add current region range length to aggregate
        if (end_at != 0) {
            duration_agg += r.startpos - end_at; // also add the gap length if end_at is set
        }
        end_at = r.endpos;
    }

    return round(double(max(duration_max, duration_agg)) / _sessionrate);
}
