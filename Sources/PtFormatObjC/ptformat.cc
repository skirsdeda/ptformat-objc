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

#ifdef HAVE_GLIB
# include <glib/gstdio.h>
# define ptf_open	g_fopen
#else
# define ptf_open	fopen
#endif

#include "ptformat/ptformat.h"

#define BITCODE			"0010111100101011"
#define ZMARK			'\x5a'
#define ZERO_TICKS		0xe8d4a51000ULL
#define MAX_CONTENT_TYPE	0x3000
#define MAX_CHANNELS_PER_TRACK	8

using namespace std;

PTFFormat::PTFFormat()
    : _ptfunxored(0)
    , _len(0)
    , _sessionrate(0)
    , _version(0)
    , _product(NULL)
    , _session_meta_base64(NULL)
    , is_bigendian(false)
{
}

PTFFormat::~PTFFormat() {
    cleanup();
}

static uint16_t
u_endian_read2(unsigned char *buf, bool bigendian)
{
    if (bigendian) {
        return ((uint16_t)(buf[0]) << 8) | (uint16_t)(buf[1]);
    } else {
        return ((uint16_t)(buf[1]) << 8) | (uint16_t)(buf[0]);
    }
}

static uint32_t
u_endian_read3(unsigned char *buf, bool bigendian)
{
    if (bigendian) {
        return ((uint32_t)(buf[0]) << 16) |
            ((uint32_t)(buf[1]) << 8) |
            (uint32_t)(buf[2]);
    } else {
        return ((uint32_t)(buf[2]) << 16) |
            ((uint32_t)(buf[1]) << 8) |
            (uint32_t)(buf[0]);
    }
}

static uint32_t
u_endian_read4(unsigned char *buf, bool bigendian)
{
    if (bigendian) {
        return ((uint32_t)(buf[0]) << 24) |
            ((uint32_t)(buf[1]) << 16) |
            ((uint32_t)(buf[2]) << 8) |
            (uint32_t)(buf[3]);
    } else {
        return ((uint32_t)(buf[3]) << 24) |
            ((uint32_t)(buf[2]) << 16) |
            ((uint32_t)(buf[1]) << 8) |
            (uint32_t)(buf[0]);
    }
}

static uint64_t
u_endian_read5(unsigned char *buf, bool bigendian)
{
    if (bigendian) {
        return ((uint64_t)(buf[0]) << 32) |
            ((uint64_t)(buf[1]) << 24) |
            ((uint64_t)(buf[2]) << 16) |
            ((uint64_t)(buf[3]) << 8) |
            (uint64_t)(buf[4]);
    } else {
        return ((uint64_t)(buf[4]) << 32) |
            ((uint64_t)(buf[3]) << 24) |
            ((uint64_t)(buf[2]) << 16) |
            ((uint64_t)(buf[1]) << 8) |
            (uint64_t)(buf[0]);
    }
}

