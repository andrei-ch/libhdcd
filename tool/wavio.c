/* ------------------------------------------------------------------
 * Copyright (C) 2016 Burt P.
 * Based on wavreader.c, copyright (C) 2009 Martin Storsjo,
 * from libfdk-aac's aac-enc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */

#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "wavio.h"

#define TAG(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

struct wavio {
    FILE *fp;
    int write;  /* 0: reading, 1: writing */
    uint32_t data_length;

    int format;
    int32_t sample_rate;
    int16_t bits_per_sample;
    int16_t channels;
    int32_t byte_rate;
    int16_t block_align;

    int16_t valid_bits_per_sample;
    int32_t channel_mask;

    int streamed;
    int raw_pcm_only;
    int length_loc;
    int data_size_loc;

    uint8_t* input_buf;
    int input_buf_size;
};

static int duh_channel_mask(int channels)
{
    switch(channels) {
        case 1:
            return 0x0004;
        case 2:
            return 0x0003;
        case 4:
            return 0x0033;
    };
    return 0;
}

static int looks_ok(wavio *wav)
{
    if (!wav) return 0;
    if (wav->format != 1)
        return 0;
    switch(wav->sample_rate) {
        case 22050:
        case 44100:
        case 88200:
        case 176400:
        case 48000:
        case 96000:
        case 192000:
            break;
        default:
            return 0;
    }
    switch(wav->valid_bits_per_sample) {
        case 20:
            if (wav->bits_per_sample != 24) return 0;
        case 8:
        case 16:
        case 24:
        case 32:
        case 64:
            break;
        default:
            return 0;
    }
    if (wav->channels < 1 || wav->channels > 9)
        return 0;
    if (!wav->channel_mask)
        return 0;
    if (wav->bits_per_sample % 8 || !wav->bits_per_sample)
        return 0;
    if (wav->byte_rate != wav->channels * wav->sample_rate * wav->bits_per_sample/8)
        return 0;
    if (wav->block_align != wav->channels * wav->bits_per_sample/8)
        return 0;
    return 1;
}

static int fwrite_int8(uint8_t v, FILE *fp) {
    return fwrite(&v, 1, 1, fp);
}

static int fwrite_int16el(int16_t v, FILE *fp) {
    const uint8_t b[2] = {
        (uint16_t)v & 0xff,
        (uint16_t)v >> 8,
    };
    return fwrite(&b, 1, 2, fp);
}

static int fwrite_int24el(int32_t v, FILE *fp) {
    const uint8_t b[3] = {
        ((uint32_t)v >> 8) & 0xff,
        ((uint32_t)v >> 16) & 0xff,
        ((uint32_t)v >> 24),
    };
    return fwrite(&b, 1, 3, fp);
}

static int fwrite_int32el(int32_t v, FILE *fp) {
    const uint8_t b[4] = {
        (uint32_t)v & 0xff,
        ((uint32_t)v >> 8) & 0xff,
        ((uint32_t)v >> 16) & 0xff,
        ((uint32_t)v >> 24),
    };
    return fwrite(&b, 1, 4, fp);
}

wavio *wav_write_open(const char *filename, int channels, int sample_rate, int bits_per_sample, int raw)
{
    int ex = 0;
    wavio* wav = malloc(sizeof(wavio));
    if (!wav) return NULL;
    memset(wav, 0, sizeof(*wav));

    if (bits_per_sample == 0) return NULL;

    wav->format = 1;
    wav->channels = channels;
    wav->channel_mask = duh_channel_mask(channels);
    wav->sample_rate = sample_rate;
    if (bits_per_sample % 8) {
        wav->valid_bits_per_sample = bits_per_sample;
        wav->bits_per_sample = (bits_per_sample + 8) - (bits_per_sample % 8);
        ex = 1;
    } else {
        wav->bits_per_sample =
        wav->valid_bits_per_sample = bits_per_sample;
    }
    if (wav->bits_per_sample > 16 || channels > 2)
        ex = 1;
    wav->raw_pcm_only = raw;
    wav->byte_rate = channels * sample_rate * wav->bits_per_sample / 8;
    wav->block_align = channels * wav->bits_per_sample / 8;
    wav->write = 1;

    if (strcmp(filename, "-") == 0) {
        wav->fp = stdout;
#ifdef _WIN32
        setmode(fileno(stdout), O_BINARY);
#endif
    } else
        wav->fp = fopen(filename, "wb");
    if(!wav->fp) {
        free(wav);
        return NULL;
    }

    if (!raw) {
        fwrite("RIFF", 1, 4, wav->fp);
        wav->length_loc = ftell(wav->fp);
        fwrite_int32el(0, wav->fp);
        if (ex)
            fwrite("WAVEfmt \x28\x00\x00\x00\xFE\xFF", 1, 14, wav->fp);
        else
            fwrite("WAVEfmt \x10\x00\x00\x00\x01\x00", 1, 14, wav->fp);
        fwrite_int16el(wav->channels, wav->fp);
        fwrite_int32el(wav->sample_rate, wav->fp);
        fwrite_int32el(wav->byte_rate, wav->fp);
        fwrite_int16el(wav->block_align, wav->fp);
        fwrite_int16el(wav->bits_per_sample, wav->fp);
        if (ex) {
            fwrite_int16el(22, wav->fp);
            fwrite_int16el(wav->valid_bits_per_sample, wav->fp);
            fwrite_int32el(wav->channel_mask, wav->fp);
            fwrite("\x01\x00\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71", 1, 16, wav->fp);
        }
        fwrite("data", 1, 4, wav->fp);
        wav->data_size_loc = ftell(wav->fp);
        fwrite_int32el(0, wav->fp);
    }
    return wav;
}

