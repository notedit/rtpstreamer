
#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>


//gcc -o test test.c -lavutil -lavformat -lavcodec -lz -lavutil -lm

#define USE_H264BSF 1


int main(int argc, char* argv[]) 
{

    AVOutputFormat *ofmt_a = NULL,*ofmt_v = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx_a = NULL, *ofmt_ctx_v = NULL;
    AVPacket pkt;
    int ret,i;
    int videoindex=-1, audioindex=-1;
    int frame_index=0;

	const char *out_filename_v = "test.h264";//Output file URL

	FILE *fp_video=fopen(out_filename_v,"wb+");  

	const char *in_filename  = "rtmp://ali.wangxiao.eaydu.com/live_bak/x_100_rtc_test";//Input file URL
	const char *out_filename_a = "test_output_audio.aac";

    av_register_all();
	avformat_network_init();

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
		printf( "Could not open input file.");
		goto end;
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		printf( "Failed to retrieve input stream information");
		goto end;
	}

    AVOutputFormat* fmt = av_guess_format("rtp", NULL, NULL);

    avformat_alloc_output_context2(&ofmt_ctx_v, fmt, fmt->name, "rtp://127.0.0.1:5000");
    if (!ofmt_ctx_v){
        ret = AVERROR_UNKNOWN;
        goto end;
    }


	printf("Writing to %s\n", ofmt_ctx_v->filename);

    ofmt_v = ofmt_ctx_v->oformat;

    avformat_alloc_output_context2(&ofmt_ctx_a, NULL, NULL, out_filename_a);
    if (!ofmt_ctx_a) {
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    ofmt_a = ofmt_ctx_a->oformat;

    for (i=0; i<ifmt_ctx->nb_streams; i++) {

        AVFormatContext *ofmt_ctx;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = NULL;

        if(ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoindex = i;
            out_stream=avformat_new_stream(ofmt_ctx_v, in_stream->codec->codec);
            ofmt_ctx=ofmt_ctx_v;
        } else if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioindex = i;
            out_stream=avformat_new_stream(ofmt_ctx_a, in_stream->codec->codec);
			ofmt_ctx=ofmt_ctx_a;
        }

        if (!out_stream) {
            printf( "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
		}

        if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
            printf( "Failed to copy context from input to output stream codec context\n");
			goto end;
		}
		out_stream->codec->codec_tag = 0;

		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

    }



	if (!(ofmt_v->flags & AVFMT_NOFILE)) {
		if (avio_open(&ofmt_ctx_v->pb, ofmt_ctx_v->filename, AVIO_FLAG_WRITE) < 0) {
			printf( "Could not open output file '%s'", ofmt_ctx_v->filename);
			goto end;
		}
	}

	if (!(ofmt_a->flags & AVFMT_NOFILE)) {
		if (avio_open(&ofmt_ctx_a->pb, out_filename_a, AVIO_FLAG_WRITE) < 0) {
			printf( "Could not open output file '%s'", out_filename_a);
			goto end;
		}
	}

	//Write file header
	if (avformat_write_header(ofmt_ctx_v, NULL) < 0) {
		printf( "Error occurred when opening video output file\n");
		goto end;
	}
	if (avformat_write_header(ofmt_ctx_a, NULL) < 0) {
		printf( "Error occurred when opening audio output file\n");
		goto end;
	}


	char buf[200000];
    AVFormatContext *ac[] = { ofmt_ctx_v };
    av_sdp_create(ac, 1, buf, 20000);
    printf("sdp:\n%s\n", buf);
    FILE* fsdp = fopen("test.sdp", "w");
    fprintf(fsdp, "%s", buf);
    fclose(fsdp);


    AVBitStreamFilterContext* h264bsfc =  av_bitstream_filter_init("h264_mp4toannexb");
	AVBitStreamFilterContext* dumpextra = av_bitstream_filter_init("dump_extra");


    while (1)
    {
        AVFormatContext *ofmt_ctx;
        AVStream *in_stream, *out_stream;

        if (av_read_frame(ifmt_ctx, &pkt) < 0) {
            break;
        }

        in_stream  = ifmt_ctx->streams[pkt.stream_index];

        if(pkt.stream_index == videoindex) {
            out_stream = ofmt_ctx_v->streams[0];
            ofmt_ctx = ofmt_ctx_v;
            printf("Write Video Packet. size:%d\tpts:%lld\n",pkt.size,pkt.pts);
#if USE_H264BSF
			int nRet = 0;
			int isKeyFrame = pkt.flags & AV_PKT_FLAG_KEY;
			av_bitstream_filter_filter(h264bsfc, in_stream->codec, NULL, &pkt.data, &pkt.size, pkt.data, pkt.size, isKeyFrame);
#endif
			fwrite(pkt.data,1,pkt.size,fp_video);

        } else if(pkt.stream_index == audioindex) {
            out_stream = ofmt_ctx_a->streams[0];
            ofmt_ctx = ofmt_ctx_a;
            printf("Write Audio Packet. size:%d\tpts:%lld\n",pkt.size,pkt.pts);
        }
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		pkt.stream_index=0;


        if (av_interleaved_write_frame(ofmt_ctx, &pkt) < 0) {
			printf( "Error muxing packet\n");
			break;
		}
		//printf("Write %8d frames to output file\n",frame_index);
		av_free_packet(&pkt);
		frame_index++;

    }

    av_bitstream_filter_close(h264bsfc);  

    av_write_trailer(ofmt_ctx_a);
	av_write_trailer(ofmt_ctx_v);

end:
	avformat_close_input(&ifmt_ctx);
	/* close output */
	if (ofmt_ctx_a && !(ofmt_a->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx_a->pb);

	if (ofmt_ctx_v && !(ofmt_v->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx_v->pb);

	avformat_free_context(ofmt_ctx_a);
	avformat_free_context(ofmt_ctx_v);

	fclose(fp_video);

	if (ret < 0 && ret != AVERROR_EOF) {
		printf( "Error occurred.\n");
		return -1;
	}
	return 0;

}