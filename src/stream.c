#include <stdio.h>
#include <libes2ts/es2ts.h>

/* A sample application to demonstrate the libes2ts library */

struct es2ts_context_s *ctx;

/* The es2ts library calls us back with buffers of TS packets .... */
int downstream_callback(struct es2ts_context_s *ctx, unsigned char *buf, int len)
{
	printf("%s(%p, %d)\n", __func__, buf, len);

	static FILE *fh = 0;
	if (fh == 0) {
		fh = fopen("output.mpegts", "wb");
		if (!fh) {
			fprintf(stderr, "could not open output file\n");
			exit(1);
		}
	}

	(void)fwrite(buf, 1, len, fh);

	return ES2TS_OK;
}

int main(int argc, char *argv[])
{
	int ret;

	ret = es2ts_alloc(&ctx);
	if (ES2TS_FAILED(ret))
		return 1;

	printf("Allocated a context %p\n", ctx);
	ret = es2ts_callback_register(ctx, &downstream_callback);
	if (ES2TS_FAILED(ret))
		return 1;

	printf("Callback registered\n");

	/* Upstream application pushed data into the library */
	/* If we had an application thread, we'd call this with buffers
	 * full of nal bytestream data.
	 */
//	int es2ts_data_enqueue(struct es2ts_context_s *ctx, unsigned char *data, int len);

	ret = es2ts_process_start(ctx);
	if (ES2TS_FAILED(ret))
		return 1;

	printf("Process Started\n");
	getchar();

	printf("Process ending\n");
	ret = es2ts_process_end(ctx);
	if (ES2TS_FAILED(ret))
		return 1;

	printf("Process ended\n");
	es2ts_callback_unregister(ctx);
	es2ts_free(ctx);
}
