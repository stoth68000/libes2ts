#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

/* A tool to demonstrate basic ES 2 TS conversion without a custom library, directly
 * on top of libavcodec etc. Only capable of dealing with files, no streaming support.
 * Feed stream01.nals in if you want some sample data.
 */

#define STREAM_DURATION   5.0
#define STREAM_FRAME_RATE 25	/* 25 images/s */
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT PIX_FMT_YUV420P	/* default pix_fmt */

static AVStream *add_output_stream(AVFormatContext * output_format_context,
				   AVStream * input_stream)
{
	AVCodecContext *input_codec_context;
	AVCodecContext *output_codec_context;
	AVStream *output_stream;

	output_stream = avformat_new_stream(output_format_context, 0);
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

	if (av_q2d(input_codec_context->time_base) *
		input_codec_context->ticks_per_frame > av_q2d(input_stream->time_base)
		&& av_q2d(input_stream->time_base) < 1.0 / 1000)
	{
		printf("ticks\n");
		output_codec_context->time_base = input_codec_context->time_base;
		output_codec_context->time_base.num *= input_codec_context->ticks_per_frame;
	} else {
		printf("ticks2\n");
		output_codec_context->time_base = input_stream->time_base;
	}
	output_codec_context->time_base.den = 25;
	output_codec_context->time_base.num = 1;

	switch (input_codec_context->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		output_codec_context->channel_layout = input_codec_context->channel_layout;
		output_codec_context->sample_rate = input_codec_context->sample_rate;
		output_codec_context->channels = input_codec_context->channels;
		output_codec_context->frame_size = input_codec_context->frame_size;
		if ((input_codec_context->block_align == 1 && input_codec_context->codec_id == CODEC_ID_MP3)
		    || input_codec_context->codec_id == CODEC_ID_AC3) {
			output_codec_context->block_align = 0;
		} else {
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

/* add a video output stream */
static AVStream *add_video_stream(AVFormatContext * oc, enum CodecID codec_id)
{
	AVCodecContext *c;
	AVStream *st;

	st = avformat_new_stream(oc, NULL);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		exit(1);
	}

	c = st->codec;
	c->codec_id = codec_id;
	c->codec_type = AVMEDIA_TYPE_VIDEO;

	/* put sample parameters */
	c->bit_rate = 400000;

	/* resolution must be a multiple of two */
	c->width = 1280;
	c->height = 768;

	/* time base: this is the fundamental unit of time (in seconds) in terms
	   of which frame timestamps are represented. for fixed-fps content,
	   timebase should be 1/framerate and timestamp increments should be
	   identically 1. */
	c->time_base.den = 25;
	c->time_base.num = 1;
	c->gop_size = 30;	/* emit one intra frame every twelve frames at most */
	c->pix_fmt = STREAM_PIX_FMT;
	if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
		/* just for testing, we also add B frames */
		c->max_b_frames = 2;
	}
	if (c->codec_id == CODEC_ID_MPEG1VIDEO) {
		/* Needed to avoid using macroblocks in which some coeffs overflow.
		   This does not happen with normal video, it just happens here as
		   the motion of the chroma plane does not match the luma plane. */
		c->mb_decision = 2;
	}
	// some formats want stream headers to be separate
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

int main(int argc, char *argv[])
{
	av_register_all();

	AVFormatContext *ictx = NULL;
	int ret = avformat_open_input(&ictx, argv[1], NULL, NULL);
	if (ret < 0) {
		printf("open_input\n");
		abort();
	}

	AVDictionary *options = NULL;
	ret = avformat_find_stream_info(ictx, &options);
	if (ret < 0) {
		printf("find_stream_info\n");
		abort();
	}

	av_dump_format(ictx, 0, argv[1], 0);

	int video_codec_id = -1;
	for (unsigned int i = 0; i < ictx->nb_streams; ++i) {
		if (ictx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_codec_id = i;
			printf("video_codec_id = %d\n", video_codec_id);
		}
	}

	/* Output */
	AVOutputFormat *fmt = av_guess_format("mpegts", NULL, NULL);
	if (!fmt) {
		printf("av_guess_format\n");
		abort();
	}

	/* allocate the output media context */
	AVFormatContext *oc = avformat_alloc_context();
	if (!oc) {
		fprintf(stderr, "Memory error\n");
		return 1;
	}
#if 0
	oc->duration = 0;
	oc->start_time = 0;
	oc->fps_probe_size = 25;
#endif
	oc->oformat = fmt;
	char *filename = "output.mpegts";
	snprintf(oc->filename, sizeof(oc->filename), "%s", filename);

	AVStream *video_st = add_output_stream(oc, ictx->streams[video_codec_id]);

	av_dump_format(oc, 0, filename, 1);

	if (avio_open(&oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
		fprintf(stderr, "Could not open '%s'\n", filename);
		exit(1);
	}
	if (avformat_write_header(oc, NULL) < 0) {
		fprintf(stderr, "Invalid output format parameters\n");
		return 1;
	}

	int64_t c = 0;
	int decode_done = 0;
	do {
		AVPacket packet;
		av_init_packet(&packet);
		decode_done = av_read_frame(ictx, &packet);
		if (decode_done)
			break;

		packet.stream_index = video_st->id;
		packet.dts = c;
		packet.pts = c;
		c += 6000; /* Take a guess at the PTS increment */

//		printf("Read packet DTS: %ld PTS: %ld\n", packet.dts, packet.pts);

		int ret = av_interleaved_write_frame(oc, &packet);
		if (ret < 0) {
			fprintf(stderr, "Could not write frame of stream: %d\n", ret);
			break;
		}

		av_free_packet(&packet);
	} while (!decode_done);

	av_write_trailer(oc);

	avformat_close_input(&ictx);
}
