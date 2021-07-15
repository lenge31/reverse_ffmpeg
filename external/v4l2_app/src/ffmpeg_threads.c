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
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>

#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"

#include "cutils/properties.h"

#include "reverse_line_image.h"

static struct AVInputFormat *avInFmt = NULL;
//static struct AVOutputFormat *avOutFmt = NULL;
static struct AVFormatContext *avFmtCtx = NULL;
static struct AVCodecParameters *avCodecPar = NULL;
static struct AVCodec *avCodec = NULL;
static struct AVDictionary *avInDic = NULL;
enum AVPixelFormat v4l2_dev_pixfmt;
static int videoIndex = -1;

static int avCallback(void *p)
{
	printf("avCallback enter, p=%p.\n", p);
	return 1;
}

static struct fb_var_screeninfo fb_v_info;
static struct fb_fix_screeninfo fb_f_info;
static unsigned char *framebuffer_mmap = NULL;
static int framebuffer_mmap_len = 0;
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
static int fb_init(void);

static char *file_capture_raw = "./capture_raw";
static int fd_capture_raw = -1;
static char *file_decode_data = "./decoded_data";
static int fd_decode_data = -1;
static char *file_scale_data = "./scaled_data";
static int fd_scale_data = -1;

static sem_t sem_file, sem_fb;
static pthread_t pthread_array[3] = {0};
static void *pthread_routine(void *arg);
static int statistic_frame_count = 0;
static struct timespec t_statistic_fps;

static int machine_status = 0; //0:normal; 1:reverse.
static char service_bootanim_exit[PROPERTY_VALUE_MAX];
static char reverse_status_value[PROPERTY_VALUE_MAX];

static char *file_m0032_ctrl = "/proc/m0032_ctrl";
static int fd_m0032_ctrl = -1;
static int reverse_status(void);

static int rgba_mirror_level(unsigned char *dst, unsigned char *src, int width, int height);// __attribute__((optimize("O0")));
static unsigned char *reverse_line_data = NULL;
static int reverse_start_width = 0;
static int reverse_start_height= 0;
static int reverse_end_width = 0;
static int reverse_end_height= 0;
static int rgba_reverse_line_init(void);
static int rgba_reverse_line(unsigned char *data, int width, int height);
static int rgba_rotate_90(unsigned char *dst, unsigned char *src, int width, int height, int isClockWise);
static int delta_clock_ms(struct timespec *a, struct timespec *b);

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	if (argc > 1) {
		strncpy(file_dev, argv[1] ,sizeof(file_dev));
	} else {
		printf("args: /dev/video0 \n");
		return -1;
	}

	//avcodec_register_all();
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
	//av_dict_set(&avInDic, "video_size", "1280x720", 0);
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
	printf("avCodecPar->codec_id=%d, avCodecPar->width=%d, avCodecPar->heigth=%d, avCodecPar->format=%d.\n",
		avCodecPar->codec_id, avCodecPar->width, avCodecPar->height, avCodecPar->format);

	avCodec = avcodec_find_decoder(avCodecPar->codec_id);
	if (avCodec == NULL) {
		printf("avcodec_find_decoder faild.\n");
		ret = -1;
		goto out;
	}

	ret = fb_init();
	if (ret < 0) {
		printf("fb_init failed.\n");
		goto out;
	}

	ret = sem_init(&sem_file, 0, 1);
	if (ret < 0) {
		printf("sem_init sem_file failed(%d:%s).\n", errno, strerror(errno));
		ret = -errno;
		goto out;
	}
	ret = sem_init(&sem_fb, 0, 1);
	if (ret < 0) {
		printf("sem_init sem_fb failed(%d:%s).\n", errno, strerror(errno));
		ret = -errno;
		goto out;
	}

	ret = rgba_reverse_line_init();
	if (ret < 0) {
		printf("rgba_reverse_line_init failed.\n");
		goto out;
	}

	while (1) {
		struct timespec ts_sleep = {
			.tv_sec = 1,
			.tv_nsec = 0,
		};

		property_get("service.bootanim.exit", service_bootanim_exit, "0");
		if (strcmp(service_bootanim_exit, "1") == 0){
			machine_status = 0;
			break;
		}

		if (machine_status == 0) {
			if (reverse_status() > 0) {
				machine_status = 1;
				if (pthread_array[0] == 0) {
					for (i = 0; i < (int)(sizeof(pthread_array)/sizeof(pthread_t)); i++) {
						printf("pthread_create %d.\n", i);
						ret = pthread_create(&pthread_array[i], NULL, pthread_routine, (void *)i);
						if (ret != 0) {
							printf("pthread_create %d failed(%d:%s)", i, ret, strerror(ret));
							ret = -ret;
							goto out;
						}
					}
					printf("stop bootanim.\n");
					property_set("ctl.stop", "bootanim");
					printf("stop zygote.\n");
					property_set("ctl.stop", "zygote");
				}
			}
		} else {
			if (reverse_status() <= 0) {
				machine_status = 0;
				break;
			}
			property_set("ctl.stop", "bootanim");
		}
		nanosleep(&ts_sleep, NULL);
	}

	if (pthread_array[0] != 0) {
		for (i = 0; i < (int)(sizeof(pthread_array)/sizeof(pthread_t)); i++) {
			ret = pthread_kill(pthread_array[i], /*SIGKILL*/SIGUSR1);
			if (ret != 0) {
				printf("pthread_kill %d failed(%d:%s)", i, ret, strerror(ret));
				//ret = -ret;
				//goto out;
			}
		}

		for (i = 0; i < (int)(sizeof(pthread_array)/sizeof(pthread_t)); i++) {
			ret = pthread_join(pthread_array[i], NULL);
			if (ret != 0) {
				printf("pthread_join %d failed(%d:%s)", i, ret, strerror(ret));
				//ret = -ret;
				//goto out;
			}
		}

		printf("start bootanim.\n");
		property_set("service.bootanim.exit", "0");
		property_set("ctl.start", "bootanim");
		printf("start zygote.\n");
		property_set("ctl.start", "zygote");
	}
