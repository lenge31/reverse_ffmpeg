/*
 * write by lenge_afar@qq.com
 */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <string.h>
#include <time.h>

#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"

static struct AVInputFormat *avInFmt = NULL;
static struct AVDictionary *format_opts = NULL;
static struct AVFormatContext *avFmtCtx = NULL;
static int avFmtCtx_stream_index[AVMEDIA_TYPE_NB];
static struct AVCodecParameters *avCodecPar = NULL;
static struct AVCodecContext *avCodecCtx = NULL;
static struct AVCodec *avCodec = NULL;
static struct AVPacket *avPacket = NULL;
static struct AVFrame *avFrame_decode = NULL;
static struct AVFrame *avFrame_scale = NULL;
static struct SwsContext *swsContext = NULL;

static int screen_rotate = 270;
static struct fb_var_screeninfo fb_v_info;
static struct fb_fix_screeninfo fb_f_info;
static unsigned char *framebuffer_all = NULL;
static int framebuffer_all_len = 0;
struct fb_surface_t {
	unsigned char *framebuffer;
	int width;
	int height;
};
static struct fb_surface_t *fb_surface;
static int fb_surface_count = 0;

static char file_dev[48];
static char *file_fb = "/dev/graphics/fb0";
static int fd_fb = -1;

static char *file_capture_raw = "./capture_raw";
static int fd_capture_raw = -1;
static char *file_decode_data = "./decoded_data";
static int fd_decode_data = -1;
static char *file_scale_data = "./scaled_data";
static int fd_scale_data = -1;

static int fb_init(void);
static int rgba_rotate(unsigned char *dst, unsigned char *src, int width, int height, int angle);
static int pushToFramebuffer(unsigned char *data);
static int delta_clock_ms(struct timespec *a, struct timespec *b);

