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

#define CAMERA_FRAME_WIDTH	1280
#define CAMERA_FRAME_HEIGHT	720
#define CAMERA_FRAME_SIZE	"1280*720"

static struct AVInputFormat *avInFmt = NULL;
//static struct AVOutputFormat *avOutFmt = NULL;
static struct AVFormatContext *avFmtCtx = NULL;
static struct AVCodecParameters *avCodecPar = NULL;
static struct AVCodecContext *avCodecCtx = NULL;
static struct AVCodec *avCodec = NULL;
static struct AVPacket *avPacket = NULL;
static struct AVDictionary *avInDic = NULL;
static struct AVFrame *avFrame_src = NULL;
static struct AVFrame *avFrame_dst = NULL;
static struct SwsContext *scale_context = NULL;
enum AVPixelFormat v4l2_dev_pixfmt;
static int videoIndex = -1;

static int avCallback(void *p)
{
	printf("avCallback enter, p=%p.\n", p);
	return 1;
}

static struct fb_var_screeninfo fb_v_info;
static struct fb_fix_screeninfo fb_f_info;
static unsigned char *framebuffer_all = NULL;
static int framebuffer_all_len = 0;
struct fb_surface_t {
	unsigned char *framebuffer;
	int width;
	int height;
	enum AVPixelFormat pixel_format; // AV_PIX_FMT_RGBA
};
static struct fb_surface_t *fb_surface;
static int fb_surface_count = 0;
static unsigned char *framebuffer_tmp = NULL;

static char file_dev[48];
static int fd_dev = -1;
static char *file_capture_1 = "./capture_1.jpg";
static int fd_capture_1 = -1;
static char *file_capture_2 = "./capture_2_1280x720.yuv420p";
static int fd_capture_2 = -1;
static char *file_capture_3 = "./capture_3_1280x400.rgba";
static int fd_capture_3 = -1;
static char *file_fb = "/dev/graphics/fb0";
static int fd_fb = -1;

static int capture_frame(void);
static int fb_init(void);
static int rgba_rotate_90(unsigned char *dst, unsigned char *src, int width, int height, int isClockWise);

int main(int argc, char *argv[])
{
	int i = 0, frame_count = 0, k = 0;
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
		printf("args: /dev/video0 \n");
		return -1;
	}

	avcodec_register_all();
	avdevice_register_all();