out:
	if (fd_capture_raw >= 0) { close(fd_capture_raw); fd_capture_raw = -1; }
	if (fd_decode_data >= 0) { close(fd_decode_data); fd_decode_data = -1; }
	if (fd_scale_data >= 0) { close(fd_scale_data); fd_scale_data = -1; }
	if (fb_surface) { free(fb_surface); fb_surface = NULL;}
	if (fd_fb >= 0) { close(fd_fb); fd_fb = -1; }
	if (fd_m0032_ctrl >= 0) { close(fd_m0032_ctrl); fd_m0032_ctrl = -1; }
	if (avFmtCtx) { avformat_close_input(&avFmtCtx); avFmtCtx = NULL; }
	if (framebuffer_mmap && framebuffer_mmap != MAP_FAILED) { munmap(framebuffer_mmap, framebuffer_mmap_len); framebuffer_mmap = NULL;} 
	if (reverse_line_image.width != fb_surface[0].height || reverse_line_image.height != fb_surface[0].width) {
		free(reverse_line_data);
		reverse_line_data= NULL;
	}

	return ret;
}

static int reverse_status(void)
{
	char buf_tmp[64];
	int ret = 0, k = 0;

	property_get("reverse.status", reverse_status_value, "0");
	if (strcmp(reverse_status_value, "1") == 0) return 1;

	if (file_m0032_ctrl && fd_m0032_ctrl < 0) {
		fd_m0032_ctrl = open(file_m0032_ctrl, O_RDWR);
		if (fd_m0032_ctrl < 0) {
			printf("open %s failed(%d:%s)", file_m0032_ctrl, errno, strerror(errno));
			return -errno;
		}
	}

	if (fd_m0032_ctrl < 0) return -1;
	ret = write(fd_m0032_ctrl, "reverse-detect", sizeof("reverse-detect"));
	if (ret < 0) {
		printf("write %s failed(%d:%s)", file_m0032_ctrl, errno, strerror(errno));
		return -errno;
	}
	ret = lseek(fd_m0032_ctrl, 0, SEEK_SET);
	if (ret < 0) {
		printf("lseek %s failed(%d:%s)", file_m0032_ctrl, errno, strerror(errno));
		return -errno;
	}
	memset(buf_tmp, 0, sizeof(buf_tmp));
	ret = read(fd_m0032_ctrl, buf_tmp, sizeof(buf_tmp));
	if ( ret < 0 ) {
		printf("read %s failed(%d:%s).\n", file_m0032_ctrl, errno, strerror(errno));
		return -errno;
	}
	k++;
	if (k > 100) {
		k = 0;
		printf("reverse_status:%s-%d\n", buf_tmp, atoi(buf_tmp));
	}

	return atoi(buf_tmp);
}

