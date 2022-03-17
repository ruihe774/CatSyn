#include <mutex>

#include <string.h>

#include <boost/container/flat_map.hpp>

#include <porphyrin.h>

VSNodeRef* cloneNodeRef(VSNodeRef* node) noexcept {
    return new VSNodeRef{node->nucl, node->substrate, node->output, node->vi};
}

void freeNode(VSNodeRef* node) noexcept {
    delete node;
}

void getFrameAsync(int n, VSNodeRef* node, VSFrameDoneCallback callback, void* userData) noexcept {
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

struct VSFrameContext {
    boost::container::small_flat_map<std::pair<VSNodeRef*, size_t>, catsyn::cat_ptr<const catsyn::IFrame>, 9>
        input_frames;
};

[[noreturn]] static void frame_not_requested() {
    throw std::logic_error("the filter attempts to get a frame that has not been requested");
}

const VSFrameRef* getFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept {
    if (auto it = frameCtx->input_frames.find(std::make_pair(node, static_cast<size_t>(n)));
        it != frameCtx->input_frames.end())
        return new VSFrameRef{it->second};
    else
        frame_not_requested();
}

void requestFrameFilter(int n, VSNodeRef* node, VSFrameContext* frameCtx) noexcept {
    frameCtx->input_frames.emplace(std::make_pair(node, static_cast<size_t>(n)), nullptr);
}

const VSVideoInfo* getVideoInfo(VSNodeRef* node) noexcept {
    return &node->vi;
}

void setVideoInfo(const VSVideoInfo* vi, int numOutputs, VSNode* node) noexcept {
    reinterpret_cast<VSNodeRef*>(node)->vi = *vi;
}
