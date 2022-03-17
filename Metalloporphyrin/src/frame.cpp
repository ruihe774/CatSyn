#include <array>

#include <boost/container/flat_map.hpp>

#include <porphyrin.h>

static boost::container::small_flat_map<uint32_t, std::unique_ptr<VSFormat>, 64> formats;
static boost::container::small_flat_map<int, VSFormat*, 64> vsformatid_map;
static std::shared_mutex formats_mutex;

catsyn::cat_ptr<catsyn::IFrame>& VSFrameRef::get_mut() noexcept {
    return reinterpret_cast<catsyn::cat_ptr<catsyn::IFrame>&>(frame);
}

static catsyn::FrameFormat ff_vs_to_cs(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW,
                                       int subSamplingH) {
    catsyn::FrameFormat ff;
    if (colorFamily > cmYUV)
        throw std::logic_error("unimplemented color family");
    ff.detail.color_family = static_cast<catsyn::ColorFamily>(colorFamily / 1000000);
    ff.detail.sample_type = static_cast<catsyn::SampleType>(sampleType);
    ff.detail.bits_per_sample = bitsPerSample;
    ff.detail.width_subsampling = subSamplingW;
    ff.detail.height_subsampling = subSamplingH;
    return ff;
}

const VSFormat* registerFormat(catsyn::FrameFormat ff, const char* name, int id) noexcept {
    static int id_offset = 1000;
    auto ffid = ff.id;
    {
        std::shared_lock<std::shared_mutex> lock(formats_mutex);
        if (auto it = formats.find(ffid); it != formats.end())
            return it->second.get();
    }
    {
        auto colorFamily = static_cast<int>(ff.detail.color_family) * 1000000;
        auto sampleType = static_cast<int>(ff.detail.sample_type);
        VSFormat format;
        strcpy_s(format.name, sizeof(VSFormat::name), name);
        format.id = id ? id : colorFamily + id_offset++;
        format.colorFamily = colorFamily;
        format.sampleType = sampleType;
        format.bitsPerSample = static_cast<int>(ff.detail.bits_per_sample);
        format.bytesPerSample = static_cast<int>(catsyn::bytes_per_sample(ff));
        format.subSamplingW = static_cast<int>(ff.detail.width_subsampling);
        format.subSamplingH = static_cast<int>(ff.detail.height_subsampling);
        format.numPlanes = static_cast<int>(catsyn::num_planes(ff));
        std::unique_lock<std::shared_mutex> lock(formats_mutex);
        auto vsf = formats.emplace(ffid, std::make_unique<VSFormat>(format)).first->second.get();
        vsformatid_map.emplace(format.id, vsf);
        return vsf;
    }
}

static const VSFormat* registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW,
                                      int subSamplingH, const char* name = "unknown", int id = 0) noexcept {
    return registerFormat(ff_vs_to_cs(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH), name, id);
}

const VSFormat* registerFormat(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH,
                               VSCore*) noexcept {
    return registerFormat(colorFamily, sampleType, bitsPerSample, subSamplingW, subSamplingH);
}

static void registerFormats() noexcept {
    registerFormat(cmGray, stInteger, 8, 0, 0, "Gray8", pfGray8);
    registerFormat(cmGray, stInteger, 16, 0, 0, "Gray16", pfGray16);
    registerFormat(cmGray, stFloat, 16, 0, 0, "GrayH", pfGrayH);
    registerFormat(cmGray, stFloat, 32, 0, 0, "GrayS", pfGrayS);
    registerFormat(cmYUV, stInteger, 8, 1, 1, "YUV420P8", pfYUV420P8);
    registerFormat(cmYUV, stInteger, 8, 1, 0, "YUV422P8", pfYUV422P8);
    registerFormat(cmYUV, stInteger, 8, 0, 0, "YUV444P8", pfYUV444P8);
    registerFormat(cmYUV, stInteger, 8, 2, 2, "YUV410P8", pfYUV410P8);
    registerFormat(cmYUV, stInteger, 8, 2, 0, "YUV411P8", pfYUV411P8);
    registerFormat(cmYUV, stInteger, 8, 0, 1, "YUV440P8", pfYUV440P8);
    registerFormat(cmYUV, stInteger, 9, 1, 1, "YUV420P9", pfYUV420P9);
    registerFormat(cmYUV, stInteger, 9, 1, 0, "YUV422P9", pfYUV422P9);
    registerFormat(cmYUV, stInteger, 9, 0, 0, "YUV444P9", pfYUV444P9);
    registerFormat(cmYUV, stInteger, 10, 1, 1, "YUV420P10", pfYUV420P10);
    registerFormat(cmYUV, stInteger, 10, 1, 0, "YUV422P10", pfYUV422P10);
    registerFormat(cmYUV, stInteger, 10, 0, 0, "YUV444P10", pfYUV444P10);
    registerFormat(cmYUV, stInteger, 12, 1, 1, "YUV420P12", pfYUV420P12);
    registerFormat(cmYUV, stInteger, 12, 1, 0, "YUV422P12", pfYUV422P12);
    registerFormat(cmYUV, stInteger, 12, 0, 0, "YUV444P12", pfYUV444P12);
    registerFormat(cmYUV, stInteger, 14, 1, 1, "YUV420P14", pfYUV420P14);
    registerFormat(cmYUV, stInteger, 14, 1, 0, "YUV422P14", pfYUV422P14);
    registerFormat(cmYUV, stInteger, 14, 0, 0, "YUV444P14", pfYUV444P14);
    registerFormat(cmYUV, stInteger, 16, 1, 1, "YUV420P16", pfYUV420P16);
    registerFormat(cmYUV, stInteger, 16, 1, 0, "YUV422P16", pfYUV422P16);
    registerFormat(cmYUV, stInteger, 16, 0, 0, "YUV444P16", pfYUV444P16);
    registerFormat(cmYUV, stFloat, 16, 0, 0, "YUV444PH", pfYUV444PH);
    registerFormat(cmYUV, stFloat, 32, 0, 0, "YUV444PS", pfYUV444PS);
    registerFormat(cmRGB, stInteger, 8, 0, 0, "RGB24", pfRGB24);
    registerFormat(cmRGB, stInteger, 9, 0, 0, "RGB27", pfRGB27);
    registerFormat(cmRGB, stInteger, 10, 0, 0, "RGB30", pfRGB30);
    registerFormat(cmRGB, stInteger, 16, 0, 0, "RGB48", pfRGB48);
    registerFormat(cmRGB, stFloat, 16, 0, 0, "RGBH", pfRGBH);
    registerFormat(cmRGB, stFloat, 32, 0, 0, "RGBS", pfRGBS);
}

