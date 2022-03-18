#include <mutex>
#include <variant>

#include <string.h>

#include <boost/container/flat_map.hpp>

#include <porphyrin.h>

VSNodeRef* cloneNodeRef(VSNodeRef* node) noexcept {
    return new VSNodeRef{node->nucl, node->substrate, node->output, node->vi};
}

void freeNode(VSNodeRef* node) noexcept {
    delete node;
}

static std::mutex starting_mutex;

void getFrameAsync(int n, VSNodeRef* node, VSFrameDoneCallback callback, void* userData) noexcept {
    if (!node->nucl.is_reacting()) {
        std::lock_guard<std::mutex> guard(starting_mutex);
        node->nucl.react();
    }
    if (!node->output)
        node->nucl.create_output(node->substrate.get(), node->output.put());
    node->output->get_frame(n, [=](const catsyn::IFrame* frame, std::exception_ptr exc) {
        if (exc)
            callback(userData, nullptr, n, node, catsyn::exception_ptr_what(exc));
        else
            callback(userData, new VSFrameRef{frame}, n, node, nullptr);
    });
}

const VSFrameRef* getFrame(int n, VSNodeRef* node, char* errorMsg, int bufSize) noexcept {
    if (!node->nucl.is_reacting()) {
        std::lock_guard<std::mutex> guard(starting_mutex);
        node->nucl.react();
    }
    if (!node->output)
        node->nucl.create_output(node->substrate.get(), node->output.put());
    std::condition_variable cv;
    std::mutex m;
    const VSFrameRef* frame_ref = nullptr;
    node->output->get_frame(n, [&](const catsyn::IFrame* frame, std::exception_ptr exc) {
        if (exc)
            strcpy_s(errorMsg, bufSize, catsyn::exception_ptr_what(exc));
        else
            frame_ref = new VSFrameRef{frame};
        std::lock_guard<std::mutex> guard(m);
        cv.notify_one();
    });
    std::unique_lock<std::mutex> guard(m);
    cv.wait(guard);
    return frame_ref;
}

struct FrameSourceCompare {
    bool operator()(const catsyn::FrameSource& lhs, const catsyn::FrameSource& rhs) const noexcept {
        if (lhs.substrate == rhs.substrate)
            return lhs.frame_idx < rhs.frame_idx;
        else
            return lhs.substrate < rhs.substrate;
    }
};

struct VSFrameContext {
    typedef boost::container::small_vector<catsyn::FrameSource, 9> request_container;
    typedef boost::container::small_flat_map<catsyn::FrameSource, const catsyn::IFrame*, 9, FrameSourceCompare>
        input_map;
    std::variant<request_container, input_map> frames;
    const char* error = nullptr;
};

[[noreturn]] static void frame_not_requested() {
    throw std::logic_error("the filter attempts to get a frame that has not been requested");
}

const VSFrameRef* getFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept {
    auto input_frames = std::get<VSFrameContext::input_map>(frameCtx->frames);
    if (auto it = input_frames.find(catsyn::FrameSource{node->substrate.get(), static_cast<size_t>(n)});
        it != input_frames.end())
        return new VSFrameRef{it->second};
    else
        frame_not_requested();
}

void requestFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept {
    std::get<VSFrameContext::request_container>(frameCtx->frames)
        .push_back(catsyn::FrameSource{node->substrate.get(), static_cast<size_t>(n)});
}

void setFilterError(const char* errorMessage, VSFrameContext* frameCtx) noexcept {
    frameCtx->error = errorMessage;
}

const VSVideoInfo* getVideoInfo(VSNodeRef* node) noexcept {
    return &node->vi;
}

void setVideoInfo(const VSVideoInfo* vi, int numOutputs, VSNode* node) noexcept {
    auto nd = reinterpret_cast<VSNodeRef*>(node);
    nd->vi = *vi;
    if (numOutputs != 1)
        nd->nucl.get_logger()->log(catsyn::LogLevel::WARNING,
                                   "Metalloporphyrin: returning multiple clips are not supported (setVideoInfo)");
}

struct VSFilter final : Object, catsyn::IFilter {
    VSCore* core;
    catsyn::VideoInfo vi;
    VSFilterGetFrame getFrame;
    VSFilterFree freer;
    void* instanceData;
    void* frameData;
    catsyn::FilterFlags flags;
    VSFrameContext ctx;
    bool has_input;

    VSFilter(const VSFilter&) = delete;
    VSFilter(VSFilter&&) = delete;
    VSFilter(VSCore* core, catsyn::VideoInfo vi, VSFilterGetFrame getFrame, VSFilterFree freer, void* instanceData,
             catsyn::FilterFlags flags, bool has_input) noexcept;
    ~VSFilter() final;

    void clone(catsyn::IObject** out) const noexcept final;

    catsyn::FilterFlags get_filter_flags() const noexcept final;
    catsyn::VideoInfo get_video_info() const noexcept final;
    const catsyn::FrameSource* get_frame_dependency(size_t frame_idx, size_t* len) noexcept final;
    void process_frame(size_t frame_idx, const catsyn::IFrame* const* input_frames, const catsyn::FrameSource* sources,
                       size_t source_count, const catsyn::IFrame** out) final;
};

