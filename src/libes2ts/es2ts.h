#ifndef ES2TS_H
#define ES2TS_H

/* A library to convert H264 NAL bytes streams into a MPEG2TS formatted stream */

#include <stdio.h>
#include <pthread.h>
#include "xorg-list.h"
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>

/* return codes */
#define ES2TS_SUCCESS(n)	(n >= 0)
#define ES2TS_FAILED(n)		(n <  0)
#define ES2TS_OK		 0
#define ES2TS_ERROR		-1
#define ES2TS_INVALID_ARG	-2
#define ES2TS_NO_RESOURCE	-3

/* Buffer / timing model is as follows:
 * 1. Upstream mechanism (the thing that generates H264 nals)
 *    generates buffers of nals. The upstream application pushes
 *    those buffers into this library via es2ts_data_enqueue().
 * 2. This library puts those buffers into a pending list.
 * 3. A library thread pulls buffers off the pending list
 *    converts the data from NALS to TS using libavformat.
 * 4. TS buffers are pushed downstream via a callback that the
 *    downstream application has registered.
 */

extern int es2ts_debug;		/* If set to 1, additional debug information */

struct es2ts_context_s;

typedef int (*es2ts_callback)(struct es2ts_context_s *ctx, unsigned char *buf, int len);

struct es2ts_context_s {
	pthread_t thread;
	int threadRunning;
	int threadTerminate;
	int threadDone;

	pthread_mutex_t listlock;
	struct xorg_list listfree;
	struct xorg_list listbusy;

	es2ts_callback cb;

	AVFormatContext *ictx;
	AVFormatContext *octx;
	unsigned char *pReadBuffer;
	unsigned char *pWriteBuffer;
	AVIOContext *pIOReadCtx;
	AVIOContext *pIOWriteCtx;
	AVDictionary *options;
	AVOutputFormat *fmt;
	AVStream *video_st;
	int64_t clk;
};

/* Allocate a process context, or free it */
int es2ts_alloc(struct es2ts_context_s **ctx);
int es2ts_free(struct es2ts_context_s *ctx);

/* Downstream process can register for payload */
int es2ts_callback_register(struct es2ts_context_s *ctx, es2ts_callback cb);
int es2ts_callback_unregister(struct es2ts_context_s *ctx);

/* Upstream application pushed data into the library */
int es2ts_data_enqueue(struct es2ts_context_s *ctx, unsigned char *data, int len);

/* Start and stop the library thread from processing data */
int es2ts_process_start(struct es2ts_context_s *ctx);
int es2ts_process_end(struct es2ts_context_s *ctx);

/* Get version information of libes2ts in runtime */
const char *es2ts_get_version(void);

#endif
