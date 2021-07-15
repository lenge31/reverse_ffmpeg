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


static struct AVInputFormat *avInFmt = NULL;
static struct AVDictionary *format_opts = NULL;
static struct AVFormatContext *avFmtCtx = NULL;
static int avFmtCtx_stream_index[AVMEDIA_TYPE_NB];
static struct AVCodecParameters *avCodecPar = NULL;
static struct AVCodecContext *avCodecCtx = NULL;
static struct AVCodec *avCodec = NULL;
static struct SwsContext *swsContext = NULL;

static pthread_t pthread_fetch_AVPacket = 0;
static void *routine_fetch_AVPacket(void *arg);
#define QUEUE_AVPACKET_SIZE	100
struct queue_AVPacket_t {
	struct AVPacket pkt[QUEUE_AVPACKET_SIZE];
	int rIndex;
	int wIndex;
	int curSize;
	sem_t sem;
	sem_t sem_empty;
	sem_t sem_filled;
}queue_AVPacket;

static pthread_t pthread_avcodec = 0;
static void *routine_avcodec(void *arg);
#define QUEUE_AVFRAME_SIZE	100
struct queue_AVFrame_t {
	struct AVFrame frame[QUEUE_AVFRAME_SIZE];
	int rIndex;
	int wIndex;
	int curSize;
	sem_t sem;
	sem_t sem_empty;
	sem_t sem_filled;
}queue_AVFrame;

#define QUEUE_AVFRAME_SCALE_SIZE	100
static pthread_t pthread_scale = 0;
static void *routine_scale(void *arg);
struct queue_AVFrame_scale_t {
	struct AVFrame frame[QUEUE_AVFRAME_SCALE_SIZE];
	int rIndex;
	int wIndex;
	int curSize;
	sem_t sem;
	sem_t sem_empty;
	sem_t sem_filled;
}queue_AVFrame_scale;

static pthread_t pthread_render = 0;
static void *routine_render(void *arg);

static int screen_rotate = 270;
static struct fb_var_screeninfo fb_v_info;
static struct fb_fix_screeninfo fb_f_info;
static unsigned char *framebuffer_mmap = NULL;
static int framebuffer_mmap_len = 0;
struct fb_surface_t {
	unsigned char *framebuffer;
	int width;
	int height;
} *fb_surface;
static int fb_surface_count = 0;

static char file_dev[48];
static char *file_fb = "/dev/graphics/fb0";
static int fd_fb = -1;
static char *file_capture_raw = "./capture_raw";
static int fd_capture_raw = -1;
static char *file_decode_data = "./decode_data";
static int fd_decode_data = -1;
static char *file_scale_data = "./scale_data";
static int fd_scale_data = -1;
static char *file_m0032_ctrl = "/proc/m0032_ctrl";
static int fd_m0032_ctrl = -1;

static int fb_init(void);
static int rgba_rotate(unsigned char *dst, unsigned char *src, int width, int height, int angle);
static int rgba_mirror_level(unsigned char *dst, unsigned char *src, int width, int height);// __attribute__((optimize("O0")));
static int delta_clock_ms(struct timespec *a, struct timespec *b);
static int reverse_status(void);

static int machine_status = 0; // 0:normal; 1:reverse.
static char service_bootanim_exit[PROPERTY_VALUE_MAX];
static char reverse_status_value[PROPERTY_VALUE_MAX];