int wav_write_samples(wavio *wav, const int32_t *samples, int nb_samples)
{
    int i;
    size_t elw = 0;

    if (!wav) return -1;
    for (i = 0; i < nb_samples; i++)
        if (wav->bits_per_sample == 24)
            elw += fwrite_int24el(samples[i], wav->fp);
        else if (wav->bits_per_sample == 32)
            elw += fwrite_int32el(samples[i], wav->fp);
        else if (wav->bits_per_sample == 16)
            elw += fwrite_int16el(samples[i] >> 16, wav->fp);
        else if (wav->bits_per_sample == 8)
            elw += fwrite_int8((samples[i] >> 24) + 0x80, wav->fp);
    wav->data_length += elw;
    return elw;
}

void wav_close(wavio *wav) {
    if (!wav) return;
    if (wav->fp) {
        if (wav->write && !wav->raw_pcm_only) {
            if (wav->length_loc && fseek(wav->fp, wav->length_loc, SEEK_SET) == 0)
                fwrite_int32el(wav->data_length + 44 - 8 , wav->fp);
            if (wav->data_size_loc && fseek(wav->fp, wav->data_size_loc, SEEK_SET) == 0)
                fwrite_int32el(wav->data_length, wav->fp);
        }
        if (wav->fp != stdin && wav->fp != stdout)
                fclose(wav->fp);
    }
    if (wav->input_buf)
        free(wav->input_buf);
    free(wav);
}

static uint32_t read_tag(wavio* wav) {
    uint32_t tag = 0;
    tag = (tag << 8) | fgetc(wav->fp);
    tag = (tag << 8) | fgetc(wav->fp);
    tag = (tag << 8) | fgetc(wav->fp);
    tag = (tag << 8) | fgetc(wav->fp);
    return tag;
}

static uint32_t read_int32(wavio* wav) {
    uint32_t value = 0;
    value |= fgetc(wav->fp) <<  0;
    value |= fgetc(wav->fp) <<  8;
    value |= fgetc(wav->fp) << 16;
    value |= fgetc(wav->fp) << 24;
    return value;
}

static uint16_t read_int16(wavio* wav) {
    uint16_t value = 0;
    value |= fgetc(wav->fp) << 0;
    value |= fgetc(wav->fp) << 8;
    return value;
}

static void skip(FILE *f, int n) {
    int i;
    for (i = 0; i < n; i++)
        fgetc(f);
}

wavio* wav_read_open_raw(const char *filename, int channels, int sample_rate, int bits_per_sample)
{
    wavio* wav = malloc(sizeof(wavio));
    memset(wav, 0, sizeof(wavio));

    if (bits_per_sample == 0) return NULL;

    if (!strcmp(filename, "-")) {
        wav->fp = stdin;
#ifdef _WIN32
        setmode(fileno(stdin), O_BINARY);
#endif
    } else
        wav->fp = fopen(filename, "rb");
    if (wav->fp == NULL) {
        free(wav);
        return NULL;
    }

    if (bits_per_sample % 8) {
        wav->valid_bits_per_sample = bits_per_sample;
        wav->bits_per_sample = (bits_per_sample + 8) - (bits_per_sample % 8);
    } else {
        wav->bits_per_sample =
        wav->valid_bits_per_sample = bits_per_sample;
    }
    wav->channels = channels;
    wav->channel_mask = duh_channel_mask(channels);
    wav->sample_rate = sample_rate;
    wav->byte_rate = channels * sample_rate * wav->bits_per_sample/8;
    wav->block_align = channels * wav->bits_per_sample/8;
    wav->raw_pcm_only = 1;
    wav->streamed = 1;
    return wav;
}