/*
	while ((avOutFmt = av_oformat_next(avOutFmt))) {
		printf("name:%s, long_name:%s, mime_type:%s, extensions:%s, "
			"video_codec=%d, audio_codec=%d, subtitle_codec=%d.\n",
			avOutFmt->name, avOutFmt->long_name, avOutFmt->mime_type, avOutFmt->extensions,
			avOutFmt->video_codec, avOutFmt->audio_codec, avOutFmt->subtitle_codec);
	}
*/
	avInFmt = av_find_input_format("video4linux2");
	if (!avInFmt) {
		printf("av_find_input_format failed.\n");
		return -1;
	}

	av_dict_set(&avInDic, "pixel_format", "mjpeg", 0);
	av_dict_set(&avInDic, "video_size", CAMERA_FRAME_SIZE, 0);
	//av_dict_set(&avInDic, "framerate", "30", 0);
	//av_dict_set(&avInDic, "max_delay", "100000", 0);
	//av_dict_set(&avInDic, "fflags", "nobuffer", 0);
	avFmtCtx = avformat_alloc_context();
	avFmtCtx->interrupt_callback.callback = avCallback;
	avFmtCtx->interrupt_callback.opaque = avFmtCtx;
	ret = avformat_open_input(&avFmtCtx, file_dev, avInFmt, &avInDic);
	if (ret < 0) {
		printf("avformat_open_input faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}
	printf("---------\n");
	av_dump_format(avFmtCtx, 0, file_dev, 0);
	printf("---------\n");
/*
	ret = avformat_find_stream_info(avFmtCtx, &avInDic);
	if (ret < 0) {
		printf("avformat_find_stream_info faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}
*/
	for (i = 0; i < (int)avFmtCtx->nb_streams; i++) {
		if (avFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
			break;
		}
	}
	if (videoIndex < 0) {
		printf("Couldn't find a video stream.\n");
		ret = -1;
		goto out;
	}

	avCodecPar = avFmtCtx->streams[videoIndex]->codecpar;
	avCodec = avcodec_find_decoder(avCodecPar->codec_id);
	if (avCodec == NULL) {
		printf("avcodec_find_decoder faild.\n");
		ret = -1;
		goto out;
	}

	avCodecCtx = avcodec_alloc_context3(avCodec);
	if (avCodecCtx == NULL) {
		printf("avcodec_alloc_context3 faild.\n");
		ret = -1;
		goto out;
	}

	ret = avcodec_open2(avCodecCtx, avCodec, NULL);
	if (ret < 0) {
		printf("avcodec_open2 faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	avPacket = (struct AVPacket *)av_malloc(sizeof(struct AVPacket));
	avFrame_src = av_frame_alloc();
	avFrame_dst = av_frame_alloc();
	if (!avPacket || !avFrame_src || !avFrame_dst) {
		printf("alloc faild.\n");
		ret = -ENOMEM;
		goto out;
	}

	fd_dev = open(file_dev, O_RDWR);
	if (fd_dev < 0) {
		printf("open %s failed(%d:%s)", file_dev, errno, strerror(errno));
		ret = -errno;
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

	ret = av_image_alloc(avFrame_dst->data, avFrame_dst->linesize, fb_surface[0].height, fb_surface[0].width,
				fb_surface[0].pixel_format, 1);
	if (ret < 0) {
		printf("dst av_image_alloc faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	printf("avCodecPar->codec_id=%d, avCodecPar->width=%d, avCodecPar->heigth=%d, avCodecPar->format=%d.\n",
		avCodecPar->codec_id, avCodecPar->width, avCodecPar->height, avCodecPar->format);
/*
	framebuffer_tmp = (unsigned char*)malloc(fb_surface[0].width * fb_surface[0].height * 4);
	if (framebuffer_tmp == NULL) {
		printf("malloc framebuffer_tmp faild.\n");
		ret = -ENOMEM;
		goto out;
	}
*/
	clock_gettime(CLOCK_REALTIME, &t_fps);
	i = 0; frame_count = 0; k = 0;
	while (fd_fb >= 0) {
		clock_gettime(CLOCK_REALTIME, &t_start);
		frame_count++;
		ret = capture_frame();
		if (ret < 0) {
			printf("capture_frame failed.\n");
			goto out;
		}
		clock_gettime(CLOCK_REALTIME, &t_frame);

		ret = avcodec_send_packet(avCodecCtx, avPacket);
		if (ret < 0) {
			printf("avcodec_send_packet faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		ret = avcodec_receive_frame(avCodecCtx, avFrame_src);
		if (ret < 0) {
			printf("avcodec_receive_frame faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_REALTIME, &t_decode);

		if (scale_context == NULL) {
			scale_context = sws_getContext(avCodecPar->width, avCodecPar->height, avFrame_src->format, 
						fb_surface[0].height, fb_surface[0].width, fb_surface[0].pixel_format,
						SWS_FAST_BILINEAR/*SWS_POINT SWS_BICUBIC SWS_X SWS_AREA SWS_BICUBLIN
						SWS_GAUSS SWS_SINC SWS_LANCZOS SWS_SPLINE*/,
						NULL, NULL, NULL);
			if (!scale_context) {
				printf("sws_getContext faild.\n");
				ret = -1;
				goto out;
			}
		}
		ret = sws_scale(scale_context, (const uint8_t * const*)avFrame_src->data, avFrame_src->linesize,
				0, avCodecPar->height, avFrame_dst->data, avFrame_dst->linesize);
		if (ret < 0) {
			printf("sws_scale faild(%d:%s).\n", ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_REALTIME, &t_scale);

		if (framebuffer_tmp) {
			rgba_rotate_90(framebuffer_tmp, avFrame_dst->data[0], fb_surface[0].height, fb_surface[0].width, 1);
			memcpy(fb_surface[i].framebuffer, framebuffer_tmp, fb_surface[0].width * fb_surface[0].height * 4);
		} else {
			rgba_rotate_90(fb_surface[i].framebuffer, avFrame_dst->data[0], fb_surface[0].height, fb_surface[0].width, 1);
		}
		fb_v_info.yoffset = i*fb_v_info.yres;
		ret = ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb_v_info);
		if (ret < 0) {
			printf("ioctl FBIOGET_FSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
			ret = -errno;
			goto out;
		}
		clock_gettime(CLOCK_REALTIME, &t_end);

		if (++i >= fb_surface_count) i = 0;

		if (((t_start.tv_sec*1000000+t_start.tv_nsec/1000)-(t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)) > 3*1000000 ) {
			printf("---------\n");
			printf("Total %f ms.\n", ((float)((t_end.tv_sec*1000000+t_end.tv_nsec/1000)-
				(t_start.tv_sec*1000000+t_start.tv_nsec/1000)))/1000);
			printf("\tframe %f ms.\n", ((float)((t_frame.tv_sec*1000000+t_frame.tv_nsec/1000)-
				(t_start.tv_sec*1000000+t_start.tv_nsec/1000)))/1000);
			printf("\tdecode %f ms.\n", ((float)((t_decode.tv_sec*1000000+t_decode.tv_nsec/1000)-
				(t_frame.tv_sec*1000000+t_frame.tv_nsec/1000)))/1000);
			printf("\tscale %f ms.\n", ((float)((t_scale.tv_sec*1000000+t_scale.tv_nsec/1000)-
				(t_decode.tv_sec*1000000+t_decode.tv_nsec/1000)))/1000);
			printf("\trotate and framebuffer %f ms.\n", ((float)((t_end.tv_sec*1000000+t_end.tv_nsec/1000)-
				(t_scale.tv_sec*1000000+t_scale.tv_nsec/1000)))/1000);
			printf("frame rate %f fps.\n", ((float)frame_count*1000000)/((t_end.tv_sec*1000000+t_end.tv_nsec/1000)-
				(t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)));
			clock_gettime(CLOCK_REALTIME, &t_fps);

			k++;
			if (k > 40) {
				k = 0;
				printf("avPacket->data=%p, avPacket->size=%d.\n", avPacket->data, avPacket->size);
				if (fd_capture_1 >= 0) { close(fd_capture_1); fd_capture_1 = -1; }
				fd_capture_1 = open(file_capture_1, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU&~S_IXUSR);
				if (fd_capture_1 < 0) {
					printf("open %s failed(%d:%s)", file_capture_1, errno, strerror(errno));
					ret = -errno;
					goto out;
				}
				if (write(fd_capture_1, avPacket->data, avPacket->size) < 0) {
					printf("write %s failed(%d:%s)", file_capture_1, errno, strerror(errno));
					ret = -errno;
					goto out;
				}

				printf("avFrame_src: data[0]=%p, linesize[0]=%d, width=%d, height=%d, format=%d, color_range=%d.\n",
					avFrame_src->data[0], avFrame_src->linesize[0],
					avFrame_src->width, avFrame_src->height, avFrame_src->format,
					avFrame_src->color_range);
				if (fd_capture_2 >= 0) { close(fd_capture_2); fd_capture_2 = -1; }
				fd_capture_2 = open(file_capture_2, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU&~S_IXUSR);
				if (fd_capture_2 < 0) {
					printf("open %s failed(%d:%s)", file_capture_2, errno, strerror(errno));
					ret = -errno;
					goto out;
				}
				if (write(fd_capture_2, avFrame_src->data[0], avFrame_src->width*avFrame_src->height) < 0) {
					printf("write %s failed(%d:%s)", file_capture_2, errno, strerror(errno));
					ret = -errno;
					goto out;
				}
				if (write(fd_capture_2, avFrame_src->data[1], avFrame_src->width*avFrame_src->height / 4) < 0) {
					printf("write %s failed(%d:%s)", file_capture_2, errno, strerror(errno));
					ret = -errno;
					goto out;
				}
				if (write(fd_capture_2, avFrame_src->data[2], avFrame_src->width*avFrame_src->height / 4) < 0) {
					printf("write %s failed(%d:%s)", file_capture_2, errno, strerror(errno));
					ret = -errno;
					goto out;
				}

				printf("avFrame_dst: data[0]=%p, linesize[0]=%d, width=%d, height=%d, format=%d, color_range=%d.\n",
					avFrame_dst->data[0], avFrame_dst->linesize[0], avFrame_dst->width, avFrame_dst->height,
					avFrame_dst->format, avFrame_dst->color_range);
				if (fd_capture_3 >= 0) { close(fd_capture_3); fd_capture_3 = -1; }
				fd_capture_3 = open(file_capture_3, O_RDWR|O_CREAT|O_TRUNC, S_IRWXU&~S_IXUSR);
				if (fd_capture_3 < 0) {
					printf("open %s failed(%d:%s)", file_capture_3, errno, strerror(errno));
					ret = -errno;
					goto out;
				}
				if (write(fd_capture_3, avFrame_dst->data[0], fb_surface[0].width * fb_surface[0].height * 4) < 0) {
					printf("write %s failed(%d:%s)", file_capture_3, errno, strerror(errno));
					ret = -errno;
					goto out;
				}
			}
			frame_count = 0;
		}
	}

out:
	if (framebuffer_tmp) { free(framebuffer_tmp); framebuffer_tmp = NULL;}
	if (avFrame_src) { av_frame_free(&avFrame_src); avFrame_src = NULL; }
	if (avFrame_dst && avFrame_dst->data[0]) { av_freep(&avFrame_dst->data[0]); }
	if (avFrame_dst) { av_frame_free(&avFrame_dst); avFrame_dst = NULL; }
	if (avCodecCtx) { avcodec_close(avCodecCtx); avcodec_free_context(&avCodecCtx); avCodecCtx = NULL; }
	if (scale_context) { sws_freeContext(scale_context); scale_context = NULL; }
	if (fb_surface) { free(fb_surface); fb_surface = NULL;}
	if (fd_dev >= 0) { close(fd_dev); fd_dev = -1; }
	if (fd_capture_1 >= 0) { close(fd_capture_1); fd_capture_1 = -1; }
	if (fd_capture_2 >= 0) { close(fd_capture_2); fd_capture_2 = -1; }
	if (fd_capture_3 >= 0) { close(fd_capture_3); fd_capture_3 = -1; }
	if (fd_fb >= 0) { close(fd_fb); fd_fb = -1; }
	if (avPacket) { av_packet_unref(avPacket); av_free(avPacket); avPacket = NULL; }
	if (avFmtCtx) { avformat_close_input(&avFmtCtx); avFmtCtx = NULL; }
	if (framebuffer_all && framebuffer_all != MAP_FAILED) { munmap(framebuffer_all, framebuffer_all_len); framebuffer_all = NULL;} 

	return ret;
}

static int capture_frame(void)
{
	int ret = 0;
	fd_set read_fds;
	struct timeval tv = {
		.tv_sec = 3,
		.tv_usec = 0,
	};

	FD_ZERO(&read_fds);
	FD_SET(fd_dev, &read_fds);
	while(1) {
		ret = select(fd_dev + 1, &read_fds, NULL, NULL, &tv);
		if (ret == -1 && errno == EINTR) continue;

		if (ret == 0) {
			printf("%s select timeout.\n", file_dev);
			return -1;
		}
		break;
	}
	ret = av_read_frame(avFmtCtx, avPacket);
	if (ret < 0) {
		printf("av_read_frame faild(%d:%s).\n", ret, av_err2str(ret));
		return ret;
	}
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

	printf("#########\n");
	printf("fb_fix_screeninfo:\n");
	printf("\tid\t%s\n", fb_f_info.id);
	printf("\tsmem_start\t0x%lx\n", fb_f_info.smem_start);
	printf("\tsmem_len\t%u\n", fb_f_info.smem_len);
	//printf("\ttype\t0x%x\n", fb_f_info.type);
	//printf("\ttype_aux\t0x%x\n", fb_f_info.type_aux);
	printf("\tvisual\t0x%x\n", fb_f_info.visual);
	//printf("\txpanstep\t%u\n", fb_f_info.xpanstep);
	printf("\typanstep\t%u\n", fb_f_info.ypanstep);
	//printf("\tywrapstep\t%u\n", fb_f_info.ywrapstep);
	printf("\tline_length\t%u\n", fb_f_info.line_length);
	//printf("\tmmio_start\t0x%lx\n", fb_f_info.mmio_start);
	//printf("\tmmio_len\t%u\n", fb_f_info.mmio_len);
	//printf("\taccel\t%u\n", fb_f_info.accel);
	//printf("\tcapabilities\t0x%x\n", fb_f_info.capabilities);

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
	//printf("\tnonstd\t%u\n", fb_v_info.nonstd);
	printf("\tactivate\t0x%x\n", fb_v_info.activate);
	printf("\theight\t%umm\n", fb_v_info.height);
	printf("\twidth\t%umm\n", fb_v_info.width);
	printf("\tpixclock\t%ups\n", fb_v_info.pixclock);
	//printf("\tleft_margin\t%u\n", fb_v_info.left_margin);
	//printf("\tright_margin\t%u\n", fb_v_info.right_margin);
	//printf("\tupper_margin\t%u\n", fb_v_info.upper_margin);
	//printf("\tlower_margin\t%u\n", fb_v_info.lower_margin);
	//printf("\thsync_len\t%u\n", fb_v_info.hsync_len);
	//printf("\tvsync_len\t%u\n", fb_v_info.vsync_len);
	//printf("\tsync\t0x%x\n", fb_v_info.sync);
	//printf("\tvmode\t0x%x\n", fb_v_info.vmode);
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
		fb_surface[i].width = fb_v_info.xres;
		fb_surface[i].height = fb_v_info.yres;
		fb_surface[i].pixel_format = AV_PIX_FMT_RGBA;
	}

	return ret;
}

static int rgba_rotate_90(unsigned char *dst, unsigned char *src, int width, int height, int isClockWise)
{
	int i, j;
	int k = 0;
	int linesize = width*4;

	if (isClockWise) {
		for (i = 0; i < width; i++)
			for (j = height-1; j >= 0; j--) {
				memcpy(&dst[k], &src[linesize*j + i*4], 4);
				k += 4;
			}
	} else {
	}

	return 0;
}
/*
root@m0032:/data/v4l2 # ./ffmpeg_app /dev/video0                           
---------
Input #0, video4linux2,v4l2, from '/dev/video0':
  Duration: N/A, bitrate: N/A
    Stream #0:0: Video: mjpeg, none, 1280x720, 25 fps, 25 tbr, 1000k tbn
---------
#########
fb_fix_screeninfo:
        id      sprdfb
        smem_start      0x9ed74000
        smem_len        6144000
        visual  0x2
        ypanstep        1
        line_length     1600
fb_var_screeninfo:
        xres    400
        yres    1280
        xres_virtual    400
        yres_virtual    3840
        xoffset 0
        yoffset 2560
        bits_per_pixel  32
        grayscale       0
        red_bitfield:
                offset  0
                length  8
                msb_right       0
        green_bitfield:
                offset  8
                length  8
                msb_right       0
        blue_bitfield:
                offset  16
                length  8
                msb_right       0
        transp_bitfield:
                offset  24
                length  0
                msb_right       0
        activate        0x10
        height  96mm
        width   54mm
        pixclock        32552ps
        rotate  0
        colorspace      0
#########
framebuffer_all=0xb1f24000, fb_surface_count=3.
avCodecPar->codec_id=7, avCodecPar->width=1280, avCodecPar->heigth=720, avCodecPar->format=-1.
[swscaler @ 0xb252a000] deprecated pixel format used, make sure you did set range correctly
---------
Total 84.320000 ms.
        frame 0.549000 ms.
        decode 20.356001 ms.
        scale 41.687000 ms.
        rotate and framebuffer 21.728001 ms.
frame rate 11.884336 fps.
avPacket->data=0xaa402000, avPacket->size=74256.
avFrame_src: data[0]=0xb1980000, linesize[0]=1280, width=1280, height=720, format=12, color_range=2.
avFrame_dst: data[0]=0xb1d00000, linesize[0]=5120, width=0, height=0, format=-1, color_range=0.
---------
Total 84.045998 ms.
        frame 0.458000 ms.
        decode 20.294001 ms.
        scale 41.596001 ms.
        rotate and framebuffer 21.698000 ms.
frame rate 11.766801 fps.
---------
Total 83.739998 ms.
        frame 0.488000 ms.
        decode 20.294001 ms.
        scale 41.596001 ms.
        rotate and framebuffer 21.362000 ms.
frame rate 11.886316 fps.
---------
Total 84.320000 ms.
        frame 0.580000 ms.
        decode 20.447001 ms.
        scale 41.687000 ms.
        rotate and framebuffer 21.606001 ms.
frame rate 11.885733 fps.
---------
Total 84.137001 ms.
        frame 0.488000 ms.
        decode 20.416000 ms.
        scale 41.931999 ms.
        rotate and framebuffer 21.301001 ms.
frame rate 11.898331 fps.
*/