int main(int argc, char *argv[])
{
	int i = 0, frame_count = 0;
	int ret = 0;
	struct timespec t_start;
	struct timespec t_fps;
	struct timespec t_frame;
	struct timespec t_decode;
	struct timespec t_scale;
	struct timespec t_end;

	if (argc > 1) {
		strncpy(file_dev, argv[1] ,sizeof(file_dev));
	} else {
		printf("args: /dev/videoX [mjpeg/h264]\n");
		return -1;
	}

	avdevice_register_all();
/*
	printf("support all video input devices:\n");
	printf("#########\n");
	while ((avInFmt=av_input_video_device_next(avInFmt))) {
		printf("name:%s, long_name:%s, extensions:%s, mime_type:%s.\n",
			avInFmt->name, avInFmt->long_name, avInFmt->extensions, avInFmt->mime_type);
	}
	printf("#########\n");
*/
/*
	avInFmt = av_find_input_format("video4linux2");
	if (!avInFmt) {
		printf("av_find_input_format video4linux2 failed.\n");
		return -1;
	}
*/
	//av_dict_set(&format_opts, "pixel_format", "mjpeg", 0);
	//av_dict_set(&format_opts, "video_size", "1280*720", 0);
	//av_dict_set(&format_opts, "framerate", "30", 0);
	//av_dict_set(&format_opts, "max_delay", "100000", 0);
	//av_dict_set(&format_opts, "fflags", "nobuffer", 0);
	//av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
	if (argc > 2)
		av_dict_set(&format_opts, "pixel_format", argv[2], 0);
	ret = avformat_open_input(&avFmtCtx, file_dev, avInFmt, &format_opts);
	if (ret < 0) {
		printf("avformat_open_input faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	memset(avFmtCtx_stream_index, AVMEDIA_TYPE_UNKNOWN, sizeof(avFmtCtx_stream_index));

	printf("%s support streams:\n", file_dev);
	printf("#########\n");
	for (i = 0; i < (int)avFmtCtx->nb_streams; i++) {
		avFmtCtx_stream_index[avFmtCtx->streams[i]->codecpar->codec_type] = i;
		printf("\tstream %d AVMediaType is %d.\n", i, avFmtCtx->streams[i]->codecpar->codec_type);
	}
	printf("#########\n");
	printf("video stream index=%d.\n", avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO]);
	//avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO] =
	//	av_find_best_stream(avFmtCtx, AVMEDIA_TYPE_VIDEO, avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
	if (avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO] < 0) {
		printf("can't find video stream.\n");
		ret = -1;
		goto out;
	}

	ret = avformat_find_stream_info(avFmtCtx, &format_opts);
	if (ret < 0) {
		printf("avformat_find_stream_info faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	printf("%s support format:\n", file_dev);
	printf("---------\n");
	av_dump_format(avFmtCtx, 0, file_dev, 0);
	printf("---------\n");

	avCodecPar = avFmtCtx->streams[avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO]]->codecpar;
	printf("avCodecPar->codec_id=%d, avCodecPar->width=%d, avCodecPar->heigth=%d, avCodecPar->format=%d.\n",
		avCodecPar->codec_id, avCodecPar->width, avCodecPar->height, avCodecPar->format);

	avCodecCtx = avcodec_alloc_context3(NULL);
	if (avCodecCtx == NULL) {
		printf("avcodec_alloc_context3 faild.\n");
		ret = -1;
		goto out;
	}
	ret = avcodec_parameters_to_context(avCodecCtx, avCodecPar);
	if (ret < 0) {
		printf("avcodec_parameters_to_context faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	avCodec = avcodec_find_decoder(avCodecCtx->codec_id);
	if (avCodec == NULL) {
		printf("avcodec_find_decoder faild.\n");
		ret = -1;
		goto out;
	}

	ret = avcodec_open2(avCodecCtx, avCodec, NULL);
	if (ret < 0) {
		printf("avcodec_open2 faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	avPacket = (struct AVPacket *)av_malloc(sizeof(struct AVPacket));
	avFrame_decode = av_frame_alloc();
	avFrame_scale = av_frame_alloc();
	if (!avPacket || !avFrame_decode || !avFrame_scale) {
		printf("alloc faild.\n");
		ret = -ENOMEM;
		goto out;
	}
	av_init_packet(avPacket);

	ret = av_image_alloc(avFrame_scale->data, avFrame_scale->linesize, avCodecPar->width, avCodecPar->height,
				AV_PIX_FMT_RGBA, 1);
	if (ret < 0) {
		printf("dst av_image_alloc faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	fd_fb = open(file_fb, O_RDWR);
	if (fd_fb < 0) {
		printf("open %s failed(%d:%s)", file_fb, errno, strerror(errno));
		return -errno;
	}

	ret = fb_init();
	if (ret < 0) {
		printf("fb_init failed.\n");
		goto out;
	}

	clock_gettime(CLOCK_MONOTONIC, &t_fps);
	while (fd_fb >= 0) {
		clock_gettime(CLOCK_MONOTONIC, &t_start);
read_frame_again:
		av_packet_unref(avPacket);
		ret = av_read_frame(avFmtCtx, avPacket);
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				ret = 0;
				printf("av_read_frame to EOF.\n");
				break;
			}
			printf("av_read_frame faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		if (avPacket->stream_index != avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO]) goto read_frame_again;
			//printf("avPacket->stream_index=%d.\n", avPacket->stream_index);
		clock_gettime(CLOCK_MONOTONIC, &t_frame);

		ret = avcodec_send_packet(avCodecCtx, avPacket);
		if (ret < 0) {
			printf("avcodec_send_packet faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		ret = avcodec_receive_frame(avCodecCtx, avFrame_decode);
		if (ret < 0) {
			if (ret == AVERROR(EAGAIN)) {
				goto read_frame_again;
			}
			printf("avcodec_receive_frame faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_MONOTONIC, &t_decode);

		if (swsContext == NULL) {
			swsContext = sws_getContext(avCodecPar->width, avCodecPar->height, avFrame_decode->format, 
					fb_surface[0].width, fb_surface[0].height, AV_PIX_FMT_RGBA,
					SWS_FAST_BILINEAR/*SWS_POINT SWS_BICUBIC SWS_X SWS_AREA SWS_BICUBLIN
							   SWS_GAUSS SWS_SINC SWS_LANCZOS SWS_SPLINE*/,
					NULL, NULL, NULL);
			if (!swsContext) {
				printf("sws_getContext faild.\n");
				ret = -1;
				goto out;
			}
		}

		ret = sws_scale(swsContext, (const uint8_t * const*)avFrame_decode->data, avFrame_decode->linesize,
				0, avCodecPar->height, avFrame_scale->data, avFrame_scale->linesize);
		if (ret < 0) {
			printf("sws_scale faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_MONOTONIC, &t_scale);

		pushToFramebuffer(avFrame_scale->data[0]);
		clock_gettime(CLOCK_MONOTONIC, &t_end);

		frame_count++;
		if (delta_clock_ms(&t_start, &t_fps) > 3000) {
			printf("---------\n");
			printf("Total %d ms.\n", delta_clock_ms(&t_end, &t_start));
			printf("\tframe %d ms.\n", delta_clock_ms(&t_frame, &t_start));
			printf("\tdecode %d ms.\n", delta_clock_ms(&t_decode, &t_frame));
			printf("\tscale %d ms.\n", delta_clock_ms(&t_scale, &t_decode));
			printf("\trotate to framebuffer %d ms.\n", delta_clock_ms(&t_end, &t_scale));
			printf("frame rate %f fps.\n", ((float)frame_count*1000)/delta_clock_ms(&t_end, &t_fps));

			clock_gettime(CLOCK_MONOTONIC, &t_fps);
			frame_count = 0;

			if (1) {
				if (fd_capture_raw >= 0) { close(fd_capture_raw); fd_capture_raw = -1; }
				fd_capture_raw = open(file_capture_raw, O_RDWR|O_TRUNC);
				if (fd_capture_raw >= 0) {
					if (write(fd_capture_raw, avPacket->data, avPacket->size) < 0) {
						printf("write %s failed(%d:%s)", file_capture_raw, errno, strerror(errno));
					} else
						printf("avPacket->data=%p, avPacket->size=%d.\n", avPacket->data, avPacket->size);
				}

				if (fd_decode_data >= 0) { close(fd_decode_data); fd_decode_data = -1; }
				fd_decode_data = open(file_decode_data, O_RDWR|O_TRUNC);
				if (fd_decode_data >= 0) {
					printf("avFrame_decode: data[0]=%p, linesize[0]=%d, width=%d, height=%d, "
							"format=%d, color_range=%d.\n",
							avFrame_decode->data[0], avFrame_decode->linesize[0],
							avFrame_decode->width, avFrame_decode->height, avFrame_decode->format,
							avFrame_decode->color_range);
					if (write(fd_decode_data, avFrame_decode->data[0],
							avFrame_decode->width*avFrame_decode->height / 4) < 0) {
						printf("write %s failed(%d:%s)", file_decode_data, errno, strerror(errno));
					}
				}

				if (fd_scale_data >= 0) { close(fd_scale_data); fd_scale_data = -1; }
				fd_scale_data = open(file_scale_data, O_RDWR|O_TRUNC);
				if (fd_scale_data >= 0) {
					printf("avFrame_scale: data[0]=%p, linesize[0]=%d, width=%d, height=%d, format=%d.\n",
							avFrame_scale->data[0], avFrame_scale->linesize[0],
							fb_surface[0].width, fb_surface[0].height, AV_PIX_FMT_RGBA); 
					if (write(fd_scale_data, avFrame_scale->data[0],
							fb_surface[0].width * fb_surface[0].height * 4) < 0) {
						printf("write %s failed(%d:%s)", file_scale_data, errno, strerror(errno));
					}
				}
			}
		}
	}

out:
	if (avFrame_scale && avFrame_scale->data[0]) {
		av_freep(&avFrame_scale->data[0]);
		av_frame_free(&avFrame_scale); avFrame_scale = NULL;}
	if (swsContext) { sws_freeContext(swsContext); swsContext = NULL; }
	if (avFrame_decode) { av_frame_free(&avFrame_decode); avFrame_decode = NULL; }
	if (avCodecCtx) { avcodec_close(avCodecCtx); avcodec_free_context(&avCodecCtx); avCodecCtx = NULL; }
	if (avPacket) { av_packet_unref(avPacket); av_free(avPacket); avPacket = NULL; }
	if (avFmtCtx) { avformat_close_input(&avFmtCtx); avFmtCtx = NULL; }
	if (format_opts) { av_dict_free(&format_opts); format_opts = NULL;}
	if (framebuffer_all && framebuffer_all != MAP_FAILED) { munmap(framebuffer_all,
		framebuffer_all_len); framebuffer_all = NULL;} 

	if (fb_surface) { free(fb_surface); fb_surface = NULL;}
	if (fd_capture_raw >= 0) { close(fd_capture_raw); fd_capture_raw = -1; }
	if (fd_decode_data >= 0) { close(fd_decode_data); fd_decode_data = -1; }
	if (fd_scale_data >= 0) { close(fd_scale_data); fd_scale_data = -1; }
	if (fd_fb >= 0) { close(fd_fb); fd_fb = -1; }

	return ret;
}

static int fb_init(void)
{
	int ret = 0;
	int i = 0;

	ret = ioctl(fd_fb, FBIOGET_FSCREENINFO, &fb_f_info);
	if (ret < 0) {
		printf("ioctl FBIOGET_FSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
		return -errno;
	}

	ret = ioctl(fd_fb, FBIOGET_VSCREENINFO, &fb_v_info);
	if (ret < 0) {
		printf("ioctl FBIOGET_VSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
		return -errno;
	}

	printf("framebuffer info:\n");
	printf("#########\n");
	printf("fb_fix_screeninfo:\n");
	printf("\tid\t\t%s\n", fb_f_info.id);
	printf("\tsmem_start\t0x%lx\n", fb_f_info.smem_start);
	printf("\tsmem_len\t%u\n", fb_f_info.smem_len);
	printf("\ttype\t\t0x%x\n", fb_f_info.type);
	printf("\ttype_aux\t0x%x\n", fb_f_info.type_aux);
	printf("\tvisual\t\t0x%x\n", fb_f_info.visual);
	printf("\txpanstep\t%u\n", fb_f_info.xpanstep);
	printf("\typanstep\t%u\n", fb_f_info.ypanstep);
	printf("\tywrapstep\t%u\n", fb_f_info.ywrapstep);
	printf("\tline_length\t%u\n", fb_f_info.line_length);
	printf("\tmmio_start\t0x%lx\n", fb_f_info.mmio_start);
	printf("\tmmio_len\t%u\n", fb_f_info.mmio_len);
	printf("\taccel\t%u\n", fb_f_info.accel);
	printf("\tcapabilities\t0x%x\n", fb_f_info.capabilities);

	printf("fb_var_screeninfo:\n");
	printf("\txres\t%u\n", fb_v_info.xres);
	printf("\tyres\t%u\n", fb_v_info.yres);
	printf("\txres_virtual\t%u\n", fb_v_info.xres_virtual);
	printf("\tyres_virtual\t%u\n", fb_v_info.yres_virtual);
	printf("\txoffset\t%u\n", fb_v_info.xoffset);
	printf("\tyoffset\t%u\n", fb_v_info.yoffset);
	printf("\tbits_per_pixel\t%u\n", fb_v_info.bits_per_pixel);
	printf("\tgrayscale\t%u\n", fb_v_info.grayscale);
	printf("\tred_bitfield:\n");
	printf("\t\toffset\t%u\n", fb_v_info.red.offset);
	printf("\t\tlength\t%u\n", fb_v_info.red.length);
	printf("\t\tmsb_right\t%u\n", fb_v_info.red.msb_right);
	printf("\tgreen_bitfield:\n");
	printf("\t\toffset\t%u\n", fb_v_info.green.offset);
	printf("\t\tlength\t%u\n", fb_v_info.green.length);
	printf("\t\tmsb_right\t%u\n", fb_v_info.green.msb_right);
	printf("\tblue_bitfield:\n");
	printf("\t\toffset\t%u\n", fb_v_info.blue.offset);
	printf("\t\tlength\t%u\n", fb_v_info.blue.length);
	printf("\t\tmsb_right\t%u\n", fb_v_info.blue.msb_right);
	printf("\ttransp_bitfield:\n");
	printf("\t\toffset\t%u\n", fb_v_info.transp.offset);
	printf("\t\tlength\t%u\n", fb_v_info.transp.length);
	printf("\t\tmsb_right\t%u\n", fb_v_info.transp.msb_right);
	printf("\tnonstd\t%u\n", fb_v_info.nonstd);
	printf("\tactivate\t0x%x\n", fb_v_info.activate);
	printf("\theight\t%umm\n", fb_v_info.height);
	printf("\twidth\t%umm\n", fb_v_info.width);
	printf("\tpixclock\t%ups\n", fb_v_info.pixclock);
	printf("\tleft_margin\t%u\n", fb_v_info.left_margin);
	printf("\tright_margin\t%u\n", fb_v_info.right_margin);
	printf("\tupper_margin\t%u\n", fb_v_info.upper_margin);
	printf("\tlower_margin\t%u\n", fb_v_info.lower_margin);
	printf("\thsync_len\t%u\n", fb_v_info.hsync_len);
	printf("\tvsync_len\t%u\n", fb_v_info.vsync_len);
	printf("\tsync\t0x%x\n", fb_v_info.sync);
	printf("\tvmode\t0x%x\n", fb_v_info.vmode);
	printf("\trotate\t%u\n", fb_v_info.rotate);
	printf("\tcolorspace\t%u\n", fb_v_info.colorspace);
	printf("#########\n");

	framebuffer_all_len = fb_f_info.smem_len;
	framebuffer_all = (unsigned char*)mmap(0, framebuffer_all_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd_fb, 0);
	if (!framebuffer_all || framebuffer_all == MAP_FAILED) {
		printf("mmap framebuffer_all %s failed(%d:%s)", file_fb, errno, strerror(errno));
		return -errno;
	}

	fb_surface_count = framebuffer_all_len / (fb_v_info.xres*fb_v_info.yres*4);
	fb_surface = (struct fb_surface_t*) malloc(fb_surface_count * sizeof(struct fb_surface_t));
	if (fb_surface == NULL) {
		printf("malloc fb_surface faild.\n");
		return -ENOMEM;
	}
	printf("framebuffer_all=%p, fb_surface_count=%d.\n", framebuffer_all, fb_surface_count);
	
	for (i = 0; i < fb_surface_count; i++) {
		fb_surface[i].framebuffer = framebuffer_all + fb_v_info.xres*fb_v_info.yres*4 * i;
		if (screen_rotate==0 || screen_rotate==180) {
			fb_surface[i].width = fb_v_info.xres;
			fb_surface[i].height = fb_v_info.yres;
		} else {
			fb_surface[i].width = fb_v_info.yres;
			fb_surface[i].height = fb_v_info.xres;
		}
	}

	return ret;
}

static int rgba_rotate(unsigned char *dst, unsigned char *src, int width, int height, int angle)
{
	int i, j;
	int k = 0;
	int linesize = width*4;

	if (angle == 0) {
		memcpy(dst, src, width*height*4);
	} else if (angle == 90) {
		for (i = 0; i < width; i++)
			for (j = height-1; j >= 0; j--) {
				memcpy(&dst[k], &src[linesize*j + i*4], 4);
				k += 4;
			}
	} else { // FIXME
	}

	return 0;
}

static int pushToFramebuffer(unsigned char *data)
{
	int ret = 0;
	static int fb_index = 0; 

	rgba_rotate(fb_surface[fb_index].framebuffer, data, fb_surface[0].width, fb_surface[0].height, 360-screen_rotate);
	fb_v_info.yoffset = fb_index*fb_v_info.yres;
	fb_v_info.activate = FB_ACTIVATE_VBL;
	ret = ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb_v_info);
	if (ret < 0) {
		printf("ioctl FBIOPUT_VSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
		ret = -errno;
	}

	if (++fb_index >= fb_surface_count) fb_index = 0;

	return ret;
}

// (a-b)ms
static int delta_clock_ms(struct timespec *a, struct timespec *b)
{
	return (a->tv_sec*1000+a->tv_nsec/1000000)-(b->tv_sec*1000+b->tv_nsec/1000000);
}