wavio* wav_read_open(const char *filename)
{
    uint32_t tag_RIFF = TAG('R', 'I', 'F', 'F');
    uint32_t tag_WAVE = TAG('W', 'A', 'V', 'E');
    uint32_t tag_fmt  = TAG('f', 'm', 't', ' ');
    uint32_t tag_data = TAG('d', 'a', 't', 'a');
    uint32_t scan_buf = 0;
    int c, state = 0;

    wavio* wav = malloc(sizeof(wavio));
    memset(wav, 0, sizeof(wavio));

    if (!strcmp(filename, "-")) {
        wav->fp = stdin;
#ifdef _WIN32
        setmode(fileno(stdin), O_BINARY);
#endif
    } else
        wav->fp = fopen(filename, "rb");
    if (wav->fp == NULL) {
        free(wav);
        return NULL;
    }

    while(1) {
        c = fgetc(wav->fp);
        if (c == EOF || c < 0 || c > 255) break;
        scan_buf = (scan_buf << 8) | c;
        if (scan_buf == tag_RIFF) {
            state = 1; continue;
        }
        if (scan_buf == tag_WAVE) {
            state = 2; continue;
        }
        if (scan_buf == tag_fmt && state == 2) {
            /*fmt_len          =*/ read_int32(wav);
            wav->format          = read_int16(wav);
            wav->channels        = read_int16(wav);
            wav->sample_rate     = read_int32(wav);
            wav->byte_rate       = read_int32(wav);
            wav->block_align     = read_int16(wav);
            wav->bits_per_sample = read_int16(wav);
            if (wav->format == 0xfffe) {
                /*cb_size                =*/ read_int16(wav);
                wav->valid_bits_per_sample = read_int16(wav);
                wav->channel_mask          = read_int32(wav);
                wav->format                = read_int16(wav);
            }
            state = 3;
            continue;
        }
        if (scan_buf == tag_data && state == 3) {
            wav->data_length = read_int32(wav);
            if (!wav->valid_bits_per_sample)
                wav->valid_bits_per_sample = wav->bits_per_sample;
            if (!wav->channel_mask)
                wav->channel_mask = duh_channel_mask(wav->channels);
            if (!wav->channel_mask) break;
            wav->streamed = 1;

            if (looks_ok(wav)) return wav;
            break;
        }
    }

    free(wav);
    return NULL;
}

wavio* wav_read_open_ms(const char *filename)
{
    wavio* wr = malloc(sizeof(wavio));
    long data_pos = 0;
    memset(wr, 0, sizeof(wavio));

    if (!strcmp(filename, "-")) {
        wr->fp = stdin;
#ifdef _WIN32
        setmode(fileno(stdin), O_BINARY);
#endif
    } else
        wr->fp = fopen(filename, "rb");
    if (wr->fp == NULL) {
        free(wr);
        return NULL;
    }

    while (1) {
        uint32_t tag, tag2, length;
        tag = read_tag(wr);
        if (feof(wr->fp))
            break;
        length = read_int32(wr);
        if (!length || length >= 0x7fff0000) {
            wr->streamed = 1;
            length = ~0;
        }
        if (tag != TAG('R', 'I', 'F', 'F') || length < 4) {
            fseek(wr->fp, length, SEEK_CUR);
            continue;
        }
        tag2 = read_tag(wr);
        length -= 4;
        if (tag2 != TAG('W', 'A', 'V', 'E')) {
            fseek(wr->fp, length, SEEK_CUR);
            continue;
        }
        // RIFF chunk found, iterate through it
        while (length >= 8) {
            uint32_t subtag, sublength;
            subtag = read_tag(wr);
            if (feof(wr->fp))
                break;
            sublength = read_int32(wr);
            length -= 8;
            if (length < sublength)
                break;
            if (subtag == TAG('f', 'm', 't', ' ')) {
                if (sublength < 16) {
                    // Insufficient data for 'fmt '
                    break;
                }
                wr->format          = read_int16(wr);
                wr->channels        = read_int16(wr);
                wr->sample_rate     = read_int32(wr);
                wr->byte_rate       = read_int32(wr);
                wr->block_align     = read_int16(wr);
                wr->bits_per_sample = read_int16(wr);
                if (wr->format == 0xfffe) {
                    if (sublength < 28) {
                        // Insufficient data for waveformatex
                        break;
                    }
                    skip(wr->fp, 2);
                    wr->valid_bits_per_sample = read_int16(wr);
                    wr->channel_mask          = read_int32(wr);
                    wr->format                = read_int32(wr);
                    skip(wr->fp, sublength - 28);
                } else {
                    skip(wr->fp, sublength - 16);
                }
            } else if (subtag == TAG('d', 'a', 't', 'a')) {
                data_pos = ftell(wr->fp);
                wr->data_length = sublength;
                if (!wr->data_length || wr->streamed) {
                    wr->streamed = 1;
                    return wr;
                }
                fseek(wr->fp, sublength, SEEK_CUR);
            } else {
                skip(wr->fp, sublength);
            }
            length -= sublength;
        }
        if (length > 0) {
            // Bad chunk?
            fseek(wr->fp, length, SEEK_CUR);
        }
    }
    fseek(wr->fp, data_pos, SEEK_SET);
    return wr;
}

