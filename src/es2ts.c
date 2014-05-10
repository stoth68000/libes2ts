#define __USE_BSD

//#define KL_DEBUG

#include "config.h"
#include <libes2ts/es2ts.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define MAX_BUFFERS	256
#define MAX_BUFFER_SIZE 32768

struct es2ts_buffer_s {
	struct xorg_list list;
	int nr;

	unsigned char *ptr;
	unsigned int maxlen;
	unsigned int usedlen;
	unsigned int readptr;
};

static struct es2ts_buffer_s *es2ts_buffer_alloc(int nr, unsigned int size);
static void es2ts_buffer_free(struct es2ts_buffer_s *buf);
static void es2ts_buffer_recycle(struct es2ts_buffer_s *buf);
static int es2ts_data_dequeue(struct es2ts_context_s *ctx, unsigned char *data, int len);

/* Read a buffer of payload (H264 nals) from an upstream source.
 * This is a blocking read routine. If we don't block libavformat eventually
 * segfaults after huge memory allocations.
 */
static int ReadFunc(void *opaque, uint8_t *buf, int buf_size)
{
	struct es2ts_context_s *ctx = opaque;
#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p, %p, %d) %s\n", __func__, ctx, buf, buf_size, __TIME__);
#endif

	while (1) {
		int ret = es2ts_data_dequeue(ctx, buf, buf_size);
		if (ES2TS_FAILED(ret)) {
			//return AVERROR_EOF;
			usleep(100 * 1000);
			continue;
		}

		break;
	}
#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) complete %d bytes\n", __func__, ctx, buf_size);
#endif

	return buf_size;
}

/* Write a buffer of payload (TS packets) to a downstream buffer */
static int WriteFunc(void *opaque, uint8_t *buf, int buf_size)
{
#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p, %p, %d)\n", __func__, opaque, buf, buf_size);
#endif
	struct es2ts_context_s *ctx = opaque;
	return ctx->cb(ctx, buf, buf_size);
}

/* Create the output formatted stream, based on the original input stream object */
static AVStream *add_output_stream(AVFormatContext *ofc, AVStream *input_stream)
{
	AVCodecContext *icc;
	AVCodecContext *occ;
	AVStream *output_stream;

	output_stream = avformat_new_stream(ofc, 0);
	if (!output_stream) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}

	icc = input_stream->codec;
	occ = output_stream->codec;

	occ->codec_id = icc->codec_id;
	occ->codec_type = icc->codec_type;
	occ->codec_tag = icc->codec_tag;
	occ->bit_rate = icc->bit_rate;
	occ->extradata = icc->extradata;
	occ->extradata_size = icc->extradata_size;

#if 1
	if (av_q2d(icc->time_base) *
		icc->ticks_per_frame > av_q2d(input_stream->time_base)
		&& av_q2d(input_stream->time_base) < 1.0 / 1000)
	{
		occ->time_base = icc->time_base;
		occ->time_base.num *= icc->ticks_per_frame;
	} else {
		occ->time_base = input_stream->time_base;
	}

	/* TODO: Some hardcoded framerates here, review and fix. */
	occ->time_base.den = 30;
	occ->time_base.num = 1;
	occ->ticks_per_frame = 1;
	output_stream->time_base.den = 30;
	output_stream->time_base.num = 1;

	icc->time_base.den = 60;
	icc->time_base.num = 1;
	icc->ticks_per_frame = 2;
#else
	icc->time_base.den = 30;
	icc->time_base.num = 1;
	icc->ticks_per_frame = 1;
	occ->time_base = icc->time_base;
	occ->ticks_per_frame = icc->ticks_per_frame;
#endif

	switch (icc->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		occ->channel_layout = icc->channel_layout;
		occ->sample_rate = icc->sample_rate;
		occ->channels = icc->channels;
		occ->frame_size = icc->frame_size;
		if ((icc->block_align == 1 &&
			icc->codec_id == CODEC_ID_MP3) ||
			icc->codec_id == CODEC_ID_AC3)
		{
			occ->block_align = 0;
		} else {
			occ->block_align = icc->block_align;
		}
		break;
	case AVMEDIA_TYPE_VIDEO:
		occ->pix_fmt = icc->pix_fmt;
		occ->width = icc->width;
		occ->height = icc->height;
		occ->has_b_frames = icc->has_b_frames;

		if (ofc->oformat->flags & AVFMT_GLOBALHEADER) {
			occ->flags |= CODEC_FLAG_GLOBAL_HEADER;
		}
		break;
	default:
		break;
	}

	return output_stream;
}

