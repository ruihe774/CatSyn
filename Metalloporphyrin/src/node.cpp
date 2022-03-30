#include <mutex>
#include <variant>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>

#include <porphyrin.h>

VSNodeRef* cloneNodeRef(VSNodeRef* node) noexcept {
    return new VSNodeRef{node->substrate, node->output, node->vi};
}

void freeNode(VSNodeRef* node) noexcept {
    delete node;
}

static std::mutex starting_mutex;

void getFrameAsync(int n, VSNodeRef* node, VSFrameDoneCallback callback, void* userData) noexcept {
    if (!core->nucl->is_reacting()) {
        std::lock_guard<std::mutex> guard(starting_mutex);
        core->nucl->react();
    }
    if (!node->output)
        core->nucl->create_output(node->substrate.get(), node->output.put());
    node->output->get_frame(n, catsyn::wrap_callback([=](const catsyn::IFrame* frame, std::exception_ptr exc) {
                                   if (exc)
                                       try {
                                           std::rethrow_exception(exc);
                                       } catch (std::exception& exc) {
                                           callback(userData, nullptr, n, node, exc.what());
                                       }
                                   else
                                       callback(userData, new VSFrameRef{frame}, n, node, nullptr);
                               }).get());
}

const VSFrameRef* getFrame(int n, VSNodeRef* node, char* errorMsg, int bufSize) noexcept {
    if (!core->nucl->is_reacting()) {
        std::lock_guard<std::mutex> guard(starting_mutex);
        core->nucl->react();
    }
    if (!node->output)
        core->nucl->create_output(node->substrate.get(), node->output.put());
    std::condition_variable cv;
    std::mutex m;
    const VSFrameRef* frame_ref = nullptr;
    node->output->get_frame(n, catsyn::wrap_callback([&](const catsyn::IFrame* frame, std::exception_ptr exc) {
                                   if (exc)
                                       try {
                                           std::rethrow_exception(exc);
                                       } catch (std::exception& exc) {
                                           strcpy_s(errorMsg, bufSize, exc.what());
                                       }
                                   else
                                       frame_ref = new VSFrameRef{frame};
                                   std::lock_guard<std::mutex> guard(m);
                                   cv.notify_one();
                               }).get());
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

struct VSFrameContext : catsyn::FrameData {
    typedef boost::container::small_vector<catsyn::FrameSource, 10> request_container;
    typedef boost::container::small_flat_map<catsyn::FrameSource, const catsyn::IFrame*, 10, FrameSourceCompare>
        input_map;
    size_t frame_idx;
    std::variant<request_container, input_map> frames;
    const char* error;
    void* vs_frame_data;
    explicit VSFrameContext(size_t frame_idx) noexcept : frame_idx(frame_idx), error(nullptr), vs_frame_data(nullptr) {}
};

const VSFrameRef* getFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept {
    auto& input_frames = std::get<VSFrameContext::input_map>(frameCtx->frames);
    auto it = input_frames.find(catsyn::FrameSource{node->substrate.get(), static_cast<size_t>(n)});
    cond_check(it != input_frames.end(), "the filter attempts to get a frame that has not been requested");
    return new VSFrameRef{it->second};
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
        core->nucl->get_logger()->log(catsyn::LogLevel::WARNING,
                                      "Metalloporphyrin: returning multiple clips are not supported (setVideoInfo)");
}

struct VSFilter final : Object, virtual catsyn::IFilter {
    catsyn::VideoInfo vi;
    catsyn::FilterFlags flags;
    VSFilterGetFrame getFrame;
    VSFilterFree freer;
    mutable void* instanceData;
    mutable bool is_source_filter;

    ~VSFilter() final;

    catsyn::FilterFlags get_filter_flags() const noexcept final;
    catsyn::VideoInfo get_video_info() const noexcept final;
    void get_frame_data(size_t frame_idx, catsyn::FrameData** frame_data) const noexcept final;
    void process_frame(const catsyn::IFrame* const* input_frames, catsyn::FrameData** frame_data,
                       const catsyn::IFrame** out) const final;
    void drop_frame_data(catsyn::FrameData* frame_data) const noexcept final;
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

static const class ParallelBlacklist {
    boost::container::small_flat_set<std::string_view, 8> list;

  public:
    ParallelBlacklist() noexcept {
        list.emplace("club.amusement.eedi2cuda");
        list.emplace("com.wolframrhodium.bm3dcuda");
        list.emplace("com.wolframrhodium.bm3dcuda_rtc");
    }

    bool is_blacklisted(const char* identifier) const noexcept {
        return list.contains(identifier);
    }
} parallel_blacklist;

void createFilter(const VSMap* in, VSMap* out, const char* name, VSFilterInit init, VSFilterGetFrame getFrame,
                  VSFilterFree freer, int filterMode, int flags, void* instanceData, VSCore*) noexcept {
    cond_check(filterMode < fmSerial, "fmSerial is not supported");
    cond_check(flags <= nfMakeLinear && (flags & nfIsCache) == 0, "nfIsCache is not supported");
    auto plugin = plugin_invoke_stack.top();
    std::unique_ptr<VSNodeRef> node(new VSNodeRef);
    init(const_cast<VSMap*>(in), out, &instanceData, reinterpret_cast<VSNode*>(node.get()), core.get(), &api);
    auto filter = new VSFilter;
    filter->vi = vi_vs_to_cs(node->vi);
    filter->flags = catsyn::FilterFlags(
        (flags & nfMakeLinear ? catsyn::ffMakeLinear : catsyn::ffNormal) |
        (filterMode == fmParallel && !parallel_blacklist.is_blacklisted(plugin->enzyme->get_identifier())
             ? catsyn::ffNormal
             : catsyn::ffSingleThreaded));
    filter->getFrame = getFrame;
    filter->freer = freer;
    filter->instanceData = instanceData;
    filter->is_source_filter = false;
    out->get_mut()->set(out->table->find("clip"), filter, "clip");
}

VSFilter::~VSFilter() {
    if (freer)
        freer(instanceData, core.get(), &api);
}

catsyn::FilterFlags VSFilter::get_filter_flags() const noexcept {
    return flags;
}

catsyn::VideoInfo VSFilter::get_video_info() const noexcept {
    return vi;
}

[[noreturn]] void throw_filter_error(const std::string& msg) {
    throw std::runtime_error(msg);
}

void VSFilter::get_frame_data(size_t frame_idx, catsyn::FrameData** frame_data) const noexcept {
    auto ctx = std::make_unique<VSFrameContext>(frame_idx);
    if (is_source_filter)
        ctx->dependency_count = 0;
    else {
        auto& frames = ctx->frames.emplace<VSFrameContext::request_container>();
        is_source_filter = !!getFrame(static_cast<int>(frame_idx), arInitial, &instanceData, &ctx->vs_frame_data,
                                      ctx.get(), core.get(), &api);
        if (auto err = ctx->error; err)
            throw_filter_error(err);
        ctx->dependencies = frames.data();
        ctx->dependency_count = frames.size();
    }
    *frame_data = ctx.release();
}

void VSFilter::process_frame(const catsyn::IFrame* const* input_frames, catsyn::FrameData** frame_data,
                             const catsyn::IFrame** out) const {
    auto ctx = std::unique_ptr<VSFrameContext>(static_cast<VSFrameContext*>(*frame_data));
    *frame_data = nullptr;
    VSFrameContext::input_map inputs;
    for (size_t i = 0; i < ctx->dependency_count; ++i)
        inputs.emplace(ctx->dependencies[i], input_frames[i]);
    ctx->frames = std::move(inputs);
    auto frame_ref = std::unique_ptr<VSFrameRef>(const_cast<VSFrameRef*>(
        getFrame(static_cast<int>(ctx->frame_idx), is_source_filter ? arInitial : arAllFramesReady, &instanceData,
                 &ctx->vs_frame_data, ctx.get(), core.get(), &api)));
    if (auto err = ctx->error; err)
        throw_filter_error(err);
    *out = frame_ref->frame.detach();
}

void VSFilter::drop_frame_data(catsyn::FrameData* frame_data) const noexcept {
    if (frame_data)
        core->nucl->get_logger()->log(catsyn::LogLevel::WARNING, "VSFilter: frame data leaked");
}

void queryCompletedFrame(VSNodeRef** node, int* n, VSFrameContext* frameCtx) noexcept {
    not_implemented();
}

void releaseFrameEarly(VSNodeRef* node, int n, VSFrameContext* frameCtx) noexcept {
    core->nucl->get_logger()->log(catsyn::LogLevel::WARNING, "Metalloporphyrin: not implemented (releaseFrameEarly)");
}

int getOutputIndex(VSFrameContext* frameCtx) noexcept {
    return 0;
}
