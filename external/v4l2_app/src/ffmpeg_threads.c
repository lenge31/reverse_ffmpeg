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

static char *file_print = NULL; //"/dev/console";
static FILE *fd_print = NULL;
//#define printf(fmt, ...)	fprintf(fd_print, fmt, ##__VA_ARGS__)

#define CAMERA_FRAME_WIDTH	1280
#define CAMERA_FRAME_HEIGHT	720
#define CAMERA_FRAME_SIZE	"1280*720"

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
static char *file_m0032_ctrl = "/proc/m0032_ctrl";
static int fd_m0032_ctrl = -1;
static sem_t sem_from, sem_to;
static pthread_t pthread_array[3] = {0};

static int capture_frame(AVPacket *packet);
static int fb_init(void);
static int rgba_rotate_90(unsigned char *dst, unsigned char *src, int width, int height, int isClockWise);
static int rgba_mirror_level(unsigned char *dst, unsigned char *src, int width, int height);// __attribute__((optimize("O0")));
static int reverse_status(void);
static void *pthread_routine(void *arg);

int main(int argc, char *argv[])
{
	int i = 0;
	int ret = 0;

	if (file_print != NULL) {
		fd_print = fopen(file_print, "a+");
		if (fd_print == NULL) {
			printf("fopen %s failed(%d:%s)", file_print, errno, strerror(errno));
			return -errno;
		}
	}

	if (reverse_status() <= 0) {
		printf("Not car reverse status.\n");
		return 0;
	}

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
	printf("avCodecPar->codec_id=%d, avCodecPar->width=%d, avCodecPar->heigth=%d, avCodecPar->format=%d.\n",
		avCodecPar->codec_id, avCodecPar->width, avCodecPar->height, avCodecPar->format);

	avCodec = avcodec_find_decoder(avCodecPar->codec_id);
	if (avCodec == NULL) {
		printf("avcodec_find_decoder faild.\n");
		ret = -1;
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

	ret = sem_init(&sem_from, 0, 1);
	if (ret < 0) {
		printf("sem_init sem_from failed(%d:%s)", errno, strerror(errno));
		ret = -errno;
		goto out;
	}
	ret = sem_init(&sem_to, 0, 1);
	if (ret < 0) {
		printf("sem_init sem_to failed(%d:%s)", errno, strerror(errno));
		ret = -errno;
		goto out;
	}

	while (1) {
		if (reverse_status() > 0) {
			struct timespec ts_sleep = {
				.tv_sec = 1,
				.tv_nsec = 0,
			};

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

			nanosleep(&ts_sleep, NULL);
			continue;
		}
		break;
	}

	if (pthread_array[0] != 0) {
		for (i = 0; i < (int)(sizeof(pthread_array)/sizeof(pthread_t)); i++) {
			printf("pthread_kill %d.\n", i);
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
	if (fb_surface) { free(fb_surface); fb_surface = NULL;}
	if (fd_dev >= 0) { close(fd_dev); fd_dev = -1; }
	if (fd_capture_1 >= 0) { close(fd_capture_1); fd_capture_1 = -1; }
	if (fd_capture_2 >= 0) { close(fd_capture_2); fd_capture_2 = -1; }
	if (fd_capture_3 >= 0) { close(fd_capture_3); fd_capture_3 = -1; }
	if (fd_fb >= 0) { close(fd_fb); fd_fb = -1; }
	if (fd_m0032_ctrl >= 0) { close(fd_m0032_ctrl); fd_m0032_ctrl = -1; }
	if (avFmtCtx) { avformat_close_input(&avFmtCtx); avFmtCtx = NULL; }
	if (framebuffer_all && framebuffer_all != MAP_FAILED) { munmap(framebuffer_all, framebuffer_all_len); framebuffer_all = NULL;} 
	if (!fd_print) { fclose(fd_print); fd_print = NULL;}

	return ret;
}

//#define REVERSE_FAKE
static int reverse_status(void)
{
#ifdef REVERSE_FAKE
	return 1;
#else
	char buf_tmp[64];
	int ret = 0, k = 0;

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
#endif
}

static int capture_frame(AVPacket *packet)
{
	int ret = 0;
	fd_set read_fds;
	struct timeval tv = {
		.tv_sec = 3,
		.tv_usec = 0,
	};
	static int frame_count = 0;
	static struct timespec t_start;
	struct timespec t_fps;

	sem_wait(&sem_from);

	FD_ZERO(&read_fds);
	FD_SET(fd_dev, &read_fds);
	while (1) {
		ret = select(fd_dev + 1, &read_fds, NULL, NULL, &tv);
		if (ret == -1 && errno == EINTR) continue;

		if (ret == 0) {
			printf("%s select timeout.\n", file_dev);
			ret = -1;
			goto out;
		}
		break;
	}
	ret = av_read_frame(avFmtCtx, packet);
	if (ret < 0) {
		printf("av_read_frame faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}

	if (frame_count == 0) {
		clock_gettime(CLOCK_REALTIME, &t_start);
	}
	clock_gettime(CLOCK_REALTIME, &t_fps);
	if (((t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)-(t_start.tv_sec*1000000+t_start.tv_nsec/1000)) > 2*1000000) {
		printf("total capture frame rate %f fps.\n", ((float)frame_count*1000000)/((t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)-
			(t_start.tv_sec*1000000+t_start.tv_nsec/1000)));
		frame_count = 0;
	} else
		frame_count++;
out:
	sem_post(&sem_from);
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
		// to do
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

static int pushToframebuffer(unsigned char *framebuffer)
{
	static int fb_index = 0; 
	int ret = 0;
	static int frame_count = 0;
	static struct timespec t_start;
	struct timespec t_fps;

	sem_wait(&sem_to);

	memcpy(fb_surface[fb_index].framebuffer, framebuffer, fb_surface[0].width * fb_surface[0].height * 4);

	fb_v_info.yoffset = fb_index*fb_v_info.yres;
	fb_v_info.activate = FB_ACTIVATE_VBL;
	ret = ioctl(fd_fb, FBIOPUT_VSCREENINFO, &fb_v_info);
	if (ret < 0) {
		printf("ioctl FBIOPUT_VSCREENINFO %s failed(%d:%s)", file_fb, errno, strerror(errno));
		ret = -errno;
		goto out;
	}
	if (++fb_index >= fb_surface_count) fb_index = 0;

	if (frame_count == 0) {
		clock_gettime(CLOCK_REALTIME, &t_start);
	}
	clock_gettime(CLOCK_REALTIME, &t_fps);
	if (((t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)-(t_start.tv_sec*1000000+t_start.tv_nsec/1000)) > 2*1000000) {
		printf("actual frame rate %f fps.(%s/%s)\n", ((float)frame_count*1000000)/((t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)-
			(t_start.tv_sec*1000000+t_start.tv_nsec/1000)), __DATE__, __TIME__);
		frame_count = 0;
	} else
		frame_count++;

out:
	sem_post(&sem_to);
	return ret;
}

static void pthread_handler_exit(int sig)
{
	printf("%s signal %d.\n", __FUNCTION__, sig);
	pthread_exit(0);
}

static void *pthread_routine(void *arg)
{
	int id = (int)arg;
	int ret = 0;

	struct timespec t_start;
	struct timespec t_fps;
	struct timespec t_frame;
	struct timespec t_decode;
	struct timespec t_scale;
	struct timespec t_rotate;
	struct timespec t_mirror_level;
	struct timespec t_end;

	int frame_count = 0, k = 0;

	struct AVPacket *avPacket = NULL;
	struct AVFrame *avFrame_src = NULL;
	struct AVFrame *avFrame_dst = NULL;
	struct AVCodecContext *avCodecCtx = NULL;
	struct SwsContext *scale_context = NULL;

	unsigned char *framebuffer_mirror = NULL;
	unsigned char *framebuffer_rotate = NULL;

	struct sigaction action;
	struct sched_param param = {.sched_priority = 90};
	int policy = -1;

	policy = sched_getscheduler(0);
	ret = sched_setscheduler(0, SCHED_RR, &param);
	if (ret < 0) {
		printf("sched_setscheduler failed(%d:%s)", errno, strerror(errno));
		//goto out;
	}
	printf("pthread %d scheduler policy %d change to %d.\n", id, policy, sched_getscheduler(0));

	avPacket = (struct AVPacket *)av_malloc(sizeof(struct AVPacket));
	avFrame_src = av_frame_alloc();
	avFrame_dst = av_frame_alloc();
	if (!avPacket || !avFrame_src || !avFrame_dst) {
		printf("pthread-%d:alloc faild.\n", id);
		ret = -ENOMEM;
		goto out;
	}
	ret = av_image_alloc(avFrame_dst->data, avFrame_dst->linesize, fb_surface[0].height, fb_surface[0].width,
				fb_surface[0].pixel_format, 1);
	if (ret < 0) {
		printf("dst av_image_alloc faild(%d:%s).\n", ret, av_err2str(ret));
		goto out;
	}
	av_init_packet(avPacket);

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
	action.sa_handler = pthread_handler_exit;
	sigaction(SIGUSR1, &action, NULL);

	clock_gettime(CLOCK_REALTIME, &t_fps);
	while (1) {
		clock_gettime(CLOCK_REALTIME, &t_start);
		frame_count++;

		//sem_wait(&sem_from);
		ret = capture_frame(avPacket);
		if (ret < 0) {
			printf("pthread-%d:capture_frame failed.\n", id);
			goto out;
		}
		clock_gettime(CLOCK_REALTIME, &t_frame);

		ret = avcodec_send_packet(avCodecCtx, avPacket);
		if (ret < 0) {
			printf("pthread-%d:avcodec_send_packet faild(%d:%s).\n", id, ret, av_err2str(ret));
			goto out;
		}
		//sem_post(&sem_from);
		ret = avcodec_receive_frame(avCodecCtx, avFrame_src);
		if (ret < 0) {
			printf("pthread-%d:avcodec_receive_frame faild(%d:%s).\n", id, ret, av_err2str(ret));
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
			printf("pthread-%d:sws_scale faild(%d:%s).\n", id, ret, av_err2str(ret));
			goto out;
		}
		clock_gettime(CLOCK_REALTIME, &t_scale);

		rgba_mirror_level(framebuffer_mirror, avFrame_dst->data[0], fb_surface[0].height, fb_surface[0].width);
		//usleep(30*1000);
		clock_gettime(CLOCK_REALTIME, &t_mirror_level);

		rgba_rotate_90(framebuffer_rotate, /*avFrame_dst->data[0]*/framebuffer_mirror, fb_surface[0].height, fb_surface[0].width, 1);
		clock_gettime(CLOCK_REALTIME, &t_rotate);

		pushToframebuffer(framebuffer_rotate);

		clock_gettime(CLOCK_REALTIME, &t_end);
		av_packet_unref(avPacket);

		if (((t_start.tv_sec*1000000+t_start.tv_nsec/1000)-(t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)) > 5*1000000) {
			printf("<<<pthread %d info\n", id);
			printf("Total %f ms.\n", ((float)((t_end.tv_sec*1000000+t_end.tv_nsec/1000)-
				(t_start.tv_sec*1000000+t_start.tv_nsec/1000)))/1000);
			printf("\tframe %f ms.\n", ((float)((t_frame.tv_sec*1000000+t_frame.tv_nsec/1000)-
				(t_start.tv_sec*1000000+t_start.tv_nsec/1000)))/1000);
			printf("\tdecode %f ms.\n", ((float)((t_decode.tv_sec*1000000+t_decode.tv_nsec/1000)-
				(t_frame.tv_sec*1000000+t_frame.tv_nsec/1000)))/1000);
			printf("\tscale %f ms.\n", ((float)((t_scale.tv_sec*1000000+t_scale.tv_nsec/1000)-
				(t_decode.tv_sec*1000000+t_decode.tv_nsec/1000)))/1000);
			printf("\tmirror %f ms.\n", ((float)((t_mirror_level.tv_sec*1000000+t_mirror_level.tv_nsec/1000)-
				(t_scale.tv_sec*1000000+t_scale.tv_nsec/1000)))/1000);
			printf("\trotate %f ms.\n", ((float)((t_rotate.tv_sec*1000000+t_rotate.tv_nsec/1000)-
				(t_mirror_level.tv_sec*1000000+t_mirror_level.tv_nsec/1000)))/1000);
			printf("\tpushToFB %f ms.\n", ((float)((t_end.tv_sec*1000000+t_end.tv_nsec/1000)-
				(t_rotate.tv_sec*1000000+t_rotate.tv_nsec/1000)))/1000);
			printf("frame rate %f fps.\n", ((float)frame_count*1000000)/((t_end.tv_sec*1000000+t_end.tv_nsec/1000)-
				(t_fps.tv_sec*1000000+t_fps.tv_nsec/1000)));
			printf("pthread %d info>>>\n", id);
			clock_gettime(CLOCK_REALTIME, &t_fps);

			k--;
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
	if (avPacket) { av_packet_unref(avPacket); av_free(avPacket); avPacket = NULL; }
	if (avFrame_src) { av_frame_free(&avFrame_src); avFrame_src = NULL; }
	if (avFrame_dst && avFrame_dst->data[0]) { av_freep(&avFrame_dst->data[0]); }
	if (avFrame_dst) { av_frame_free(&avFrame_dst); avFrame_dst = NULL; }
	if (avCodecCtx) { avcodec_close(avCodecCtx); avcodec_free_context(&avCodecCtx); avCodecCtx = NULL; }
	if (scale_context) { sws_freeContext(scale_context); scale_context = NULL; }
	if (framebuffer_mirror) { free(framebuffer_mirror); framebuffer_mirror = NULL;}
	if (framebuffer_rotate) { free(framebuffer_rotate); framebuffer_rotate = NULL;}

	return (void *)ret;
}
/*
root@m0032:/ # /system/xbin/v4l2/ffmpeg_threads_mirror /dev/video0                            <
---------
Input #0, video4linux2,v4l2, from '/dev/video0':
  Duration: N/A, bitrate: N/A
    Stream #0:0: Video: mjpeg, none, 1280x720, 25 fps, 25 tbr, 1000k tbn
---------
avCodecPar->codec_id=7, avCodecPar->width=1280, avCodecPar->heigth=720, avCodecPar->format=-1.
#########
fb_fix_screeninfo:
	id	sprdfb
	smem_start	0x9ed74000
	smem_len	6144000
	visual	0x2
	ypanstep	1
	line_length	1600
fb_var_screeninfo:
	xres	400
	yres	1280
	xres_virtual	400
	yres_virtual	3840
	xoffset	0
	yoffset	1280
	bits_per_pixel	32
	grayscale	0
	red_bitfield:
		offset	0
		length	8
		msb_right	0
	green_bitfield:
		offset	8
		length	8
		msb_right	0
	blue_bitfield:
		offset	16
		length	8
		msb_right	0
	transp_bitfield:
		offset	24
		length	0
		msb_right	0
	activate	0x10
	height	96mm
	width	54mm
	pixclock	32552ps
	rotate	0
	colorspace	0
#########
framebuffer_all=0xb1f84000, fb_surface_count=3.
pthread_create 0.
pthread_create 1.
pthread_create 2.
stop bootanim.
stop zygote.
pthread 2 scheduler policy 0 change to 2.
pthread 1 scheduler policy 0 change to 2.
pthread 0 scheduler policy 0 change to 2.
[swscaler @ 0xb15e4000] deprecated pixel format used, make sure you did set range correctly
[swscaler @ 0xb1625000] deprecated pixel format used, make sure you did set range correctly
[swscaler @ 0xb0382000] deprecated pixel format used, make sure you did set range correctly
total capture frame rate 24.046495 fps.
actual frame rate 25.279751 fps.(May  9 2018/11:28:47)
total capture frame rate 24.025625 fps.
actual frame rate 24.027052 fps.(May  9 2018/11:28:47)
<<<pthread 0 info
Total 125.182999 ms.
	frame 21.881001 ms.
	decode 21.087999 ms.
	scale 42.785999 ms.
	mirror 7.629000 ms.
	rotate 29.694000 ms.
	pushToFB 2.105000 ms.
frame rate 7.744834 fps.
pthread 0 info>>>
<<<pthread 1 info
Total 123.991997 ms.
	frame 23.010000 ms.
	decode 20.843000 ms.
	scale 43.487999 ms.
	mirror 6.805000 ms.
	rotate 27.709999 ms.
	pushToFB 2.136000 ms.
frame rate 7.494661 fps.
pthread 1 info>>>
<<<pthread 2 info
Total 140.044998 ms.
	frame 8.392000 ms.
	decode 27.434999 ms.
	scale 54.352001 ms.
	mirror 8.728000 ms.
	rotate 38.390999 ms.
	pushToFB 2.747000 ms.
frame rate 7.771083 fps.
pthread 2 info>>>
total capture frame rate 23.919701 fps.
actual frame rate 23.560955 fps.(May  9 2018/11:28:47)
total capture frame rate 23.797024 fps.
actual frame rate 23.797718 fps.(May  9 2018/11:28:47)
<<<pthread 0 info
Total 102.844002 ms.
	frame 0.122000 ms.
	decode 21.271000 ms.
	scale 42.389000 ms.
	mirror 7.263000 ms.
	rotate 29.448999 ms.
	pushToFB 2.350000 ms.
frame rate 8.375084 fps.
pthread 0 info>>>
<<<pthread 1 info
Total 130.921005 ms.
	frame 25.848000 ms.
	decode 21.087999 ms.
	scale 43.366001 ms.
	mirror 7.568000 ms.
	rotate 30.884001 ms.
	pushToFB 2.167000 ms.
frame rate 8.330174 fps.
pthread 1 info>>>
<<<pthread 2 info
Total 131.561005 ms.
	frame 0.153000 ms.
	decode 27.740000 ms.
	scale 53.558998 ms.
	mirror 8.758000 ms.
	rotate 38.513000 ms.
	pushToFB 2.838000 ms.
frame rate 7.300991 fps.
pthread 2 info>>>
total capture frame rate 23.867756 fps.
actual frame rate 23.870325 fps.(May  9 2018/11:28:47)
total capture frame rate 23.948572 fps.
actual frame rate 23.913263 fps.(May  9 2018/11:28:47)
total capture frame rate 23.985428 fps.
actual frame rate 23.670719 fps.(May  9 2018/11:28:47)
<<<pthread 0 info
Total 102.508003 ms.
	frame 0.122000 ms.
	decode 20.965000 ms.
	scale 42.450001 ms.
	mirror 7.172000 ms.
	rotate 29.541000 ms.
	pushToFB 2.258000 ms.
frame rate 8.410327 fps.
pthread 0 info>>>
<<<pthread 1 info
Total 123.778999 ms.
	frame 17.974001 ms.
	decode 20.722000 ms.
	scale 42.571999 ms.
	mirror 7.721000 ms.
	rotate 31.128000 ms.
	pushToFB 3.662000 ms.
frame rate 8.405774 fps.
pthread 1 info>>>
<<<pthread 2 info
Total 147.705002 ms.
	frame 0.153000 ms.
	decode 27.160999 ms.
	scale 53.344002 ms.
	mirror 8.850000 ms.
	rotate 41.993000 ms.
	pushToFB 16.204000 ms.
frame rate 7.223358 fps.
pthread 2 info>>>
total capture frame rate 23.795938 fps.
actual frame rate 23.785034 fps.(May  9 2018/11:28:47)
total capture frame rate 23.897602 fps.
actual frame rate 23.625443 fps.(May  9 2018/11:28:47)
<<<pthread 0 info
Total 127.227997 ms.
	frame 19.316999 ms.
	decode 21.027000 ms.
	scale 42.450001 ms.
	mirror 7.660000 ms.
	rotate 30.457001 ms.
	pushToFB 6.317000 ms.
frame rate 8.365833 fps.
pthread 0 info>>>
total capture frame rate 23.999628 fps.
<<<pthread 1 info
Total 126.679001 ms.
	frame 22.187000 ms.
	decode 21.025999 ms.
	scale 43.762001 ms.
	mirror 7.721000 ms.
	rotate 29.877001 ms.
	pushToFB 2.106000 ms.
frame rate 8.401072 fps.
pthread 1 info>>>
<<<pthread 2 info
Total 133.757996 ms.
	frame 0.122000 ms.
	decode 26.976999 ms.
	scale 54.902000 ms.
	mirror 8.819000 ms.
	rotate 40.070000 ms.
	pushToFB 2.868000 ms.
frame rate 7.283440 fps.
pthread 2 info>>>
actual frame rate 23.745636 fps.(May  9 2018/11:28:47)
total capture frame rate 23.973600 fps.
actual frame rate 23.868521 fps.(May  9 2018/11:28:47)
total capture frame rate 23.971104 fps.
actual frame rate 24.358017 fps.(May  9 2018/11:28:47)
<<<pthread 0 info
Total 141.479004 ms.
	frame 16.937000 ms.
	decode 20.903999 ms.
	scale 42.480999 ms.
	mirror 7.568000 ms.
	rotate 33.355999 ms.
	pushToFB 20.233000 ms.
frame rate 8.377543 fps.
pthread 0 info>>>
<<<pthread 1 info
Total 129.028000 ms.
	frame 24.841000 ms.
	decode 21.118000 ms.
	scale 43.273998 ms.
	mirror 7.874000 ms.
	rotate 29.815001 ms.
	pushToFB 2.106000 ms.
frame rate 8.397501 fps.
pthread 1 info>>>
<<<pthread 2 info
Total 133.667007 ms.
	frame 0.152000 ms.
	decode 28.229000 ms.
	scale 53.497002 ms.
	mirror 8.789000 ms.
	rotate 40.131001 ms.
	pushToFB 2.869000 ms.
frame rate 7.254921 fps.
pthread 2 info>>>
total capture frame rate 24.189217 fps.
actual frame rate 23.902941 fps.(May  9 2018/11:28:47)
total capture frame rate 23.908632 fps.
actual frame rate 23.746683 fps.(May  9 2018/11:28:47)

root@m0032:/ # top -t -m 15 -s rss
  PID   TID PR CPU% S     VSS     RSS PCY UID      Thread          Proc
 3220  3220  1   0% S 124324K  88940K  fg root     ffmpeg_threads_ /system/xbin/v4l2/ffmpeg_threads_mirror
 3220  3225  0  24% R 124324K  88940K  fg root     ffmpeg_threads_ /system/xbin/v4l2/ffmpeg_threads_mirror
 3220  3224  2  21% R 124324K  88940K  fg root     ffmpeg_threads_ /system/xbin/v4l2/ffmpeg_threads_mirror
 3220  3223  3  22% R 124324K  88940K  fg root     ffmpeg_threads_ /system/xbin/v4l2/ffmpeg_threads_mirror
 
 root@m0032:/ # ps -p -P -t | grep ffmpeg                                       
USER      PID   PPID  VSIZE  RSS  PRIO  NICE  RTPRI SCHED  PCY WCHAN            PC  NAME
root      3220  3144  124324 89040 20    0     0     0     fg  hrtimer_na b5f31894 S /system/xbin/v4l2/ffmpeg_threads_mirror
root      3223  3220  124324 89040 -91   0     90    2     fg  dispc_sync b5f30e40 S ffmpeg_threads_
root      3224  3220  124324 89040 -91   0     90    2     fg           0 b6132dea R ffmpeg_threads_
root      3225  3220  124324 89040 -91   0     90    2     fg           0 b6ace750 R ffmpeg_threads_
*/
