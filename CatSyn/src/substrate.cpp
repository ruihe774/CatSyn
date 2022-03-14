#include <catimpl.h>

Substrate::Substrate(cat_ptr<const IFilter> filter) noexcept : vi(filter->get_video_info()) {
    filters[std::this_thread::get_id()] = filter.usurp_or_clone();
}

VideoInfo Substrate::get_video_info() const noexcept {
    return vi;
}

void Nucleus::register_filter(const IFilter* in, ISubstrate** out) noexcept {
    create_instance<Substrate>(out, wrap_cat_ptr(in));
}

FrameInstance::FrameInstance(Substrate* substrate, size_t frame_idx, std::initializer_list<FrameInstance*> inputs,
                             std::initializer_list<FrameInstance*> outputs) noexcept
    : substrate(substrate), frame_idx(frame_idx), inputs(inputs), outputs(outputs) {}

MaintainTask MaintainTask::create(MaintainTask::Type t, void* p, size_t v) noexcept {
    MaintainTask mtk;
    mtk.p = reinterpret_cast<uintptr_t>(p) | static_cast<uintptr_t>(t);
    mtk.v = v;
    return mtk;
}

size_t MaintainTask::get_value() const noexcept {
    return v;
}

void* MaintainTask::get_pointer() const noexcept {
    return reinterpret_cast<Substrate*>(p & ~static_cast<uintptr_t>(3));
}

MaintainTask::Type MaintainTask::get_type() const noexcept {
    return static_cast<MaintainTask::Type>(p & 3);
}

static void worker(Semaphore& work_semaphore, Semaphore& maintain_semaphore,
                   boost::lockfree::queue<FrameInstance*>& work_queue,
                   boost::lockfree::queue<MaintainTask>& maintain_queue, const std::atomic_bool& stop) noexcept;
static void maintainer(Nucleus& nucl, Semaphore& semaphore, boost::lockfree::queue<MaintainTask>& queue,
                       const std::atomic_bool& stop) noexcept;

void Nucleus::react() noexcept {
    maintainer_thread =
        Thread(maintainer, std::ref(*this), std::ref(maintain_semaphore), std::ref(maintain_queue), std::cref(stop));
    for (size_t i = 0; i < config.thread_count; ++i)
        worker_threads.emplace_back(worker, std::ref(work_semaphore), std::ref(maintain_semaphore),
                                    std::ref(work_queue), std::ref(maintain_queue), std::cref(stop));
}

bool Nucleus::is_reacting() const noexcept {
    return !!maintainer_thread;
}

[[noreturn]] void bug() {
    throw std::logic_error("bug!");
}

void worker(Semaphore& work_semaphore, Semaphore& maintain_semaphore,
            boost::lockfree::queue<FrameInstance*>& work_queue, boost::lockfree::queue<MaintainTask>& maintain_queue,
            const std::atomic_bool& stop) noexcept {
    while (true) {
        work_semaphore.acquire();
        if (stop.load(std::memory_order_acquire))
            break;
        work_queue.consume_one([&](FrameInstance* inst) {
            std::unique_lock<std::mutex> lock(inst->processing_mutex, std::try_to_lock);
            if (!lock.owns_lock())
                return;
            try {
                if (inst->product)
                    goto early_exit;
                std::vector<IFrame*> input_frames;
                for (auto input : inst->inputs) {
#ifndef NDEBUG
                    if (!input->product)
                        bug();
#endif
                    input_frames.push_back(input->product.get());
                }
                cat_ptr<IFrame> product;
                inst->substrate->filters[std::this_thread::get_id()]->process_frame(
                    inst->frame_idx, input_frames.data(), reinterpret_cast<const FrameSource*>(inst->inputs.data()),
                    inst->inputs.size(), product.put());
                inst->product = std::move(product);
                maintain_queue.push(MaintainTask::create(MaintainTask::Type::Notify, inst, 0));
                maintain_semaphore.release();
            } catch (...) {
                lock.unlock();
                throw;
            }
        early_exit:
            lock.unlock();
        });
    }
}