static int process_setup(struct es2ts_context_s *ctx)
{
        int iReadBufSize = 32 * 1024;
        int iWriteBufSize = (iReadBufSize / 188) * 188;
	int ret;

	av_register_all();

	/* allocate the input media context */
	ctx->ictx = avformat_alloc_context();
	ctx->octx = avformat_alloc_context();
	if ((ctx->ictx == 0) || (ctx->octx == 0)) {
		fprintf(stderr, "unable to allocate input/output contexts\n");
		return ES2TS_ERROR;
	}

        /* Create the AVIO streaming buffers for input/output */
        ctx->pReadBuffer = (unsigned char *)malloc(iReadBufSize);
        ctx->pWriteBuffer = (unsigned char *)malloc(iWriteBufSize);
	if ((!ctx->pReadBuffer) || (!ctx->pWriteBuffer)) {
		fprintf(stderr, "unable to allocate input/output buffers\n");
		return ES2TS_ERROR;
	}

	ctx->pIOReadCtx = avio_alloc_context(ctx->pReadBuffer,
		iReadBufSize,   // internal Buffer and its size
		0,              // bWriteable (1=true, 0=false)
		ctx,            // user context data - passed to callback functions
		ReadFunc,	// Read callback
		0,              // Write callback function (not used in this example)
		0);             // Seek Function

	ctx->pIOWriteCtx = avio_alloc_context(ctx->pWriteBuffer,
		iWriteBufSize,  // internal Buffer and its size
		0,              // bWriteable (1=true, 0=false)
		ctx,            // user context data - passed to callback functions
		0,		// Read callback, (not used)
		WriteFunc,      // Write callback
		0);             // Seek Function

	if ((!ctx->pIOReadCtx) || (!ctx->pIOWriteCtx)) {
		fprintf(stderr, "avio_alloc_context allocation failed\n");
		return ES2TS_ERROR;
	}

	/* Map the custom streaming read function into the input context */
	ctx->ictx->pb = ctx->pIOReadCtx;
	ret = avformat_open_input(&ctx->ictx, "/tmp/dummy18491874", NULL, NULL);

	/* And the output writing function also */
	ctx->octx->pb = ctx->pIOWriteCtx;

	/* Query the input stream details */
	ctx->options = NULL;
	ret = avformat_find_stream_info(ctx->ictx, &ctx->options);
	if (ret < 0) {
		fprintf(stderr, "avformat_find_stream_info\n");
		return ES2TS_ERROR;
	}
	av_dump_format(ctx->ictx, 0, 0, 0);

	/* Locate the first video codec in the input stream */
	int video_codec_id = -1;
	for (unsigned int i = 0; i < ctx->ictx->nb_streams; i++) {
		if (ctx->ictx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			video_codec_id = i;
	}

	/* Output format will be MPEG2-TS based */
	ctx->fmt = av_guess_format("mpegts", NULL, NULL);
	if (!ctx->fmt) {
		fprintf(stderr, "av_guess_format\n");
		return ES2TS_ERROR;
	}

	/* Output format will match the input format */
	ctx->octx->oformat = ctx->fmt;

	/* Add a new input stream type to the output stream */
	ctx->video_st = add_output_stream(ctx->octx, ctx->ictx->streams[video_codec_id]);
#if 0
	if (avformat_set_parameters(ctx->octx, NULL) < 0) {
		fprintf(stderr, "av_set_parameters\n");
		return ES2TS_ERROR;
	}
#endif
	av_dump_format(ctx->octx, 0, 0, 1);

	/* Any headers for output are generated */
	avformat_write_header(ctx->octx, 0);

	ctx->clk = 0;

	return ES2TS_OK;
}

static int process_packet(struct es2ts_context_s *ctx, int *done)
{
	int ret = ES2TS_OK;

	AVPacket packet;
	av_init_packet(&packet);
	*done = av_read_frame(ctx->ictx, &packet);
	if (*done)
		return ES2TS_OK;

	//ctx->clk += 2000; /* No errors at 2000 but video stalls */

	//ctx->clk += 3000; /* really close at 30fps (105ms) */
	//ctx->clk += 3200; /* really close at 30fps (270 ms) */
	//ctx->clk += 2600; /* really close at 30fps (0 ms) but stalling a little */
	//ctx->clk += 2700; /* really close at 30fps (0 ms) but stalling a little */
	//ctx->clk += 2800; /* really close at 30fps (0 ms) but stalling a little */
	//ctx->clk += 2850; /* really close at 30fps (0 ms) but stalling a little */

	int duration = 2;
	ctx->clk += duration; /* really close at 30fps (0 ms) but stalling a little */

	packet.stream_index = ctx->video_st->id;
//	packet.dts = ctx->clk;
//	packet.pts = ctx->clk;
//	packet.duration = duration;

	AVStream *outStream = ctx->octx->streams[0];
	if (packet.pts != (int64_t)AV_NOPTS_VALUE) {
		packet.pts =  av_rescale_q(packet.pts,  outStream->codec->time_base, outStream->time_base);
		printf("pts\n");
	}

	if (packet.dts != (int64_t)AV_NOPTS_VALUE) {
		packet.dts = av_rescale_q(packet.dts, outStream->codec->time_base, outStream->time_base);
		printf("dts\n");
	}

	ret = av_interleaved_write_frame(ctx->octx, &packet);
	if (ret < 0) {
		fprintf(stderr, "write error\n");
		ret = ES2TS_ERROR;
	}

	av_free_packet(&packet);

	return ret;
}

static void process_teardown(struct es2ts_context_s *ctx)
{
	av_write_trailer(ctx->octx);
	avformat_close_input(&ctx->ictx);
}

static struct es2ts_buffer_s *es2ts_buffer_alloc(int nr, unsigned int size)
{
	struct es2ts_buffer_s *buf = calloc(1, sizeof(*buf));

	if (!buf)
		return 0;

	buf->nr = nr;
	buf->ptr = calloc(1, size);
	buf->maxlen = size;
	buf->usedlen = 0;
	buf->readptr = 0;

	return buf;
}

static void es2ts_buffer_free(struct es2ts_buffer_s *buf)
{
	if (!buf)
		return;

	if (buf->ptr) {
		memset(buf->ptr, 0, buf->maxlen);
		free(buf->ptr);
	}

	memset(buf, 0, sizeof(*buf));
	free(buf);
}

static void es2ts_buffer_recycle(struct es2ts_buffer_s *buf)
{
	if ((!buf) || (!buf->ptr))
		return;

	memset(buf->ptr, 0, buf->maxlen);
	buf->usedlen = 0;
	buf->readptr = 0;
}

int es2ts_alloc(struct es2ts_context_s **r)
{
	struct es2ts_context_s *ctx = calloc(1, sizeof(struct es2ts_context_s));
	if (!ctx)
		return ES2TS_ERROR;

	pthread_mutex_init(&ctx->listlock, NULL);
	xorg_list_init(&ctx->listfree);
	xorg_list_init(&ctx->listbusy);

	/* Allocate all buffers */
	for (int i = 0; i < MAX_BUFFERS; i++) {
		struct es2ts_buffer_s *buf = es2ts_buffer_alloc(i, MAX_BUFFER_SIZE);
		if (buf) {
			pthread_mutex_lock(&ctx->listlock);
			xorg_list_add(&buf->list, &ctx->listfree);
			pthread_mutex_unlock(&ctx->listlock);
		}
	}

	*r = ctx;

	return ES2TS_OK;
}

int es2ts_free(struct es2ts_context_s *ctx)
{
	struct es2ts_buffer_s *buf;
	if (!ctx)
		return ES2TS_INVALID_ARG;

	pthread_mutex_lock(&ctx->listlock);
	while (xorg_list_is_empty(&ctx->listfree) == 0) {
		buf = xorg_list_first_entry(&ctx->listfree, struct es2ts_buffer_s, list);
		xorg_list_del(&buf->list);
		es2ts_buffer_free(buf);
	}
	while (xorg_list_is_empty(&ctx->listbusy) == 0) {
		buf = xorg_list_first_entry(&ctx->listbusy, struct es2ts_buffer_s, list);
		xorg_list_del(&buf->list);
		es2ts_buffer_free(buf);
	}
	pthread_mutex_unlock(&ctx->listlock);

	memset(ctx, 0, sizeof(*ctx));

	return ES2TS_OK;
}

int es2ts_callback_register(struct es2ts_context_s *ctx, es2ts_callback cb)
{
	if ((!ctx) || (!cb))
		return ES2TS_INVALID_ARG;

	ctx->cb = cb;
	return ES2TS_OK;
}

int es2ts_callback_unregister(struct es2ts_context_s *ctx)
{
	if (!ctx)
		return ES2TS_INVALID_ARG;

	ctx->cb = 0;
	return ES2TS_OK;
}

static int es2ts_data_dequeue(struct es2ts_context_s *ctx, unsigned char *data, int len)
{
	struct es2ts_buffer_s *buf;
	int ret = ES2TS_OK;

	if ((!ctx) || (!data) || (len <= 0))
		return ES2TS_INVALID_ARG;

	int outputrem = len;
	int idx = 0;
	pthread_mutex_lock(&ctx->listlock);
	while (outputrem > 0) {
		if (xorg_list_is_empty(&ctx->listbusy)) {
			ret = ES2TS_NO_RESOURCE;
			break;
		}

		buf = xorg_list_first_entry(&ctx->listbusy, struct es2ts_buffer_s, list);
		int bufrem = buf->maxlen - buf->readptr;

		int cplen;
		if (outputrem <= bufrem)
			cplen = outputrem;
		else
			cplen = bufrem;

		memcpy(data + idx, buf->ptr + buf->readptr, cplen);
		buf->readptr += cplen;
		idx += cplen;
		outputrem -= cplen;

		if (buf->readptr == buf->maxlen) {
			xorg_list_del(&buf->list);
			es2ts_buffer_recycle(buf);
			xorg_list_append(&buf->list, &ctx->listfree);
#ifdef KL_DEBUG
			fprintf(stderr, "%s(%p, %p, %d) append to free\n", __func__, ctx, data, len);
#endif
		}
	}
	pthread_mutex_unlock(&ctx->listlock);

	if (outputrem == 0)
		ret = ES2TS_OK;

#ifdef KL_DEBUG
	fprintf(stderr, "%s() returns %d\n", __func__, ret);
#endif
	return ret;
}

int es2ts_data_enqueue(struct es2ts_context_s *ctx, unsigned char *data, int len)
{
	struct es2ts_buffer_s *buf;
	int ret = ES2TS_OK;

	if ((!ctx) || (!data) || (len <= 0))
		return ES2TS_INVALID_ARG;

#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p, %p, %d)\n", __func__, ctx, data, len);
#endif

	int inputrem = len;
	int idx = 0;
	pthread_mutex_lock(&ctx->listlock);
	while (inputrem > 0) {
		if (xorg_list_is_empty(&ctx->listfree)) {
			ret = ES2TS_ERROR;
			break;
		}

		buf = xorg_list_first_entry(&ctx->listfree, struct es2ts_buffer_s, list);
		int bufrem = buf->maxlen - buf->usedlen;

		int cplen;
		if (inputrem <= bufrem)
			cplen = inputrem;
		else
			cplen = bufrem;

		memcpy(buf->ptr + buf->usedlen, data + idx, cplen);
		buf->usedlen += cplen;
		idx += cplen;
		inputrem -= cplen;

		if (buf->usedlen == buf->maxlen) {
			xorg_list_del(&buf->list);
			xorg_list_append(&buf->list, &ctx->listbusy);
#ifdef KL_DEBUG
			fprintf(stderr, "%s(%p, %p, %d) append to busy\n", __func__, ctx, data, len);
#endif
		}
	}
	pthread_mutex_unlock(&ctx->listlock);

	if (inputrem == 0)
		ret = ES2TS_OK;

	return ret;
}

void *es2ts_process(void *p)
{
	struct es2ts_context_s *ctx = p;
#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) Thread starts\n", __func__, ctx);
#endif

	process_setup(ctx);

	ctx->threadTerminate = 0;
	ctx->threadRunning = 1;
	int done = 0;
	while (!ctx->threadTerminate) {
		int ret = process_packet(ctx, &done);
		if (ES2TS_FAILED(ret)) {
			break;
		}

		if (done)
			ctx->threadTerminate = 1;
	}
	ctx->threadDone = 1;

	process_teardown(ctx);

#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) Thread complete\n", __func__, ctx);
#endif
	return NULL;
}

int es2ts_process_start(struct es2ts_context_s *ctx)
{
	if (!ctx)
		return ES2TS_INVALID_ARG;

#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) Creating Thread\n", __func__, ctx);
#endif

	if (pthread_create(&ctx->thread, NULL, &es2ts_process, ctx) != 0)
		return ES2TS_ERROR;

#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) Thread Creation success\n", __func__, ctx);
#endif
	return ES2TS_OK;
}

int es2ts_process_end(struct es2ts_context_s *ctx)
{
	if (!ctx)
		return ES2TS_INVALID_ARG;

#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) Thread termination requested\n", __func__, ctx);
#endif

	ctx->threadTerminate = 1;
	while (!ctx->threadDone)
		usleep(100 * 1000);
	ctx->threadRunning = 0;
	ctx->threadTerminate = 0;
#ifdef KL_DEBUG
	fprintf(stderr, "%s(%p) Thread termination complete\n", __func__, ctx);
#endif
	return ES2TS_OK;
}