static int fb_init(void)
{
	int ret = 0;
	int i = 0;

	fd_fb = open(file_fb, O_RDWR);
	if (fd_fb < 0) {
		printf("open %s failed(%d:%s)", file_fb, errno, strerror(errno));
		return -errno;
	}

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

	framebuffer_mmap_len = fb_f_info.smem_len;
	framebuffer_mmap = (unsigned char*)mmap(0, framebuffer_mmap_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd_fb, 0);
	if (!framebuffer_mmap || framebuffer_mmap == MAP_FAILED) {
		printf("mmap framebuffer_mmap %s failed(%d:%s)", file_fb, errno, strerror(errno));
		return -errno;
	}

	fb_surface_count = framebuffer_mmap_len / (fb_v_info.xres*fb_v_info.yres*4);
	fb_surface = (struct fb_surface_t*) malloc(fb_surface_count * sizeof(struct fb_surface_t));
	if (fb_surface == NULL) {
		printf("malloc fb_surface faild.\n");
		return -ENOMEM;
	}
	printf("framebuffer_mmap=%p, fb_surface_count=%d.\n", framebuffer_mmap, fb_surface_count);
	
	for (i = 0; i < fb_surface_count; i++) {
		fb_surface[i].framebuffer = framebuffer_mmap + fb_v_info.xres*fb_v_info.yres*4 * i;
		fb_surface[i].width = fb_v_info.xres;
		fb_surface[i].height = fb_v_info.yres;
	}

	return ret;
}

static int rgba_mirror_level(unsigned char *dst, unsigned char *src, int width, int height)
{
	int i, j;

	for (i = 0; i < height; i++)
		for (j = 0; j < width; j++) {
			*((unsigned int *)dst + i*width + width-1-j) = *((unsigned int *)src + i*width + j);
		}

	return 0;
}

static int rgba_reverse_line_init()
{
	int i,j, ret = 0;
	int width = fb_surface[0].height;
	int height = fb_surface[0].width;
	unsigned int *src = NULL;

	if (reverse_line_image.width != fb_surface[0].height || reverse_line_image.height != fb_surface[0].width) {
		struct SwsContext *scale_context_reverse = NULL;
		unsigned char *src_data[AV_NUM_DATA_POINTERS];
		int src_linesize[AV_NUM_DATA_POINTERS];
		unsigned char *dst_data[AV_NUM_DATA_POINTERS];
		int dst_linesize[AV_NUM_DATA_POINTERS];

		reverse_line_data = malloc(fb_surface[0].height * fb_surface[0].width* 4);
		if (reverse_line_data == NULL) {
			printf("reverse_line_data malloc failed(%d:%s)", errno, strerror(errno));
			return -errno;
		}

		memset(src_data, 0, sizeof(src_data));
		memset(src_linesize, 0, sizeof(src_linesize));
		memset(dst_data, 0, sizeof(dst_data));
		memset(dst_linesize, 0, sizeof(dst_linesize));
		if (scale_context_reverse == NULL) {
			scale_context_reverse = sws_getContext(reverse_line_image.width, reverse_line_image.height, AV_PIX_FMT_RGBA, 
						fb_surface[0].height, fb_surface[0].width, AV_PIX_FMT_RGBA,
						SWS_FAST_BILINEAR/*SWS_POINT SWS_BICUBIC SWS_X SWS_AREA SWS_BICUBLIN
						SWS_GAUSS SWS_SINC SWS_LANCZOS SWS_SPLINE*/,
						NULL, NULL, NULL);
			if (!scale_context_reverse) {
				printf("sws_getContext faild.\n");
				return -1;
			}
		}

		src_data[0] = reverse_line_image.pixel_data;
		src_linesize[0] = reverse_line_image.width*4;
		dst_data[0] = reverse_line_data;
		dst_linesize[0] = fb_surface[0].height*4;
		ret = sws_scale(scale_context_reverse, (const uint8_t * const*)src_data, src_linesize,
				0, reverse_line_image.height, dst_data, dst_linesize);
		if (ret < 0) {
			printf("sws_scale faild(%d:%s).\n", ret, av_err2str(ret));
			return ret;
		}
	} else {
		reverse_line_data = reverse_line_image.pixel_data;
	}
	src = (unsigned int *)reverse_line_data;

	// find start point
	for (j=0; j<height; j++)
		for (i=0; i<width; i++) {
			if (*(src+i+j*width) != 0xfdfdfd && *(src+i+j*width) != 0x00ffffff) {
				reverse_end_height = j-1;
				break;
			}
		}
	for (i=0; i<width; i++)
		for (j=0; j<height; j++) {
			if (*(src+i+j*width) != 0xfdfdfd && *(src+i+j*width) != 0x00ffffff) {
				reverse_end_width = i-1;
				break;
			}
		}
	// find end point
	for (j=height-1; j>0; j--)
		for (i=width-1; i>0; i--) {
			if (*(src+i+j*width) != 0xfdfdfd && *(src+i+j*width) != 0x00ffffff) {
				reverse_start_height = j+1;
				break;
			}
		}
	for (i=width-1; i>0; i--)
		for (j=height-1; j>0; j--) {
			if (*(src+i+j*width) != 0xfdfdfd && *(src+i+j*width) != 0x00ffffff) {
				reverse_start_width= i+1;
				break;
			}
		}

	printf("reverse_start_width=%d, reverse_start_height=%d, reverse_end_width=%d, reverse_end_height=%d.\n",
		reverse_start_width, reverse_start_height, reverse_end_width, reverse_end_height);

	return ret;
}
static int rgba_reverse_line(unsigned char *data, int width, int height)
{
	int i = 0, j = 0;
	unsigned int *src_data = (unsigned int *)reverse_line_data;
	unsigned int *dst_data = (unsigned int *)data;

	(void *)height;

	for (j=reverse_start_height; j<reverse_end_height; j++)
		for (i=reverse_start_width; i<reverse_end_width; i++) {
			//if (i%100 == 0) printf("pixel_value=0x%x.\n", *(src_data+i+j*width));
			if (*(src_data+i+j*width) != 0xfdfdfd && *(src_data+i+j*width) != 0x00ffffff) {
				*(dst_data+i+j*width) = *(src_data+i+j*width);
			}
		}

	return 0;
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
		// to do
	}

	return 0;
}