int main_init(int argc, char *argv[])
{
	int ret = 0, i = 0;

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
		ret = -1;
		return ret;
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
		return ret;
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
		return ret;
	}

	ret = avformat_find_stream_info(avFmtCtx, &format_opts);
	if (ret < 0) {
		printf("avformat_find_stream_info faild(%d:%s).\n", ret, av_err2str(ret));
		return ret;
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
		return ret;
	}
	ret = avcodec_parameters_to_context(avCodecCtx, avCodecPar);
	if (ret < 0) {
		printf("avcodec_parameters_to_context faild(%d:%s).\n", ret, av_err2str(ret));
		return ret;
	}

	avCodec = avcodec_find_decoder(avCodecCtx->codec_id);
	if (avCodec == NULL) {
		printf("avcodec_find_decoder faild.\n");
		ret = -1;
		return ret;
	}

	ret = avcodec_open2(avCodecCtx, avCodec, NULL);
	if (ret < 0) {
		printf("avcodec_open2 faild(%d:%s).\n", ret, av_err2str(ret));
		return ret;
	}

	ret = fb_init();
	if (ret < 0) {
		printf("fb_init failed.\n");
		return ret;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0, i = 0;

	printf("compile date-time %s-%s.\n", __DATE__, __TIME__);

	if (argc > 1) {
		strncpy(file_dev, argv[1] ,sizeof(file_dev));
	} else {
		printf("args: /dev/videoX [mjpeg/h264]\n");
		return -1;
	}

	ret = main_init(argc, argv);
	if (ret < 0) {
		printf("main_init failed.\n");
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
				if (pthread_fetch_AVPacket == 0) {
					printf("pthread_create pthread_fetch_AVPacket.\n");
					ret = pthread_create(&pthread_fetch_AVPacket, NULL, routine_fetch_AVPacket, NULL);
					if (ret != 0) {
						printf("pthread_create pthread_fetch_AVPacket failed(%d:%s)", ret, strerror(ret));
						ret = -ret;
						goto out;
					}
					memset(&queue_AVPacket, 0, sizeof(queue_AVPacket));
					ret = sem_init(&queue_AVPacket.sem, 0, 1);
					if (ret < 0) {
						printf("sem_init queue_AVPacket.sem failed(%d:%s)", errno, strerror(errno));
						ret = -errno;
						goto out;
					}
					ret = sem_init(&queue_AVPacket.sem_empty, 0, 0);
					ret = sem_init(&queue_AVPacket.sem_filled, 0, 0);
				}
				if (pthread_avcodec == 0) {
					printf("pthread_create pthread_avcodec.\n");
					ret = pthread_create(&pthread_avcodec, NULL, routine_avcodec, NULL);
					if (ret != 0) {
						printf("pthread_create pthread_avcodec failed(%d:%s)", ret, strerror(ret));
						ret = -ret;
						goto out;
					}
					memset(&queue_AVFrame, 0, sizeof(queue_AVFrame));
					ret = sem_init(&queue_AVFrame.sem, 0, 1);
					if (ret < 0) {
						printf("sem_init queue_AVFrame.sem failed(%d:%s)", errno, strerror(errno));
						ret = -errno;
						goto out;
					}
					ret = sem_init(&queue_AVFrame.sem_empty, 0, 0);
					ret = sem_init(&queue_AVFrame.sem_filled, 0, 0);
				}
				if (pthread_scale == 0) {
					printf("pthread_create pthread_avcodec.\n");
					ret = pthread_create(&pthread_scale, NULL, routine_scale, NULL);
					if (ret != 0) {
						printf("pthread_create pthread_scale failed(%d:%s)", ret, strerror(ret));
						ret = -ret;
						goto out;
					}
					memset(&queue_AVFrame_scale, 0, sizeof(queue_AVFrame_scale));
					ret = sem_init(&queue_AVFrame_scale.sem, 0, 1);
					if (ret < 0) {
						printf("sem_init queue_AVFrame_scale.sem failed(%d:%s)", errno, strerror(errno));
						ret = -errno;
						goto out;
					}
					ret = sem_init(&queue_AVFrame_scale.sem_empty, 0, 0);
					ret = sem_init(&queue_AVFrame_scale.sem_filled, 0, 0);
				}
				if (pthread_render == 0) {
					printf("pthread_create pthread_render.\n");
					ret = pthread_create(&pthread_render, NULL, routine_render, NULL);
					if (ret != 0) {
						printf("pthread_create pthread_render, failed(%d:%s)", ret, strerror(ret));
						ret = -ret;
						goto out;
					}
					printf("stop zygote.\n");
					property_set("ctl.stop", "zygote");
					printf("stop bootanim.\n");
					property_set("ctl.stop", "bootanim");
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

out:
	if (pthread_fetch_AVPacket != 0) {
		ret = pthread_kill(pthread_fetch_AVPacket, /*SIGKILL*/SIGUSR1);
		if (ret != 0) {
			printf("pthread_kill failed(%d:%s)", ret, strerror(ret));
		}
	}
	if (pthread_avcodec != 0) {
		ret = pthread_kill(pthread_avcodec, /*SIGKILL*/SIGUSR1);
		if (ret != 0) {
			printf("pthread_kill failed(%d:%s)", ret, strerror(ret));
		}
	}
	if (pthread_scale!= 0) {
		ret = pthread_kill(pthread_scale, /*SIGKILL*/SIGUSR1);
		if (ret != 0) {
			printf("pthread_kill failed(%d:%s)", ret, strerror(ret));
		}
	}
	if (pthread_render!= 0) {
		ret = pthread_kill(pthread_render, /*SIGKILL*/SIGUSR1);
		if (ret != 0) {
			printf("pthread_kill failed(%d:%s)", ret, strerror(ret));
		}
	}

	if (pthread_fetch_AVPacket != 0) {
		ret = pthread_join(pthread_fetch_AVPacket, NULL);
		if (ret != 0) {
			printf("pthread_join failed(%d:%s)", ret, strerror(ret));
		}
		pthread_fetch_AVPacket = 0;
		sem_destroy(&queue_AVPacket.sem);
		sem_destroy(&queue_AVPacket.sem_empty);
		sem_destroy(&queue_AVPacket.sem_filled);
	}
	if (pthread_avcodec != 0) {
		ret = pthread_join(pthread_avcodec, NULL);
		if (ret != 0) {
			printf("pthread_join failed(%d:%s)", ret, strerror(ret));
		}
		pthread_avcodec= 0;
		sem_destroy(&queue_AVFrame.sem);
		sem_destroy(&queue_AVFrame.sem_empty);
		sem_destroy(&queue_AVFrame.sem_filled);
	}
	if (pthread_scale!= 0) {
		ret = pthread_join(pthread_scale, NULL);
		if (ret != 0) {
			printf("pthread_join failed(%d:%s)", ret, strerror(ret));
		}
		pthread_scale= 0;
		sem_destroy(&queue_AVFrame_scale.sem);
		sem_destroy(&queue_AVFrame_scale.sem_empty);
		sem_destroy(&queue_AVFrame_scale.sem_filled);
	}
	if (pthread_render!= 0) {
		ret = pthread_join(pthread_render, NULL);
		if (ret != 0) {
			printf("pthread_join failed(%d:%s)", ret, strerror(ret));
		}
		pthread_render= 0;

		printf("start bootanim.\n");
		property_set("service.bootanim.exit", "0");
		property_set("ctl.start", "bootanim");
		printf("start zygote.\n");
		property_set("ctl.start", "zygote");
	}

	for(i=0; i<QUEUE_AVPACKET_SIZE; i++) av_packet_unref(&queue_AVPacket.pkt[i]);
	for(i=0; i<QUEUE_AVFRAME_SIZE; i++) av_frame_unref(&queue_AVFrame.frame[i]);
	for(i=0; i<QUEUE_AVFRAME_SCALE_SIZE; i++) av_frame_unref(&queue_AVFrame_scale.frame[i]);

	if (swsContext) { sws_freeContext(swsContext); swsContext = NULL; }
	if (avCodecCtx) { avcodec_close(avCodecCtx); avcodec_free_context(&avCodecCtx); avCodecCtx = NULL; }
	if (avFmtCtx) { avformat_close_input(&avFmtCtx); avFmtCtx = NULL; }
	if (format_opts) { av_dict_free(&format_opts); format_opts = NULL;}
	if (framebuffer_mmap && framebuffer_mmap != MAP_FAILED) { munmap(framebuffer_mmap,
		framebuffer_mmap_len); framebuffer_mmap = NULL;} 
	if (fb_surface) { free(fb_surface); fb_surface = NULL;}
	if (fd_capture_raw >= 0) { close(fd_capture_raw); fd_capture_raw = -1; }
	if (fd_decode_data >= 0) { close(fd_decode_data); fd_decode_data = -1; }
	if (fd_scale_data >= 0) { close(fd_scale_data); fd_scale_data = -1; }
	if (fd_fb >= 0) { close(fd_fb); fd_fb = -1; }
	if (fd_m0032_ctrl >= 0) { close(fd_m0032_ctrl); fd_m0032_ctrl = -1; }

	return ret;
}

static int reverse_status(void)
{
	char buf_tmp[64];
	int ret = 0;

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

static int rgba_mirror_level(unsigned char *dst, unsigned char *src, int width, int height)
{
	int i, j;

	for (i = 0; i < height; i++)
		for (j = 0; j < width; j++) {
			*((unsigned int *)dst + i*width + width-1-j) = *((unsigned int *)src + i*width + j);
		}

	return 0;
}

static int delta_clock_ms(struct timespec *a, struct timespec *b)
{
	return (a->tv_sec*1000+a->tv_nsec/1000000)-(b->tv_sec*1000+b->tv_nsec/1000000);
}

static void sa_handler_fetct_AVPacket(int sig)
{
	printf("%s signal %d.\n", __FUNCTION__, sig);
}

static void *routine_fetch_AVPacket(void *arg)
{
	int ret = 0;
	int tmpIndex = 0;
	int sem_value = 0;

	struct timespec t_start;
	struct timespec t_end;
	int packet_count = 0; 

	struct sigaction action;
	struct sched_param param = {.sched_priority = 90};
	int policy = -1;

	(int)arg;

	policy = sched_getscheduler(0);
	ret = sched_setscheduler(0, SCHED_RR, &param);
	if (ret < 0) {
		printf("sched_setscheduler failed(%d:%s)", errno, strerror(errno));
	}
	printf("sched_setscheduler routine_fetch_AVPacket policy %d change to %d.\n", policy, sched_getscheduler(0));

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sa_handler_fetct_AVPacket;
	sigaction(SIGUSR1, &action, NULL);

	while (machine_status) {
		if (packet_count%90 == 0) clock_gettime(CLOCK_MONOTONIC, &t_start);
		sem_wait(&queue_AVPacket.sem);
		tmpIndex = queue_AVPacket.wIndex;
		if (queue_AVPacket.curSize >= QUEUE_AVPACKET_SIZE) { //filled wait
			while (sem_getvalue(&queue_AVPacket.sem_filled, &sem_value)==0 && sem_value>0) {
				sem_wait(&queue_AVPacket.sem_filled);
			}
			sem_post(&queue_AVPacket.sem);
			printf("queue_AVPacket filled, %s wait to read.\n", __FUNCTION__);
			sem_wait(&queue_AVPacket.sem_filled);
			printf("queue_AVPacket filled, %s wait finish.\n", __FUNCTION__);
		} else
			sem_post(&queue_AVPacket.sem);

again:
		av_packet_unref(&queue_AVPacket.pkt[tmpIndex]);

		av_init_packet(&queue_AVPacket.pkt[tmpIndex]);
		ret = av_read_frame(avFmtCtx, &queue_AVPacket.pkt[tmpIndex]);
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				printf("av_read_frame to EOF.\n");
				machine_status = 0;
			}
			printf("av_read_frame faild(%d:%s).\n", ret, av_err2str(ret));
		}
		if (queue_AVPacket.pkt[tmpIndex].stream_index != avFmtCtx_stream_index[AVMEDIA_TYPE_VIDEO])
			goto again;

		sem_wait(&queue_AVPacket.sem);
		if (queue_AVPacket.wIndex == QUEUE_AVPACKET_SIZE-1) {
			queue_AVPacket.wIndex = 0;
		} else
			queue_AVPacket.wIndex++;
		queue_AVPacket.curSize++;

		if (sem_getvalue(&queue_AVPacket.sem_empty, &sem_value)==0 && sem_value<=0) {//empty release
			sem_post(&queue_AVPacket.sem_empty);
		}
		sem_post(&queue_AVPacket.sem);

		if (packet_count%90 == 0) {
			clock_gettime(CLOCK_MONOTONIC, &t_end);
			printf("fetch a packet %d ms.\n", delta_clock_ms(&t_end, &t_start));
		}
		packet_count++;
	}

	return (void *)ret;
}

static void sa_handler_avcodec(int sig)
{
	printf("%s signal %d.\n", __FUNCTION__, sig);
}

static void *routine_avcodec(void *arg)
{
	int ret = 0;
	int tmpIndex_from = 0, tmpIndex_to;
	int sem_value = 0;

	struct timespec t_start;
	struct timespec t_end;
	int packet_count = 0; 

	struct sigaction action;
	struct sched_param param = {.sched_priority = 90};
	int policy = -1;

	(int)arg;

	policy = sched_getscheduler(0);
	ret = sched_setscheduler(0, SCHED_RR, &param);
	if (ret < 0) {
		printf("sched_setscheduler failed(%d:%s)", errno, strerror(errno));
	}
	printf("sched_setscheduler routine_avcodec policy %d change to %d.\n", policy, sched_getscheduler(0));

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sa_handler_avcodec;
	sigaction(SIGUSR1, &action, NULL);

	while (machine_status) {
		if (packet_count%90 == 0) clock_gettime(CLOCK_MONOTONIC, &t_start);

		sem_wait(&queue_AVPacket.sem);
		tmpIndex_from = queue_AVPacket.rIndex;
		if (queue_AVPacket.curSize <= 0) {//empty wait
			while (sem_getvalue(&queue_AVPacket.sem_empty, &sem_value)==0 && sem_value>0) {
				sem_wait(&queue_AVPacket.sem_empty);
			}
			sem_post(&queue_AVPacket.sem);
			printf("queue_AVPacket empty, %s wait to write.\n", __FUNCTION__);
			sem_wait(&queue_AVPacket.sem_empty);
			printf("queue_AVPacket empty, %s wait finish.\n", __FUNCTION__);
		} else
			sem_post(&queue_AVPacket.sem);

		ret = avcodec_send_packet(avCodecCtx, &queue_AVPacket.pkt[tmpIndex_from]);
                if (ret < 0) {
                        printf("avcodec_send_packet faild(%d:%s).\n", ret, av_err2str(ret));
                }
		av_packet_unref(&queue_AVPacket.pkt[tmpIndex_from]);

		sem_wait(&queue_AVPacket.sem);
		if (queue_AVPacket.rIndex == QUEUE_AVPACKET_SIZE-1) {
			queue_AVPacket.rIndex = 0;
		} else
			queue_AVPacket.rIndex++;
		queue_AVPacket.curSize--;

		if (sem_getvalue(&queue_AVPacket.sem_filled, &sem_value)==0 && sem_value<=0) {//filled release
			sem_post(&queue_AVPacket.sem_filled);
		}
		sem_post(&queue_AVPacket.sem);

		//while (1) {
			sem_wait(&queue_AVFrame.sem);
			tmpIndex_to = queue_AVFrame.wIndex;
			if (queue_AVFrame.curSize >= QUEUE_AVFRAME_SIZE) {//filled wait
				while (sem_getvalue(&queue_AVFrame.sem_filled, &sem_value)==0 && sem_value>0) {
					sem_wait(&queue_AVFrame.sem_filled);
				}
				sem_post(&queue_AVFrame.sem);
				printf("queue_AVFrame filled, %s wait to read.\n", __FUNCTION__);
				sem_wait(&queue_AVFrame.sem_filled);
				printf("queue_AVFrame filled, %s wait finish.\n", __FUNCTION__);
			} else
				sem_post(&queue_AVFrame.sem);

			ret = avcodec_receive_frame(avCodecCtx, &queue_AVFrame.frame[tmpIndex_to]);
			if (ret < 0) {
				/*if (ret == AVERROR(EAGAIN)) {
					break;
				}*/
				printf("avcodec_receive_frame faild(%d:%s).\n", ret, av_err2str(ret));
			}

			sem_wait(&queue_AVFrame.sem);
			if (queue_AVFrame.wIndex == QUEUE_AVFRAME_SIZE-1)
				queue_AVFrame.wIndex = 0;
			else
				queue_AVFrame.wIndex++;
			queue_AVFrame.curSize++;
			if (sem_getvalue(&queue_AVFrame.sem_empty, &sem_value)==0 && sem_value<=0) {//empty release
				sem_post(&queue_AVFrame.sem_empty);
			}
			sem_post(&queue_AVFrame.sem);
		//}

		if (packet_count%90 == 0) {
			clock_gettime(CLOCK_MONOTONIC, &t_end);
			printf("codec a packet to frame %d ms.\n", delta_clock_ms(&t_end, &t_start));
		}
		packet_count++;
	}

	return (void *)ret;
}

static void sa_handler_scale(int sig)
{
	printf("%s signal %d.\n", __FUNCTION__, sig);
}

static void *routine_scale(void *arg)
{
	int ret = 0;
	int tmpIndex_from = 0, tmpIndex_to;
	int sem_value = 0;

	struct timespec t_start;
	struct timespec t_end;
	int frame_count = 0; 

	struct sigaction action;
	struct sched_param param = {.sched_priority = 90};
	int policy = -1;

	(int)arg;

	policy = sched_getscheduler(0);
	ret = sched_setscheduler(0, SCHED_RR, &param);
	if (ret < 0) {
		printf("sched_setscheduler failed(%d:%s)", errno, strerror(errno));
	}
	printf("sched_setscheduler routine_scale policy %d change to %d.\n", policy, sched_getscheduler(0));

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sa_handler_scale;
	sigaction(SIGUSR1, &action, NULL);

	while (machine_status) {
		if (frame_count%90 == 0) clock_gettime(CLOCK_MONOTONIC, &t_start);

		sem_wait(&queue_AVFrame.sem);
		tmpIndex_from = queue_AVFrame.rIndex;
		if (queue_AVFrame.curSize <= 0) {//empty wait
			while (sem_getvalue(&queue_AVFrame.sem_empty, &sem_value)==0 && sem_value>0) {
				sem_wait(&queue_AVFrame.sem_empty);
			}
			sem_post(&queue_AVFrame.sem);
			printf("queue_AVFrame empty, %s wait to write.\n", __FUNCTION__);
			sem_wait(&queue_AVFrame.sem_empty);
			printf("queue_AVFrame empty, %s wait finish.\n", __FUNCTION__);
		} else
			sem_post(&queue_AVFrame.sem);

                if (swsContext == NULL) {
                        swsContext = sws_getContext(avCodecPar->width, avCodecPar->height, queue_AVFrame.frame[tmpIndex_from].format,
                                        fb_surface[0].width, fb_surface[0].height, AV_PIX_FMT_RGBA,
                                        SWS_FAST_BILINEAR/*SWS_POINT SWS_BICUBIC SWS_X SWS_AREA SWS_BICUBLIN
                                                           SWS_GAUSS SWS_SINC SWS_LANCZOS SWS_SPLINE*/,
                                        NULL, NULL, NULL);
                        if (!swsContext) {
                                printf("sws_getContext faild.\n");
                        }
                }

		sem_wait(&queue_AVFrame_scale.sem);
		tmpIndex_to = queue_AVFrame_scale.wIndex;
		if (queue_AVFrame_scale.curSize >= QUEUE_AVFRAME_SCALE_SIZE) {//filled wait
			while (sem_getvalue(&queue_AVFrame_scale.sem_filled, &sem_value)==0 && sem_value>0) {
				sem_wait(&queue_AVFrame_scale.sem_filled);
			}
			sem_post(&queue_AVFrame_scale.sem);
			printf("queue_AVFrame_scale filled, %s wait to read.\n", __FUNCTION__);
			sem_wait(&queue_AVFrame_scale.sem_filled);
			printf("queue_AVFrame_scale filled, %s wait finish.\n", __FUNCTION__);
		} else
			sem_post(&queue_AVFrame_scale.sem);

		if (queue_AVFrame_scale.frame[tmpIndex_to].data[0] == NULL) {
			ret = av_image_alloc(queue_AVFrame_scale.frame[tmpIndex_to].data, queue_AVFrame_scale.frame[tmpIndex_to].linesize,
					avCodecPar->width, avCodecPar->height, AV_PIX_FMT_RGBA, 1);
			if (ret < 0) {
				printf("av_image_alloc faild(%d:%s).\n", ret, av_err2str(ret));
			}
		}

		//printf("queue_AVFrame.curSize=%d, queue_AVFrame.frame[%d].data[0]=%p, queue_AVFrame.frame[tmpIndex_from].linesize[0]=%d.\n",
		//	queue_AVFrame.curSize, tmpIndex_from, queue_AVFrame.frame[tmpIndex_from].data[0], queue_AVFrame.frame[tmpIndex_from].linesize[0]);
		ret = sws_scale(swsContext, (const uint8_t * const*)queue_AVFrame.frame[tmpIndex_from].data, queue_AVFrame.frame[tmpIndex_from].linesize,
                                0, avCodecPar->height, queue_AVFrame_scale.frame[tmpIndex_to].data, queue_AVFrame_scale.frame[tmpIndex_to].linesize);
                if (ret < 0) {
                        printf("sws_scale faild(%d:%s).\n", ret, av_err2str(ret));
                }
		av_frame_unref(&queue_AVFrame.frame[tmpIndex_from]);

		sem_wait(&queue_AVFrame.sem);
		if (queue_AVFrame.rIndex == QUEUE_AVFRAME_SIZE-1) {
			queue_AVFrame.rIndex = 0;
		} else
			queue_AVFrame.rIndex++;
		queue_AVFrame.curSize--;
		if (sem_getvalue(&queue_AVFrame.sem_filled, &sem_value)==0 && sem_value<=0) {//filled release
			sem_post(&queue_AVFrame.sem_filled);
		}
		sem_post(&queue_AVFrame.sem);

		sem_wait(&queue_AVFrame_scale.sem);
		if (queue_AVFrame_scale.wIndex == QUEUE_AVFRAME_SCALE_SIZE-1)
			queue_AVFrame_scale.wIndex = 0;
		else
			queue_AVFrame_scale.wIndex++;
		queue_AVFrame_scale.curSize++;
		if (sem_getvalue(&queue_AVFrame_scale.sem_empty, &sem_value)==0 && sem_value<=0) {//empty release
			sem_post(&queue_AVFrame_scale.sem_empty);
		}
		sem_post(&queue_AVFrame_scale.sem);

		if (frame_count%90 == 0) {
			clock_gettime(CLOCK_MONOTONIC, &t_end);
			printf("scale a frame %d ms.\n", delta_clock_ms(&t_end, &t_start));
		}
		frame_count++;
	}

	return (void *)ret;
}

static void sa_handler_render(int sig)
{
	printf("%s signal %d.\n", __FUNCTION__, sig);
}

static void *routine_render(void *arg)
{
	int ret = 0;
	static int fb_index = 0;
	int tmpIndex = 0;
	int sem_value = 0;

	struct timespec t_start;
	struct timespec t_end;
	int frame_count = 0; 

	struct sigaction action;
	struct sched_param param = {.sched_priority = 90};
	int policy = -1;

	(int)arg;

	policy = sched_getscheduler(0);
	ret = sched_setscheduler(0, SCHED_RR, &param);
	if (ret < 0) {
		printf("sched_setscheduler failed(%d:%s)", errno, strerror(errno));
	}
	printf("sched_setscheduler routine_render policy %d change to %d.\n", policy, sched_getscheduler(0));

	memset(&action, 0, sizeof(action));
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sa_handler_render;
	sigaction(SIGUSR1, &action, NULL);

	while (machine_status) {
		if (frame_count%90 == 0) clock_gettime(CLOCK_MONOTONIC, &t_start);

		sem_wait(&queue_AVFrame_scale.sem);
		tmpIndex = queue_AVFrame_scale.rIndex;
		if (queue_AVFrame_scale.curSize <= 0) {//empty wait
			while (sem_getvalue(&queue_AVFrame_scale.sem_empty, &sem_value)==0 && sem_value>0) {
				sem_wait(&queue_AVFrame_scale.sem_empty);
			}
			sem_post(&queue_AVFrame_scale.sem);
			printf("queue_AVFrame_scale empty, %s wait to write.\n", __FUNCTION__);
			sem_wait(&queue_AVFrame_scale.sem_empty);
			printf("queue_AVFrame_scale empty, %s wait finish.\n", __FUNCTION__);
		} else
			sem_post(&queue_AVFrame_scale.sem);

		rgba_rotate(fb_surface[fb_index].framebuffer, queue_AVFrame_scale.frame[tmpIndex].data[0], fb_surface[0].width, fb_surface[0].height, 360-screen_rotate);
		fb_v_info.yoffset = fb_index*fb_v_info.yres;
		fb_v_info.activate = FB_ACTIVATE_VBL;
		ret = ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb_v_info);
		if (ret < 0) {
			printf("ioctl FBIOPUT_VSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
		}
		if (++fb_index >= fb_surface_count) fb_index = 0;

		if (queue_AVFrame_scale.frame[tmpIndex].data[0] != NULL) {
			av_freep(&queue_AVFrame_scale.frame[tmpIndex].data[0]);
			queue_AVFrame_scale.frame[tmpIndex].data[0] = NULL;
		}

		sem_wait(&queue_AVFrame_scale.sem);
		if (queue_AVFrame_scale.rIndex == QUEUE_AVFRAME_SCALE_SIZE-1) {
			queue_AVFrame_scale.rIndex = 0;
		} else
			queue_AVFrame_scale.rIndex++;
		queue_AVFrame_scale.curSize--;

		if (sem_getvalue(&queue_AVFrame_scale.sem_filled, &sem_value)==0 && sem_value<=0) {//filled release
			sem_post(&queue_AVFrame_scale.sem_filled);
		}
		sem_post(&queue_AVFrame_scale.sem);

		if (frame_count%90 == 0) {
			clock_gettime(CLOCK_MONOTONIC, &t_end);
			printf("render a frame %d ms.\n", delta_clock_ms(&t_end, &t_start));
		}
		frame_count++;
	}

	return (void *)ret;
}
