
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libavformat/avformat.h"


double get_media_duration(const char *segment_file_name)
{
    /*
    AVInputFormat *ifmt = av_find_input_format("mpegts");
    if (!ifmt) {
        fprintf(stderr, "Could not find MPEG-TS demuxer\n");
        return -1;
    }
    */

    AVInputFormat *ifmt = NULL;
    AVFormatContext *ic = NULL;
    int ret = avformat_open_input(&ic, segment_file_name, ifmt, NULL);
    if (ret != 0) {
        fprintf(stderr, "Could not open input file %s, make sure it is an valid file: %d\n", segment_file_name, ret);
        return -2;
    }

    if (avformat_find_stream_info(ic, NULL) < 0) {
        fprintf(stderr, "Could not read stream information\n");
    	return -3;
    }

    double video_stream_duration = 0.0;
    double audio_stream_duration = 0.0;
    int video_flag = 0;
    int i;
    for (i = 0; i < ic->nb_streams; i++) {
        switch (ic->streams[i]->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                video_flag = 1;
                video_stream_duration = (double)ic->streams[i]->duration * ic->streams[i]->time_base.num / ic->streams[i]->time_base.den;
                break;
            case AVMEDIA_TYPE_AUDIO:
                audio_stream_duration = (double)ic->streams[i]->duration * ic->streams[i]->time_base.num / ic->streams[i]->time_base.den;
                break;
            default:
                break;
        }
    }

    av_free(ic);

    if (video_flag) {
        return video_stream_duration;
    } else {
        return audio_stream_duration;
    }
}

int main(int argc, char **argv)
{
    const char *dir = NULL;
    const char *pattern = NULL;
    unsigned int index = 0;
    const char *index_file_name = NULL;    
    unsigned int target_duration = 0;
    const char *http_prefix = NULL;
    char segment_file_path[256];
    char segment_file_name[128];

    FILE *index_fp;
    char write_buf[512];

    if (argc < 7 ) {
        fprintf(stderr, "Usage: %s <media dir> <media file pattern> <start index> <output m3u8 index file> <target duration> <http pattern>\n", argv[0]);
        exit(1);
    }
    
    dir = argv[1];
    pattern = argv[2];
    index = atoi(argv[3]);
    index_file_name = argv[4];
    target_duration = atoi(argv[5]);
    http_prefix = argv[6];

    fprintf(stdout, "input dir: %s\n", dir);
    fprintf(stdout, "media file pattern: %s\n", pattern);
    fprintf(stdout, "start index: %d\n", index);
    fprintf(stdout, "index file name: %s\n", index_file_name);
    fprintf(stdout, "target duration: %d\n", target_duration);
    fprintf(stdout, "http pattern: %s\n", http_prefix );
  
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
        snprintf(segment_file_name, sizeof(segment_file_name), pattern, index);
        snprintf(segment_file_path, sizeof(segment_file_path), "%s%s", dir, segment_file_name);
        
        double duration = get_media_duration(segment_file_path); 
        if (duration < 0){
            break;
        }

        snprintf(write_buf, sizeof(write_buf), "#EXTINF:%u,\n%s%s\n", (unsigned int)round(duration), http_prefix, segment_file_name);
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
