/*
 * Copyright (c) 2009 Chase Douglas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libavformat/avformat.h"

static AVStream *add_output_stream(AVFormatContext *output_format_context, AVStream *input_stream) {
    AVCodecContext *input_codec_context;
    AVCodecContext *output_codec_context;
    AVStream *output_stream;

    output_stream = av_new_stream(output_format_context, 0);
    if (!output_stream) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    input_codec_context = input_stream->codec;
    output_codec_context = output_stream->codec;

    output_codec_context->codec_id = input_codec_context->codec_id;
    output_codec_context->codec_type = input_codec_context->codec_type;
    output_codec_context->codec_tag = input_codec_context->codec_tag;
    output_codec_context->bit_rate = input_codec_context->bit_rate;
    output_codec_context->extradata = input_codec_context->extradata;
    output_codec_context->extradata_size = input_codec_context->extradata_size;

    if(av_q2d(input_codec_context->time_base) * input_codec_context->ticks_per_frame > av_q2d(input_stream->time_base) && av_q2d(input_stream->time_base) < 1.0/1000) {
        output_codec_context->time_base = input_codec_context->time_base;
        output_codec_context->time_base.num *= input_codec_context->ticks_per_frame;
    }
    else {
        output_codec_context->time_base = input_stream->time_base;
    }

    switch (input_codec_context->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            output_codec_context->channel_layout = input_codec_context->channel_layout;
            output_codec_context->sample_rate = input_codec_context->sample_rate;
            output_codec_context->channels = input_codec_context->channels;
            output_codec_context->frame_size = input_codec_context->frame_size;
            if ((input_codec_context->block_align == 1 && input_codec_context->codec_id == CODEC_ID_MP3) || input_codec_context->codec_id == CODEC_ID_AC3) {
                output_codec_context->block_align = 0;
            }
            else {
                output_codec_context->block_align = input_codec_context->block_align;
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            output_codec_context->pix_fmt = input_codec_context->pix_fmt;
            output_codec_context->width = input_codec_context->width;
            output_codec_context->height = input_codec_context->height;
            output_codec_context->has_b_frames = input_codec_context->has_b_frames;

            if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
                output_codec_context->flags |= CODEC_FLAG_GLOBAL_HEADER;
            }
            break;
    default:
        break;
    }

    return output_stream;
}

int main(int argc, char **argv)
{
    const char *input;
    const char *output_prefix;
    unsigned int  segment_duration;
    const char *index;
    const char *http_prefix;
    long max_tsfiles = 0;
    char *max_tsfiles_check;
    double prev_segment_time = 0;
    unsigned int output_index = 1;
    AVInputFormat *ifmt;
    AVOutputFormat *ofmt;
    AVFormatContext *ic = NULL;
    AVFormatContext *oc;
    AVStream *video_st;
    AVStream *audio_st;
    AVCodec *codec;
    char *output_filename;
    int video_index;
    int audio_index;
    int ret;
    int i;

    FILE *index_fp;
    char *write_buf;

    if (argc < 6 || argc > 7) {
        fprintf(stderr, "Usage: %s <input MPEG-TS file> <segment duration in seconds> <output MPEG-TS file prefix> <output m3u8 index file> <http prefix> [<segment window size>]\n", argv[0]);
        exit(1);
    }

    av_register_all();

    input = argv[1];
    if (!strcmp(input, "-")) {
        input = "pipe:";
    }
    segment_duration = atoi(argv[2]);
    output_prefix = argv[3];
    index = argv[4];
    http_prefix=argv[5];
    if (argc == 7) {
        max_tsfiles = strtol(argv[6], &max_tsfiles_check, 10);
        if (max_tsfiles_check == argv[6] || max_tsfiles < 0 || max_tsfiles >= INT_MAX) {
            fprintf(stderr, "Maximum number of ts files (%s) invalid\n", argv[6]);
            exit(1);
        }
    }

    ifmt = av_find_input_format("mpegts");
    if (!ifmt) {
        fprintf(stderr, "Could not find MPEG-TS demuxer\n");
        exit(1);
    }

    ret = av_open_input_file(&ic, input, ifmt, 0, NULL);
    if (ret != 0) {
        fprintf(stderr, "Could not open input file, make sure it is an mpegts file: %d\n", ret);
        exit(1);
    }

    if (av_find_stream_info(ic) < 0) {
        fprintf(stderr, "Could not read stream information\n");
        exit(1);
    }

    ofmt = av_guess_format("mpegts", NULL, NULL);
    if (!ofmt) {
        fprintf(stderr, "Could not find MPEG-TS muxer\n");
        exit(1);
    }

    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "Could not allocated output context");
        exit(1);
    }
    oc->oformat = ofmt;

    video_index = -1;
    audio_index = -1;

    for (i = 0; i < ic->nb_streams && (video_index < 0 || audio_index < 0); i++) {
        switch (ic->streams[i]->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                video_index = i;
                ic->streams[i]->discard = AVDISCARD_NONE;
                video_st = add_output_stream(oc, ic->streams[i]);
                break;
            case AVMEDIA_TYPE_AUDIO:
                audio_index = i;
                ic->streams[i]->discard = AVDISCARD_NONE;
                audio_st = add_output_stream(oc, ic->streams[i]);
                break;
            default:
                ic->streams[i]->discard = AVDISCARD_ALL;
                break;
        }
    }

    if (av_set_parameters(oc, NULL) < 0) {
        fprintf(stderr, "Invalid output format parameters\n");
        exit(1);
    }

    av_dump_format(oc, 0, output_prefix, 1);

    codec = avcodec_find_decoder(video_st->codec->codec_id);
    if (!codec) {
        fprintf(stderr, "Could not find video decoder, key frames will not be honored\n");
    }

    if (avcodec_open(video_st->codec, codec) < 0) {
        fprintf(stderr, "Could not open video decoder, key frames will not be honored\n");
    }

    output_filename = malloc(sizeof(char) * (strlen(output_prefix) + 15));
    if (!output_filename) {
        fprintf(stderr, "Could not allocate space for output filenames\n");
        exit(1);
    }

    snprintf(output_filename, strlen(output_prefix) + 15, "%s-%u.ts", output_prefix, output_index);
    if (avio_open(&oc->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Could not open '%s'\n", output_filename);
        exit(1);
    }

    if (av_write_header(oc)) {
        fprintf(stderr, "Could not write mpegts header to first output file\n");
        exit(1);
    }

    //create m3u8 indexc file:
    index_fp = fopen(index, "w");
    if (!index_fp) {
        fprintf(stderr, "Could not open temporary m3u8 index file (%s), no index file will be created\n", index);
        return -1;
    }

    write_buf = malloc(sizeof(char) * 1024);
    if (!write_buf) {
        fprintf(stderr, "Could not allocate write buffer for index file, index file will be invalid\n");
        fclose(index_fp);
        return -1;
    }

    snprintf(write_buf, 1024, "#EXTM3U\n#EXT-X-TARGETDURATION:%u\n", segment_duration);
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
        fprintf(stderr, "Could not write to m3u8 index file, will not continue writing to index file\n");
        free(write_buf);
        fclose(index_fp);
        return -1;
    }

    int end = 0;
    do {
        double segment_time;
        double duration_time;
        AVPacket packet;

        if (av_read_frame(ic, &packet) < 0) {
            end = 1;
        }

        if (packet.stream_index == video_index && (packet.flags & AV_PKT_FLAG_KEY)) {
        //if (packet.stream_index == video_index) {
            segment_time = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
        }
        else if (video_index < 0) {
            segment_time = (double)audio_st->pts.val * audio_st->time_base.num / audio_st->time_base.den;
        }

        duration_time = segment_time - prev_segment_time;
        
        if (end) {
            snprintf(write_buf, 1024, "#EXTINF:%u,\n%s%s-%u.ts\n", (unsigned int)round(duration_time), http_prefix, output_prefix, output_index);
            if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
                fprintf(stderr, "Could not write to m3u8 index file, will not continue writing to index file\n");
            }
            break;
        }

        if (duration_time >= segment_duration) {
            av_write_trailer(oc);
            avio_flush(oc->pb);
            avio_close(oc->pb);

            snprintf(write_buf, 1024, "#EXTINF:%u,\n%s%s-%u.ts\n", (unsigned int)round(duration_time), http_prefix, output_prefix, output_index++);
            if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
                fprintf(stderr, "Could not write to m3u8 index file, will not continue writing to index file\n");
                break;
            }

            snprintf(output_filename, strlen(output_prefix) + 15, "%s-%u.ts", output_prefix, output_index);
            if (avio_open(&oc->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
                fprintf(stderr, "Could not open '%s'\n", output_filename);
                break;
            }

            if (av_write_header(oc)) {
                fprintf(stderr, "Could not write mpegts header to first output file\n");
                exit(1);
            }

            prev_segment_time = segment_time;
        }

        ret = av_interleaved_write_frame(oc, &packet);
        if (ret < 0) {
            fprintf(stderr, "Warning: Could not write frame of stream\n");
        } else if (ret > 0) {
            fprintf(stderr, "End of stream requested\n");
            av_free_packet(&packet);
            break;
        }

        av_free_packet(&packet);

    } while (1);

    av_write_trailer(oc);

    avcodec_close(video_st->codec);

    for(i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }

    avio_close(oc->pb);
    av_free(oc);

    snprintf(write_buf, 1024, "#EXT-X-ENDLIST\n");
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
        fprintf(stderr, "Could not write last file and endlist tag to m3u8 index file\n");
    }

    free(write_buf);
    fclose(index_fp);

    return 0;
}

// vim:sw=4:tw=4:ts=4:ai:expandtab