const VSFormat* getFormatPreset(int id, VSCore*) noexcept {
    return vsformatid_map[id];
}

static struct FormatRegisterer {
    FormatRegisterer() noexcept {
        registerFormats();
    }
} fmtreg [[maybe_unused]];

VSFrameRef* newVideoFrame(const VSFormat* format, int width, int height, const VSFrameRef* propSrc,
                          VSCore* core) noexcept {
    catsyn::FrameInfo fi{
        ff_vs_to_cs(format->colorFamily, format->sampleType, format->bitsPerSample, format->subSamplingW,
                    format->subSamplingH),
        static_cast<unsigned>(width),
        static_cast<unsigned>(height),
    };
    auto frame_ref = new VSFrameRef;
    core->nucl->get_factory()->create_frame(fi, nullptr, nullptr, propSrc ? propSrc->frame->get_frame_props() : nullptr,
                                            frame_ref->frame.put());
    return frame_ref;
}

VSFrameRef* newVideoFrame2(const VSFormat* format, int width, int height, const VSFrameRef** planeSrc,
                           const int* planes, const VSFrameRef* propSrc, VSCore* core) noexcept {
    if (!planeSrc || !planes)
        return newVideoFrame(format, width, height, propSrc, core);
    catsyn::FrameInfo fi{
        ff_vs_to_cs(format->colorFamily, format->sampleType, format->bitsPerSample, format->subSamplingW,
                    format->subSamplingH),
        static_cast<unsigned>(width),
        static_cast<unsigned>(height),
    };
    auto frame_ref = new VSFrameRef;
    std::array<const catsyn::IAlignedBytes*, 3> agb;
    std::array<size_t, 3> strides;
    for (unsigned i = 0; i < catsyn::num_planes(fi.format); ++i)
        if (planeSrc[i]) {
            auto frame = planeSrc[i]->frame;
            agb[i] = frame->get_plane(planes[i]);
            strides[i] = frame->get_stride(planes[i]);
        } else
            agb[i] = nullptr;
    core->nucl->get_factory()->create_frame(
        fi, agb.data(), strides.data(), propSrc ? propSrc->frame->get_frame_props() : nullptr, frame_ref->frame.put());
    return frame_ref;
}

VSFrameRef* copyFrame(const VSFrameRef* f, VSCore*) noexcept {
    return new VSFrameRef{f->frame.clone()};
}

const VSFrameRef* cloneFrameRef(const VSFrameRef* f) noexcept {
    return new VSFrameRef{f->frame};
}

void freeFrame(const VSFrameRef* f) noexcept {
    delete f;
}

int getStride(const VSFrameRef* f, int plane) noexcept {
    return static_cast<int>(f->frame->get_stride(plane));
}

const uint8_t* getReadPtr(const VSFrameRef* f, int plane) noexcept {
    return static_cast<const uint8_t*>(f->frame->get_plane(plane)->data());
}

uint8_t* getWritePtr(VSFrameRef* f, int plane) noexcept {
    return static_cast<uint8_t*>(f->get_mut()->get_plane_mut(plane)->data());
}

const VSFormat* getFrameFormat(const VSFrameRef* f) noexcept {
    return registerFormat(f->frame->get_frame_info().format);
}

int getFrameWidth(const VSFrameRef* f, int plane) noexcept {
    return static_cast<int>(catsyn::plane_width(f->frame->get_frame_info(), plane));
}

int getFrameHeight(const VSFrameRef* f, int plane) noexcept {
    return static_cast<int>(catsyn::plane_height(f->frame->get_frame_info(), plane));
}

void copyFrameProps(const VSFrameRef* src, VSFrameRef* dst, VSCore*) noexcept {
    catsyn::cat_ptr<const catsyn::ITable> props = src->frame->get_frame_props();
    dst->get_mut()->set_frame_props(props.clone().get());
}

const VSMap* getFramePropsRO(const VSFrameRef* f) noexcept {
    return new VSMap(f->frame->get_frame_props());
}

VSMap* getFramePropsRW(VSFrameRef* f) noexcept {
    return new VSMap(f->get_mut()->get_frame_props_mut());
}
