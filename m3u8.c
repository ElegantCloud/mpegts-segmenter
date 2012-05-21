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
target_duration* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libavformat/avformat.h"


int64_t get_ts_duration(const char *ts_file_name)
{
    AVInputFormat *ifmt = av_find_input_format("mpegts");
    if (!ifmt) {
        fprintf(stderr, "Could not find MPEG-TS demuxer\n");
        return -1;
    }

    AVFormatContext *ic = NULL;
    int ret = avformat_open_input(&ic, ts_file_name, ifmt, NULL);
    if (ret != 0) {
        fprintf(stderr, "Could not open input file %s, make sure it is an mpegts file: %d\n", ts_file_name, ret);
        return -2;
    }

    if (avformat_find_stream_info(ic, NULL) < 0) {
        fprintf(stderr, "Could not read stream information\n");
	return -3;
    }

    int64_t video_stream_duration = 0;
    int64_t audio_stream_duration = 0;
    int i;
    for (i = 0; i < ic->nb_streams; i++) {
        switch (ic->streams[i]->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                video_stream_duration = ic->streams[i]->duration;
                break;
            case AVMEDIA_TYPE_AUDIO:
                audio_stream_duration = ic->streams[i]->duration;
                break;
            default:
                break;
        }
    }

    av_free(ic);

    return video_stream_duration;
}

int main(int argc, char **argv)
{
    const char *prefix = NULL;
    unsigned int index = 0;
    const char *index_file_name = NULL;    
    unsigned int target_duration = 0;
    const char *http_prefix = NULL;
    char ts_file_name[128];

    FILE *index_fp;
    char write_buf[512];

    if (argc < 6 ) {
        fprintf(stderr, "Usage: %s <MPEG-TS file prefix> <start index> <output m3u8 index file> <target duration> <http prefix>\n", argv[0]);
        exit(1);
    }
    
    prefix = argv[1];
    index = atoi(argv[2]);
    index_file_name = argv[3];
    target_duration = atoi(argv[4]);
    http_prefix = argv[5];

    fprintf(stdout, "MPEG-TS prefix: %s\n", prefix);
    fprintf(stdout, "start index: %d\n", index);
    fprintf(stdout, "index file name: %s\n", index_file_name);
    fprintf(stdout, "target duration: %d\n", target_duration);
    fprintf(stdout, "http prefix: %s\n", http_prefix );
  

    index_fp = fopen(index_file_name, "w");
    if (!index_fp) {
        fprintf(stderr, "Could not open temporary m3u8 index file (%s), no index file will be created\n", index_file_name);
        return 1;
    }

    snprintf(write_buf, sizeof(write_buf), "#EXTM3U\n#EXT-X-TARGETDURATION:%u\n#EXT-X-MEDIA-SEQUENCE:%u\n", target_duration, index);
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
        fprintf(stderr, "Could not write to file %s, will not continue writing to index file\n", index_file_name);
        fclose(index_fp);
        return 2;
    }

    av_register_all();

    while(1){
        snprintf(ts_file_name, sizeof(ts_file_name), "%s%u.ts", prefix, index);
        
        int duration = get_ts_duration(ts_file_name); 
        if (duration < 0){
            break;
        }

        snprintf(write_buf, sizeof(write_buf), "#EXTINF:%d,\n%s%s\n", duration, http_prefix, ts_file_name);
        if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
	    fprintf(stderr, "Could not write to m3u8 index file, will not continue writing to index file\n");
	    break;
        }

        index += 1;
    }

    snprintf(write_buf, sizeof(write_buf), "#EXT-X-ENDLIST\n");
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
        fprintf(stderr, "Could not write last file and endlist tag to m3u8 index file\n");
    }

    fclose(index_fp);
    return 0;

}