int wav_get_header(wavio* wav, int* format, int* channels, int* sample_rate, int* bits_per_sample, int *valid_bits_per_sample, unsigned int* data_length)
{
    if (format)
        *format = wav->format;
    if (channels)
        *channels = wav->channels;
    if (sample_rate)
        *sample_rate = wav->sample_rate;
    if (bits_per_sample)
        *bits_per_sample = wav->bits_per_sample;
    if (valid_bits_per_sample)
        *valid_bits_per_sample = wav->valid_bits_per_sample;
    if (data_length)
        *data_length = wav->data_length;
    return wav->format && wav->sample_rate;
}

int wav_read(wavio* wav, unsigned char* data, unsigned int length) {
    int n;
    if (wav->fp == NULL)
        return -1;
    if (length > wav->data_length && !wav->streamed)
        length = wav->data_length;
    n = fread(data, 1, length, wav->fp);
    return n;
}

int wav_read_samples(wavio* wav, int32_t* samples, int nb_samples)
{
    int read, i, bytes_per_sample, input_size;
    if (!wav) return -1;

    bytes_per_sample = wav->bits_per_sample / 8;
    input_size = nb_samples * bytes_per_sample;

    if(!wav->input_buf)
        wav->input_buf = malloc(input_size);
    else if (wav->input_buf_size != input_size) {
        wav->input_buf = realloc(wav->input_buf, input_size);
        wav->input_buf_size = input_size;
    }
    if (!wav->input_buf) return -1;

    read = wav_read(wav, wav->input_buf, input_size);
    nb_samples = read / bytes_per_sample;
    for (i = 0; i < nb_samples; i++) {
        const uint8_t* in = &wav->input_buf[bytes_per_sample * i];
        if (bytes_per_sample == 1) {
            samples[i] = (int32_t)in[0] - 0x80;
            samples[i] <<= 24;
        } else if (bytes_per_sample == 2) {
            samples[i] = in[0] | (in[1] << 8) | ((in[1] & 0x80) ? (0xffff << 16) : 0);
            samples[i] <<= 16;
        } else if (bytes_per_sample == 3) {
            samples[i] = in[0] | (in[1] << 8) | (in[2] << 16) | ((in[2] & 0x80) ? (0xff << 24) : 0);
            samples[i] <<= 8;
        } else if (bytes_per_sample == 4) {
            samples[i] = in[0] | (in[1] << 8) | (in[2] << 16) | (in[3] << 24);
        }
    }
    return nb_samples;
}

void wavio_dump(wavio* wav, const char* tag)
{
    static const char * const fdesc[] = {
        "NULL",
        "stdin",
        "stdout",
        "<file>",
    };
    int fdi = 3;

    if (!wav) return;

    if (wav->fp == NULL)
        fdi = 0;
    if (wav->fp == stdin)
        fdi = 1;
    if (wav->fp == stdout)
        fdi = 2;

    fprintf(stderr,
        ".wavio(%s).format: %d\n"
        ".wavio(%s).channels: %d\n"
        ".wavio(%s).channel_mask: 0x%08x\n"
        ".wavio(%s).sample_rate: %d\n"
        ".wavio(%s).bits_per_sample: %d\n"
        ".wavio(%s).valid_bits_per_sample: %d\n"
        ".wavio(%s).byte_rate: %d\n"
        ".wavio(%s).block_align: %d\n"
        ".wavio(%s).write: %s\n"
        ".wavio(%s).streamed: %s\n"
        ".wavio(%s).raw_pcm_only: %s\n"
        ".wavio(%s).data_length: %d\n"
        ".wavio(%s).length_loc: %d\n"
        ".wavio(%s).data_size_loc: %d\n"
        ".wavio(%s).fp: %s\n"
        ".wavio(%s).input_buf: %s\n"
        ".wavio(%s).input_buf_size: %d\n",
        tag, wav->format,
        tag, wav->channels,
        tag, wav->channel_mask,
        tag, wav->sample_rate,
        tag, wav->bits_per_sample,
        tag, wav->valid_bits_per_sample,
        tag, wav->byte_rate,
        tag, wav->block_align,
        tag, (wav->write) ? "true" : "false",
        tag, (wav->streamed) ? "true" : "false",
        tag, (wav->raw_pcm_only) ? "true" : "false",
        tag, wav->data_length,
        tag, wav->length_loc,
        tag, wav->data_size_loc,
        tag, fdesc[fdi],
        tag, (wav->input_buf) ? "allocated" : "not allocated",
        tag, wav->input_buf_size
        );

}