// (a-b)ms
static int delta_clock_ms(struct timespec *a, struct timespec *b)
{
	return (a->tv_sec*1000+a->tv_nsec/1000000)-(b->tv_sec*1000+b->tv_nsec/1000000);
}

static void pthread_sa_handler(int sig)
{
	printf("%s signal %d.\n", __FUNCTION__, sig);
}

static void *pthread_routine(void *arg)
{
	int id = (int)arg;
	int fb_index = 0; 
	int ret = 0, i = 0;

	struct timespec t_start;
	struct timespec t_fps;
	struct timespec t_frame;
	struct timespec t_decode;
	struct timespec t_scale;
	struct timespec t_mirror_level;
	struct timespec t_reverse_line;
	struct timespec t_rotate;
	struct timespec t_end;

	int frame_count = 0;

	struct AVPacket avPacket;
	struct AVFrame avFrame_decode;
	struct AVFrame avFrame_scale;
	struct AVCodecContext *avCodecCtx = NULL;
	struct SwsContext *scale_context = NULL;

	unsigned char *framebuffer_mirror = NULL;
	unsigned char *framebuffer_rotate = NULL;

	struct sigaction action;
	struct sched_param param = {.sched_priority = 99};
	int policy = -1;

	policy = sched_getscheduler(0);
	ret = sched_setscheduler(0, SCHED_RR, &param);
	if (ret < 0) {
		printf("sched_setscheduler failed(%d:%s)", errno, strerror(errno));
		//goto out;
	}
	printf("pthread %d scheduler policy %d change to %d.\n", id, policy, sched_getscheduler(0));

	ret = av_image_alloc(avFrame_scale.data, avFrame_scale.linesize, avCodecPar->width, avCodecPar->height,
				AV_PIX_FMT_RGBA, 1);
	if (ret < 0) {
		printf("dst av_image_alloc faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}
/*
	for (i=0; i<AV_NUM_DATA_POINTERS; i++) {
		printf("avFrame_scale.data[%d]=%p, avFrame_scale.linesize[%d]=%d.\n", 
			i, avFrame_scale.data[i], i, avFrame_scale.linesize[i]);
	}
*/
	av_init_packet(&avPacket);

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

	framebuffer_mirror = (unsigned char*)malloc(fb_surface[0].width * fb_surface[0].height * 4);
	if (framebuffer_mirror == NULL) {
		printf("pthread-%d:malloc framebuffer_mirror faild.\n", id);
		ret = -ENOMEM;
		goto out;
	}
	framebuffer_rotate = (unsigned char*)malloc(fb_surface[0].width * fb_surface[0].height * 4);
	if (framebuffer_rotate == NULL) {
		printf("pthread-%d:malloc framebuffer_rotate faild.\n", id);
		ret = -ENOMEM;
		goto out;
	}

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = pthread_sa_handler;
	sigaction(SIGUSR1, &action, NULL);

	clock_gettime(CLOCK_MONOTONIC, &t_statistic_fps);
	clock_gettime(CLOCK_MONOTONIC, &t_fps);
	while (machine_status) {
		clock_gettime(CLOCK_MONOTONIC, &t_start);

		av_packet_unref(&avPacket);
		sem_wait(&sem_file);
		ret = av_read_frame(avFmtCtx, &avPacket);
		if (ret < 0) {
			printf("pthread-%d:av_read_frame faild(%d:%s).\n", id, ret, av_err2str(ret));
			goto out;
		}
		sem_post(&sem_file);
		clock_gettime(CLOCK_MONOTONIC, &t_frame);

		ret = avcodec_send_packet(avCodecCtx, &avPacket);
		if (ret < 0) {
			printf("pthread-%d:avcodec_send_packet faild(%d:%s).\n", id, ret, av_err2str(ret));
			goto out;
		}

		ret = avcodec_receive_frame(avCodecCtx, &avFrame_decode);
		if (ret < 0) {
			printf("pthread-%d:avcodec_receive_frame faild(%d:%s).\n", id, ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_MONOTONIC, &t_decode);
		if (scale_context == NULL) {
			scale_context = sws_getContext(avCodecPar->width, avCodecPar->height, avFrame_decode.format, 
						fb_surface[0].height, fb_surface[0].width, AV_PIX_FMT_RGBA,
						SWS_FAST_BILINEAR/*SWS_POINT SWS_BICUBIC SWS_X SWS_AREA SWS_BICUBLIN
						SWS_GAUSS SWS_SINC SWS_LANCZOS SWS_SPLINE*/,
						NULL, NULL, NULL);
			if (!scale_context) {
				printf("sws_getContext faild.\n");
				ret = -1;
				goto out;
			}
		}
		ret = sws_scale(scale_context, (const uint8_t * const*)avFrame_decode.data, avFrame_decode.linesize,
				0, avCodecPar->height, avFrame_scale.data, avFrame_scale.linesize);
		if (ret < 0) {
			printf("pthread-%d:sws_scale faild(%d:%s).\n", id, ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_MONOTONIC, &t_scale);

		rgba_mirror_level(framebuffer_mirror, avFrame_scale.data[0], fb_surface[0].height, fb_surface[0].width);
		//usleep(30*1000);
		clock_gettime(CLOCK_MONOTONIC, &t_mirror_level);

		rgba_reverse_line(framebuffer_mirror, fb_surface[0].height, fb_surface[0].width);
		clock_gettime(CLOCK_MONOTONIC, &t_reverse_line);

		rgba_rotate_90(framebuffer_rotate, /*avFrame_scale.data[0]*/framebuffer_mirror, fb_surface[0].height, fb_surface[0].width, 1);
		clock_gettime(CLOCK_MONOTONIC, &t_rotate);

		sem_wait(&sem_fb);
		memcpy(fb_surface[fb_index].framebuffer, framebuffer_rotate, fb_surface[0].width * fb_surface[0].height * 4);
		fb_v_info.yoffset = fb_index*fb_v_info.yres;
		fb_v_info.activate = FB_ACTIVATE_VBL;
		ret = ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb_v_info);
		if (ret < 0) {
			printf("ioctl FBIOPUT_VSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
			ret = -errno;
			goto out;
		}
		if (++fb_index >= fb_surface_count) fb_index = 0;
		clock_gettime(CLOCK_MONOTONIC, &t_end);
		statistic_frame_count++;
		if (delta_clock_ms(&t_end, &t_statistic_fps) > 2000) {
			printf("total frame rate %f fps.\n", ((float)statistic_frame_count*1000)/delta_clock_ms(&t_end, &t_statistic_fps));
			statistic_frame_count = 0;
			clock_gettime(CLOCK_MONOTONIC, &t_statistic_fps);
		}
		sem_post(&sem_fb);

		frame_count++;

		if (delta_clock_ms(&t_start, &t_fps) > 5000) {
			printf("<<<pthread %d info\n", id);
			printf("total %d ms.\n", delta_clock_ms(&t_end, &t_start));
			printf("\tframe %d ms.\n", delta_clock_ms(&t_frame, &t_start));
			printf("\tdecode %d ms.\n", delta_clock_ms(&t_decode, &t_frame));
			printf("\tscale %d ms.\n", delta_clock_ms(&t_scale, &t_decode));
			printf("\tmirror %d ms.\n", delta_clock_ms(&t_mirror_level, &t_scale));
			printf("\treverse_line %d ms.\n", delta_clock_ms(&t_reverse_line, &t_mirror_level));
			printf("\trotate %d ms.\n", delta_clock_ms(&t_rotate, &t_reverse_line));
			printf("\tpushToFB %d ms.\n", delta_clock_ms(&t_end, &t_rotate));
			printf("frame rate %f fps.\n", ((float)frame_count*1000)/delta_clock_ms(&t_end, &t_fps));
			printf("pthread %d info>>>\n", id);

			clock_gettime(CLOCK_MONOTONIC, &t_fps);
			frame_count = 0;

			if (1) {
				if (fd_capture_raw >= 0) { close(fd_capture_raw); fd_capture_raw = -1; }
				fd_capture_raw = open(file_capture_raw, O_RDWR|O_TRUNC);
				if (fd_capture_raw >= 0) {
					if (write(fd_capture_raw, avPacket.data, avPacket.size) < 0) {
						printf("write %s failed(%d:%s)", file_capture_raw, errno, strerror(errno));
					} else
						printf("avPacket.data=%p, avPacket.size=%d.\n", avPacket.data, avPacket.size);
				}

				if (fd_decode_data >= 0) { close(fd_decode_data); fd_decode_data = -1; }
				fd_decode_data = open(file_decode_data, O_RDWR|O_TRUNC);
				if (fd_decode_data >= 0) {
					printf("avFrame_decode: data[0]=%p, linesize[0]=%d, width=%d, height=%d, "
							"format=%d, color_range=%d.\n",
							avFrame_decode.data[0], avFrame_decode.linesize[0],
							avFrame_decode.width, avFrame_decode.height, avFrame_decode.format,
							avFrame_decode.color_range);
					if (write(fd_decode_data, avFrame_decode.data[0],
								avFrame_decode.width*avFrame_decode.height / 4) < 0) {
						printf("write %s failed(%d:%s)", file_decode_data, errno, strerror(errno));
					}
				}

				if (fd_scale_data >= 0) { close(fd_scale_data); fd_scale_data = -1; }
				fd_scale_data = open(file_scale_data, O_RDWR|O_TRUNC);
				if (fd_scale_data >= 0) {
					printf("avFrame_scale: data[0]=%p, linesize[0]=%d, width=%d, height=%d, format=%d.\n",
							avFrame_scale.data[0], avFrame_scale.linesize[0],
							fb_surface[0].width, fb_surface[0].height, AV_PIX_FMT_RGBA); 
					if (write(fd_scale_data, avFrame_scale.data[0],
								fb_surface[0].width * fb_surface[0].height * 4) < 0) {
						printf("write %s failed(%d:%s)", file_scale_data, errno, strerror(errno));
					}
				}
			}
		}
	}

out:
	av_packet_unref(&avPacket);
	av_frame_unref(&avFrame_decode);
	if (avFrame_scale.data[0]) { av_freep(&avFrame_scale.data[0]); }
	if (avCodecCtx) { avcodec_close(avCodecCtx); avcodec_free_context(&avCodecCtx); avCodecCtx = NULL; }
	if (scale_context) { sws_freeContext(scale_context); scale_context = NULL; }
	if (framebuffer_mirror) { free(framebuffer_mirror); framebuffer_mirror = NULL;}
	if (framebuffer_rotate) { free(framebuffer_rotate); framebuffer_rotate = NULL;}

	return (void *)ret;
}