static catsyn::VideoInfo vi_vs_to_cs(const VSVideoInfo& vvi) {
    catsyn::VideoInfo cvi;
    cvi.frame_info.format = ff_vs_to_cs(vvi.format);
    cvi.frame_info.width = vvi.width;
    cvi.frame_info.height = vvi.height;
    cvi.fps.num = vvi.fpsNum;
    cvi.fps.den = vvi.fpsDen;
    cvi.frame_count = vvi.numFrames;
    return cvi;
}

[[noreturn]] static void fm_not_support() {
    throw std::invalid_argument("fmSerial not supported");
}

[[noreturn]] static void nf_not_support() {
    throw std::invalid_argument("nfIsCache not supported");
}

void createFilter(const VSMap* in, VSMap* out, const char* name, VSFilterInit init, VSFilterGetFrame getFrame,
                  VSFilterFree freer, int filterMode, int flags, void* instanceData, VSCore* core) noexcept {
    if (filterMode >= fmSerial)
        fm_not_support();
    if (flags > nfMakeLinear || flags & nfIsCache)
        nf_not_support();
    std::unique_ptr<VSNodeRef> node(new VSNodeRef{*core->nucl});
    init(const_cast<VSMap*>(in), out, &instanceData, reinterpret_cast<VSNode*>(node.get()), core, &api);
    bool has_input = false;
    for (size_t ref = 0, size = in->view.size(); ref < size; ++ref)
        if (dynamic_cast<const catsyn::ISubstrate*>(in->view.table->get(ref)))
            has_input = true;
    out->get_mut().set("clip",
                       new VSFilter(core, vi_vs_to_cs(node->vi), getFrame, freer, instanceData,
                                    catsyn::FilterFlags((flags & nfMakeLinear ? catsyn::ffMakeLinear : 0) |
                                                        (filterMode == fmParallel ? 0 : catsyn::ffSingleThreaded)), has_input));
}

VSFilter::VSFilter(VSCore* core, catsyn::VideoInfo vi, VSFilterGetFrame getFrame, VSFilterFree freer,
                   void* instanceData, catsyn::FilterFlags flags, bool has_input) noexcept
    : core(core), vi(vi), getFrame(getFrame), freer(freer), instanceData(instanceData), frameData(nullptr),
      flags(flags), has_input(has_input) {}

VSFilter::~VSFilter() {
//    if (freer)
//        freer(instanceData, core, &api);
}

void VSFilter::clone(catsyn::IObject** out) const noexcept {
    catsyn::create_instance<VSFilter>(out, core, vi, getFrame, nullptr, instanceData, flags, has_input);
}

catsyn::FilterFlags VSFilter::get_filter_flags() const noexcept {
    return flags;
}

catsyn::VideoInfo VSFilter::get_video_info() const noexcept {
    return vi;
}

[[noreturn]] void throw_filter_error(const char* msg) {
    throw std::runtime_error(msg);
}

const catsyn::FrameSource* VSFilter::get_frame_dependency(size_t frame_idx, size_t* len) noexcept {
    if (!has_input) {
        *len = 0;
        return nullptr;
    }
    ctx.frames = VSFrameContext::request_container{};
    ctx.error = nullptr;
    getFrame(static_cast<int>(frame_idx), arInitial, &instanceData, &frameData, &ctx, core, &api);
    if (ctx.error)
        throw_filter_error(ctx.error);
    auto frames = std::get<VSFrameContext::request_container>(ctx.frames);
    *len = frames.size();
    return frames.data();
}

void VSFilter::process_frame(size_t frame_idx, const catsyn::IFrame* const* input_frames,
                             const catsyn::FrameSource* sources, size_t source_count, const catsyn::IFrame** out) {
    ctx.frames = VSFrameContext::input_map{};
    ctx.error = nullptr;
    auto frames = std::get<VSFrameContext::input_map>(ctx.frames);
    for (size_t i = 0; i < source_count; ++i)
        frames.emplace(sources[i], input_frames[i]);
    auto frame_ref = const_cast<VSFrameRef*>(
        getFrame(static_cast<int>(frame_idx), has_input ? arAllFramesReady : arInitial, &instanceData, &frameData, &ctx, core, &api));
    if (ctx.error)
        throw_filter_error(ctx.error);
    *out = frame_ref->frame.detach();
    freeFrame(frame_ref);
}

[[noreturn]] static void not_implemented() {
    throw std::logic_error("not implemented");
}

void queryCompletedFrame(VSNodeRef** node, int* n, VSFrameContext* frameCtx) noexcept {
    not_implemented();
}

void releaseFrameEarly(VSNodeRef* node, int n, VSFrameContext* frameCtx) noexcept {
    node->nucl.get_logger()->log(catsyn::LogLevel::WARNING, "Metalloporphyrin: not implemented (releaseFrameEarly)");
}

int getOutputIndex(VSFrameContext* frameCtx) noexcept {
    return 0;
}
