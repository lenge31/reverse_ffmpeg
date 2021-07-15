/*
 * write by lenge_afar@qq.com
 */

#include <linux/videodev2.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include <time.h>

static int fd_dev = -1;
static struct v4l2_capability v4l2_cap_capture;
static struct v4l2_fmtdesc v4l2_fmtdesc_capture;
static struct v4l2_format v4l2_fmt_capture;
static struct v4l2_cropcap v4l2_cropcap_capture;
static struct v4l2_crop v4l2_crop_capture;

#define frame_bufs_COUNT	3
static struct v4l2_requestbuffers v4l2_reqbuf_capture;
static struct v4l2_buffer v4l2_buf_capture;
struct frame_buf {
	void *addr;
	unsigned int length;
};
static struct frame_buf frame_bufs[frame_bufs_COUNT];

#define PICTURE_COUNT	2
static int fd_frame = -1;
#define VIDEO_SECONDS	5
static int fd_video = -1;

int main(int argc, char *argv[])
{
	int i, ret = 0, actual_size;
	int frame_count = 0;
	enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (argc < 2) {
		printf("please input arg, like /dev/video0 [mjpeg/yuyv/h264]\n");
		return -1;
	}
	
	fd_dev = open(argv[1], O_RDWR);
	if (fd_dev == -1){
		ret = -1;
		printf("open %s failed(%d:%s).\n", argv[1], errno, strerror(errno));
		goto out;
	}

	ret = ioctl(fd_dev, VIDIOC_QUERYCAP, &v4l2_cap_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_QUERYCAP failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}
	printf("Driver:\t\t%s\nCard:\t\t%s\nBus info:\t%s\nVersion:\t%u.%u.%u\nCapabilities:\t0x%x\ndevice_caps:\t0x%x\n\n",
		v4l2_cap_capture.driver, v4l2_cap_capture.card, v4l2_cap_capture.bus_info, (v4l2_cap_capture.version>>16)&0xFF,
		(v4l2_cap_capture.version>>8)&0xFF, (v4l2_cap_capture.version)&0xFF, v4l2_cap_capture.capabilities,
		v4l2_cap_capture.device_caps);

	v4l2_fmtdesc_capture.index = 0,
	v4l2_fmtdesc_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	printf("VIDIOC_ENUM_FMT:\n");
	while(ioctl(fd_dev, VIDIOC_ENUM_FMT, &v4l2_fmtdesc_capture) != -1) {
		printf("\tindex:%d, Type:0x%x, Flags:0x%x, Description:%s, Pixelformat:0x%x\n",
			v4l2_fmtdesc_capture.index, v4l2_fmtdesc_capture.type, v4l2_fmtdesc_capture.flags,
			v4l2_fmtdesc_capture.description, v4l2_fmtdesc_capture.pixelformat);

		v4l2_fmtdesc_capture.index++;
	}
	printf("\n");

	memset(&v4l2_cropcap_capture, 0, sizeof(v4l2_cropcap_capture));
	v4l2_cropcap_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_dev, VIDIOC_CROPCAP, &v4l2_cropcap_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_CROPCAP failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}
	printf("v4l2_cropcap_capture.defrect.left=%d, top=%d, width=%d, height=%d.\n", v4l2_cropcap_capture.defrect.left,
			v4l2_cropcap_capture.defrect.top, v4l2_cropcap_capture.defrect.width, v4l2_cropcap_capture.defrect.height);

	memset(&v4l2_crop_capture, 0, sizeof(v4l2_crop_capture));
	v4l2_crop_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_crop_capture.c = v4l2_cropcap_capture.defrect;
	//ret = ioctl(fd_dev, VIDIOC_S_CROP, &v4l2_crop_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_S_CROP failed(%d:%s).\n", errno, strerror(errno));
		//goto out;
	}

	memset(&v4l2_fmt_capture, 0, sizeof(v4l2_fmt_capture));
	v4l2_fmt_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	ret = ioctl(fd_dev, VIDIOC_G_FMT, &v4l2_fmt_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_G_FMT failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}
	printf("\ndefalut:width=%d, height=%d, pixelformat=0x%x, field=0x%x, bytesperline=%d," 
		"\n\tsizeimage=%d, colorspace=%d, priv=0x%x.\n\n",
		v4l2_fmt_capture.fmt.pix.width, v4l2_fmt_capture.fmt.pix.height, v4l2_fmt_capture.fmt.pix.pixelformat,
		v4l2_fmt_capture.fmt.pix.field, v4l2_fmt_capture.fmt.pix.bytesperline, v4l2_fmt_capture.fmt.pix.sizeimage,
		v4l2_fmt_capture.fmt.pix.colorspace, v4l2_fmt_capture.fmt.pix.priv);

	if (argc > 2) {
		//v4l2_fmt_capture.fmt.pix.width = 640;
		//v4l2_fmt_capture.fmt.pix.height = 480;
		if (strcmp(argv[2], "mjpeg") == 0)
			v4l2_fmt_capture.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
		else if (strcmp(argv[2], "yuyv") == 0) {
			v4l2_fmt_capture.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
			//v4l2_fmt_capture.fmt.pix.field = V4L2_FIELD_INTERLACED;
		} else if (strcmp(argv[2], "h264") == 0)
			v4l2_fmt_capture.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
		else
			v4l2_fmt_capture.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	} else
		v4l2_fmt_capture.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	ret = ioctl(fd_dev, VIDIOC_S_FMT, &v4l2_fmt_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_S_FMT failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}

	memset(&v4l2_fmt_capture, 0, sizeof(v4l2_fmt_capture));
	v4l2_fmt_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_dev, VIDIOC_G_FMT, &v4l2_fmt_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_G_FMT failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}
	printf("set:width=%d, height=%d, pixelformat=0x%x, field=0x%x, bytesperline=%d," 
			"\n\tsizeimage=%d, colorspace=%d, priv=0x%x.\n\n",
			v4l2_fmt_capture.fmt.pix.width, v4l2_fmt_capture.fmt.pix.height, v4l2_fmt_capture.fmt.pix.pixelformat,
			v4l2_fmt_capture.fmt.pix.field, v4l2_fmt_capture.fmt.pix.bytesperline, v4l2_fmt_capture.fmt.pix.sizeimage,
			v4l2_fmt_capture.fmt.pix.colorspace, v4l2_fmt_capture.fmt.pix.priv);

	v4l2_reqbuf_capture.count = frame_bufs_COUNT;
	v4l2_reqbuf_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v4l2_reqbuf_capture.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd_dev, VIDIOC_REQBUFS, &v4l2_reqbuf_capture);
	if (ret == -1) {
		printf("ioctl VIDIOC_REQBUFS failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}
	printf("v4l2_reqbuf_capture.count=%d.\n", v4l2_reqbuf_capture.count);
	
	i = 0;
	memset(frame_bufs, 0, sizeof(frame_bufs));
	memset(&v4l2_buf_capture, 0, sizeof(v4l2_buf_capture));
	while (i < frame_bufs_COUNT) {
		v4l2_buf_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf_capture.memory = V4L2_MEMORY_MMAP;
		v4l2_buf_capture.index = i;
		ret = ioctl(fd_dev, VIDIOC_QUERYBUF, &v4l2_buf_capture);
		if (ret == -1) {
			printf("ioctl VIDIOC_QUERYBUF failed(%d:%s).\n", errno, strerror(errno));
			goto out;
		}

		frame_bufs[i].length = v4l2_buf_capture.length;
		frame_bufs[i].addr = mmap(NULL, v4l2_buf_capture.length, PROT_READ|PROT_WRITE, MAP_SHARED,
						fd_dev, v4l2_buf_capture.m.offset);
		if (!frame_bufs[i].addr || frame_bufs[i].addr == MAP_FAILED) {
			ret = -1;
			printf("mmap failed(%d:%s).\n", errno, strerror(errno));
			goto out;
		}
		printf("\nframe_bufs[%d].addr=%p, frame_bufs[%d].length=%d, v4l2_buf_capture.m.offset=%d.\n",
			i, frame_bufs[i].addr, i, frame_bufs[i].length, v4l2_buf_capture.m.offset);

		ret = ioctl(fd_dev, VIDIOC_QBUF, &v4l2_buf_capture);
		if (ret == -1) {
			printf("ioctl VIDIOC_QBUF failed(%d:%s).\n", errno, strerror(errno));
			goto out;
		}

		i++;
		memset(&v4l2_buf_capture, 0, sizeof(v4l2_buf_capture));
	}

	ret = ioctl(fd_dev, VIDIOC_STREAMON, &buf_type);
	if (ret == -1) {
		printf("ioctl VIDIOC_STREAMON failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}

	printf("---------capure picture\n");
	i = 0;
	while (i < PICTURE_COUNT) {
		char file_name[32];
		fd_set read_fds;
		struct timeval tv = {
			.tv_sec = 3,
			.tv_usec = 0,
		};

		FD_ZERO(&read_fds);
		FD_SET(fd_dev, &read_fds);

		ret = select(fd_dev + 1, &read_fds, NULL, NULL, &tv);
		if (ret == -1 && errno == EINTR) continue;

		if (ret == 0) {
			printf("select timeout.\n");
			ret = -1;
			break;
		}

		v4l2_buf_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2_buf_capture.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(fd_dev, VIDIOC_DQBUF, &v4l2_buf_capture);
		if (ret == -1) {
			printf("ioctl VIDIOC_DQBUF failed(%d:%s).\n", errno, strerror(errno));
			goto out;
		}

		if (v4l2_fmt_capture.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
			snprintf(file_name, sizeof(file_name), "./frame_%d.yuyv", i);
		if (v4l2_fmt_capture.fmt.pix.pixelformat == V4L2_PIX_FMT_H264)
			snprintf(file_name, sizeof(file_name), "./frame_%d.h264", i);
		else
			snprintf(file_name, sizeof(file_name), "./frame_%d.mjpeg", i);
		fd_frame = open(file_name, O_CREAT|O_RDWR|O_TRUNC, (S_IRWXU&~S_IXUSR)|(S_IRWXG&~S_IXGRP));
		if (fd_frame == -1) {
			ret = -1;
			printf("open %s failed(%d:%s).\n", file_name, errno, strerror(errno));
			goto out;
		}

		printf("Write fd_frame %d, v4l2_buf_capture.index=%d, v4l2_buf_capture.bytesused=%d.\n",
			 i, v4l2_buf_capture.index, v4l2_buf_capture.bytesused);
		ret = write(fd_frame, frame_bufs[v4l2_buf_capture.index].addr, v4l2_buf_capture.bytesused);
		if (ret == -1) {
			printf("write fd_frame failed(%d:%s).\n", errno, strerror(errno));
			goto out;
		}
		if (fd_frame >= 0) { close(fd_frame); fd_frame = -1; }

		ret = ioctl(fd_dev, VIDIOC_QBUF, &v4l2_buf_capture);
		if (ret == -1) {
			printf("ioctl VIDIOC_QBUF failed(%d:%s).\n", errno, strerror(errno));
			goto out;
		}

		memset(&v4l2_buf_capture, 0, sizeof(v4l2_buf_capture));
		i++;
	}
	printf("---------capure picture end\n");

	if (VIDEO_SECONDS) {
		struct timespec tv1, tv2;
		char file_name[32];

		printf("---------capure video\n");

		if (v4l2_fmt_capture.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
			snprintf(file_name, sizeof(file_name), "./video.yuyv");
		if (v4l2_fmt_capture.fmt.pix.pixelformat == V4L2_PIX_FMT_H264)
			snprintf(file_name, sizeof(file_name), "./video.h264");
		else
			snprintf(file_name, sizeof(file_name), "./video.mjpeg");
		fd_video = open(file_name, O_CREAT|O_RDWR, (S_IRWXU&~S_IXUSR)|(S_IRWXG&~S_IXGRP));
		if (fd_dev == -1) {
			ret = -1;
			printf("open %s failed(%d:%s).\n", file_name, errno, strerror(errno));
			goto out;
		}

		frame_count = 0;
		clock_gettime(CLOCK_REALTIME, &tv1);
		tv2 = tv1;
		while (tv2.tv_sec - tv1.tv_sec < VIDEO_SECONDS) {
			fd_set read_fds;
			struct timeval tv = {
				.tv_sec = 3,
				.tv_usec = 0,
			};

			FD_ZERO(&read_fds);
			FD_SET(fd_dev, &read_fds);

			ret = select(fd_dev + 1, &read_fds, NULL, NULL, &tv);
			if (ret == -1 && errno == EINTR) continue;

			if (ret == 0) {
				printf("select timeout.\n");
				ret = -1;
				break;
			}

			v4l2_buf_capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			v4l2_buf_capture.memory = V4L2_MEMORY_MMAP;
			ret = ioctl(fd_dev, VIDIOC_DQBUF, &v4l2_buf_capture);
			if (ret == -1) {
				printf("ioctl VIDIOC_DQBUF failed(%d:%s).\n", errno, strerror(errno));
				goto out;
			}

			ret = write(fd_video, frame_bufs[v4l2_buf_capture.index].addr, v4l2_buf_capture.bytesused);
			if (ret == -1) {
				printf("write fd_video failed(%d:%s).\n", errno, strerror(errno));
				goto out;
			}

			frame_count++;
			if (tv2.tv_sec - tv1.tv_sec > 1) {
				static int ii = 0;
				if (frame_count-ii>10) {
					printf("frame rate = %f.\n\n",
						(float)frame_count*1000/((tv2.tv_sec*1000+(float)tv2.tv_nsec/1000000)-
						(tv1.tv_sec*1000+(float)tv1.tv_nsec/1000000)));
					ii = frame_count;
				}
			}

			ret = ioctl(fd_dev, VIDIOC_QBUF, &v4l2_buf_capture);
			if (ret == -1) {
				printf("ioctl VIDIOC_QBUF failed(%d:%s).\n", errno, strerror(errno));
				goto out;
			}

			memset(&v4l2_buf_capture, 0, sizeof(v4l2_buf_capture));

			clock_gettime(CLOCK_REALTIME, &tv2);
		}
		printf("---------capure video end\n");
	}

	ret = ioctl(fd_dev, VIDIOC_STREAMOFF, &buf_type);
	if (ret == -1) {
		printf("ioctl VIDIOC_STREAMOFF failed(%d:%s).\n", errno, strerror(errno));
		goto out;
	}

out:
	if (fd_dev >= 0) { close(fd_dev); fd_dev = -1; }
	if (fd_frame >= 0) { close(fd_frame); fd_frame = -1; }
	if (fd_video >= 0) { close(fd_video); fd_video = -1; }
	i = 0;
	while (i < frame_bufs_COUNT) {
		if (frame_bufs[i].addr != NULL && frame_bufs[i].addr != MAP_FAILED) {
			munmap(frame_bufs[i].addr, frame_bufs[i].length);
			frame_bufs[i].addr = NULL;
			frame_bufs[i].length = 0;
		}
		i++;
	}
	return ret;
}
/*
root@m0032:/data/v4l2 # ./v4l2_app /dev/video0                                 
Driver:         uvcvideo
Card:           H264 REAR CAMERA XJ
Bus info:       usb-20200000.usb-1.1.1
Version:        3.10.65
Capabilities:   0x84000001
device_caps:    0x4000001

VIDIOC_ENUM_FMT:
        index:0, Type:0x1, Flags:0x1, Description:MJPEG, Pixelformat:0x47504a4d
        index:1, Type:0x1, Flags:0x0, Description:YUV 4:2:2 (YUYV), Pixelformat:0x56595559

v4l2_cropcap_capture.defrect.left=0, top=0, width=1280, height=720.

defalut:width=1280, height=720, pixelformat=0x47504a4d, field=0x1, bytesperline=0,
        sizeimage=1843789, colorspace=0, priv=0x0.

set:width=1280, height=720, pixelformat=0x47504a4d, field=0x1, bytesperline=0,
        sizeimage=1843789, colorspace=0, priv=0x0.

v4l2_reqbuf_capture.count=3.

frame_bufs[0].addr=0xb6bbd000, frame_bufs[0].length=1843789, v4l2_buf_capture.m.offset=0.

frame_bufs[1].addr=0xb69fa000, frame_bufs[1].length=1843789, v4l2_buf_capture.m.offset=1847296.

frame_bufs[2].addr=0xb6837000, frame_bufs[2].length=1843789, v4l2_buf_capture.m.offset=3694592.
---------capure picture
Write fd_frame 0, v4l2_buf_capture.index=0, v4l2_buf_capture.bytesused=95320.
Write fd_frame 1, v4l2_buf_capture.index=1, v4l2_buf_capture.bytesused=91624.
---------capure picture end
---------capure video
frame rate = 24.038462.

frame rate = 23.437500.

frame rate = 23.065475.

frame rate = 24.796196.

frame rate = 24.305555.

frame rate = 23.941532.

frame rate = 24.356617.

---------capure video end

root@m0032:/data/v4l2 # ./v4l2_app /dev/video1                                 
Driver:         uvcvideo
Card:           H264 REAR CAMERA XJ
Bus info:       usb-20200000.usb-1.1.1
Version:        3.10.65
Capabilities:   0x84000001
device_caps:    0x4000001

VIDIOC_ENUM_FMT:
        index:0, Type:0x1, Flags:0x1, Description:H.264, Pixelformat:0x34363248

v4l2_cropcap_capture.defrect.left=0, top=0, width=1280, height=720.

defalut:width=1280, height=720, pixelformat=0x34363248, field=0x1, bytesperline=2560,
        sizeimage=0, colorspace=8, priv=0x0.

set:width=1280, height=720, pixelformat=0x34363248, field=0x1, bytesperline=2560,
        sizeimage=1843200, colorspace=8, priv=0x0.

v4l2_reqbuf_capture.count=3.

frame_bufs[0].addr=0xb6bbe000, frame_bufs[0].length=1843200, v4l2_buf_capture.m.offset=0.

frame_bufs[1].addr=0xb69fc000, frame_bufs[1].length=1843200, v4l2_buf_capture.m.offset=1843200.

frame_bufs[2].addr=0xb683a000, frame_bufs[2].length=1843200, v4l2_buf_capture.m.offset=3686400.
---------capure picture
Write fd_frame 0, v4l2_buf_capture.index=0, v4l2_buf_capture.bytesused=19593.
Write fd_frame 1, v4l2_buf_capture.index=1, v4l2_buf_capture.bytesused=9349.
---------capure picture end
---------capure video
frame rate = 24.147728.

frame rate = 23.437500.

frame rate = 24.305555.

frame rate = 24.925594.

frame rate = 24.375000.

frame rate = 23.976294.

frame rate = 24.414062.

---------capure video end
*/