static uint64_t
u_endian_read8(unsigned char *buf, bool bigendian)
{
    if (bigendian) {
        return ((uint64_t)(buf[0]) << 56) |
            ((uint64_t)(buf[1]) << 48) |
            ((uint64_t)(buf[2]) << 40) |
            ((uint64_t)(buf[3]) << 32) |
            ((uint64_t)(buf[4]) << 24) |
            ((uint64_t)(buf[5]) << 16) |
            ((uint64_t)(buf[6]) << 8) |
            (uint64_t)(buf[7]);
    } else {
        return ((uint64_t)(buf[7]) << 56) |
            ((uint64_t)(buf[6]) << 48) |
            ((uint64_t)(buf[5]) << 40) |
            ((uint64_t)(buf[4]) << 32) |
            ((uint64_t)(buf[3]) << 24) |
            ((uint64_t)(buf[2]) << 16) |
            ((uint64_t)(buf[1]) << 8) |
            (uint64_t)(buf[0]);
    }
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
    uint8_t offsetbytes, lengthbytes, startbytes;

    if (is_bigendian) {
        offsetbytes = (_ptfunxored[j+4] & 0xf0) >> 4;
        lengthbytes = (_ptfunxored[j+3] & 0xf0) >> 4;
        startbytes = (_ptfunxored[j+2] & 0xf0) >> 4;
        //somethingbytes = (_ptfunxored[j+2] & 0xf);
        //skipbytes = _ptfunxored[j+1];
    } else {
        offsetbytes = (_ptfunxored[j+1] & 0xf0) >> 4; //3
        lengthbytes = (_ptfunxored[j+2] & 0xf0) >> 4;
        startbytes = (_ptfunxored[j+3] & 0xf0) >> 4; //1
        //somethingbytes = (_ptfunxored[j+3] & 0xf);
        //skipbytes = _ptfunxored[j+4];
    }

    switch (offsetbytes) {
    case 5:
        offset = u_endian_read5(&_ptfunxored[j+5], false);
        break;
    case 4:
        offset = (uint64_t)u_endian_read4(&_ptfunxored[j+5], false);
        break;
    case 3:
        offset = (uint64_t)u_endian_read3(&_ptfunxored[j+5], false);
        break;
    case 2:
        offset = (uint64_t)u_endian_read2(&_ptfunxored[j+5], false);
        break;
    case 1:
        offset = (uint64_t)(_ptfunxored[j+5]);
        break;
    default:
        offset = 0;
        break;
    }
    j+=offsetbytes;
    switch (lengthbytes) {
    case 5:
        length = u_endian_read5(&_ptfunxored[j+5], false);
        break;
    case 4:
        length = (uint64_t)u_endian_read4(&_ptfunxored[j+5], false);
        break;
    case 3:
        length = (uint64_t)u_endian_read3(&_ptfunxored[j+5], false);
        break;
    case 2:
        length = (uint64_t)u_endian_read2(&_ptfunxored[j+5], false);
        break;
    case 1:
        length = (uint64_t)(_ptfunxored[j+5]);
        break;
    default:
        length = 0;
        break;
    }
    j+=lengthbytes;
    switch (startbytes) {
    case 5:
        start = u_endian_read5(&_ptfunxored[j+5], false);
        break;
    case 4:
        start = (uint64_t)u_endian_read4(&_ptfunxored[j+5], false);
        break;
    case 3:
        start = (uint64_t)u_endian_read3(&_ptfunxored[j+5], false);
        break;
    case 2:
        start = (uint64_t)u_endian_read2(&_ptfunxored[j+5], false);
        break;
    case 1:
        start = (uint64_t)(_ptfunxored[j+5]);
        break;
    default:
        start = 0;
        break;
    }
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
    r.startpos = start;
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
                                    start = u_endian_read4(&_ptfunxored[j], is_bigendian);
                                    tindex = count;
                                    track_t ti;
                                    if (!find_track(tindex, ti) || !find_region(rawindex, ti.reg)) {
                                        continue;
                                    }
                                    ti.reg.startpos = start;
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
                            r.startpos = (int64_t)0xe8d4a51000ULL;
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
                                r.startpos = (int64_t)0xe8d4a51000ULL;
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
                                    start = u_endian_read5(&_ptfunxored[j], is_bigendian);
                                    tindex = count;
                                    if (!find_miditrack(tindex, ti) || !find_midiregion(rawindex, ti.reg)) {
                                        continue;
                                    }
                                    int64_t signedstart = (int64_t)(start - ZERO_TICKS);
                                    if (signedstart < 0)
                                        signedstart = -signedstart;
                                    ti.reg.startpos = signedstart;
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
    uint64_t pos = u_endian_read8(data, is_bigendian);
    data += 8;
    uint8_t is_major = *data++;
    uint8_t is_sharp = *data++;
    uint8_t signs = *data;

    if (is_major > 1 || is_sharp > 1 || signs > 7)
        return false;

    key_signature_t parsed_sig = key_signature_t { pos, (bool)is_major, (bool)is_sharp, signs };
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
        uint64_t pos = u_endian_read8(data, is_bigendian);
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

        time_signature_t parsed_sig = time_signature_t { pos, measure_num, (uint8_t)nom, (uint8_t)denom };
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
        uint64_t pos = u_endian_read8(data, is_bigendian);
        data += 10; // 8b + 2b (pad)
        uint64_t tempo_bytes = u_endian_read8(data, is_bigendian);
        double tempo;
        memcpy(&tempo, &tempo_bytes, sizeof(double));
        data += 8;
        uint64_t beat_length = u_endian_read8(data, is_bigendian);
        data += 9; // 8b + 1b (pad)

        // check that tempo is within range (5 - 500) and beat length is divisible by 1/32 note length
        if (tempo < 5. || tempo > 500. || beat_length % 120000 != 0)
            return false;

        tempo_change_t tempo_change = tempo_change_t { pos, tempo, beat_length };
        _tempochanges.push_back(tempo_change);
    }
    return true;
}
