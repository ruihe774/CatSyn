#include <unordered_map>

#include <boost/functional/hash.hpp>

#include <catimpl.h>

struct FrameInstance {
    const cat_ptr<Substrate> substrate;
    const size_t frame_idx;
    cat_ptr<IFrame> product;
    boost::container::small_vector<FrameInstance*, 13> inputs;
    boost::container::small_vector<FrameInstance*, 30> outputs;
    std::mutex processing_mutex;
    std::unique_ptr<IOutput::Callback> callback;
    bool false_dep;

    FrameInstance(Substrate* substrate, size_t frame_idx) noexcept;
};

static std::thread::id position_zero;

Substrate::Substrate(cat_ptr<const IFilter> filter) noexcept {
    filters[position_zero] = filter.usurp_or_clone();
}

VideoInfo Substrate::get_video_info() const noexcept {
    return filters.find(position_zero)->second->get_video_info();
}

void Nucleus::register_filter(const IFilter* in, ISubstrate** out) noexcept {
    create_instance<Substrate>(out, wrap_cat_ptr(in));
}

FrameInstance::FrameInstance(Substrate* substrate, size_t frame_idx) noexcept
    : substrate(substrate), frame_idx(frame_idx), false_dep(false) {}

MaintainTask MaintainTask::create(MaintainTask::Type t, void* p, size_t v,
                                  std::array<std::byte, MaintainTask::payload_size> pl) noexcept {
    MaintainTask mtk;
    mtk.p = reinterpret_cast<uintptr_t>(p) | static_cast<uintptr_t>(t);
    mtk.v = v;
    mtk.pl = pl;
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

std::array<std::byte, MaintainTask::payload_size> MaintainTask::get_payload() const noexcept {
    return pl;
}

static void worker(Nucleus&) noexcept;
static void maintainer(Nucleus&) noexcept;

void Nucleus::react() noexcept {
    maintainer_thread = Thread(maintainer, std::ref(*this));
    set_thread_priority(maintainer_thread.value(), 1);
    for (size_t i = 0; i < config.thread_count; ++i)
        worker_threads.emplace_back(worker, std::ref(*this));
    logger.log(LogLevel::DEBUG, "Nucleus: reaction started");
}

bool Nucleus::is_reacting() const noexcept {
    return !!maintainer_thread;
}

Nucleus::~Nucleus() {
    stop.store(true, std::memory_order_release);
    for (auto&& _ : worker_threads)
        work_semaphore.release();
    for (auto&& t : worker_threads)
        t.join();
    maintain_semaphore.release();
    if (maintainer_thread)
        maintainer_thread->join();
}

[[noreturn, maybe_unused]] static void bug() {
    throw std::logic_error("bug!");
}

template<typename... Args> static void post_maintain_task(Nucleus& nucl, Args&&... args) noexcept {
    nucl.maintain_queue.push(MaintainTask::create(std::forward<Args>(args)...));
    nucl.maintain_semaphore.release();
}

static void post_work(Nucleus& nucl, FrameInstance* inst) noexcept {
    nucl.work_queue.push(inst);
    nucl.work_semaphore.release();
}

void worker(Nucleus& nucl) noexcept {
    while (true) {
        nucl.work_semaphore.acquire();
        if (nucl.stop.load(std::memory_order_acquire))
            break;
        nucl.work_queue.consume_one([&](FrameInstance* inst) {
            std::unique_lock<std::mutex> lock(inst->processing_mutex, std::try_to_lock);
            if (!lock.owns_lock() || inst->product)
                return;
            boost::container::small_vector<IFrame*, 13> input_frames;
            for (auto input : inst->inputs) {
#ifndef NDEBUG
                if (!input->product)
                    bug();
#endif
                input_frames.push_back(input->product.get());
            }
            cat_ptr<IFrame> product;
            try {
                inst->substrate->filters[std::this_thread::get_id()]->process_frame(
                    inst->frame_idx, input_frames.data(), reinterpret_cast<const FrameSource*>(inst->inputs.data()),
                    inst->inputs.size() - inst->false_dep, product.put());
            } catch (...) {
                std::array<std::byte, MaintainTask::payload_size> pl{};
                *reinterpret_cast<std::exception_ptr*>(pl.data()) = std::current_exception();
                post_maintain_task(nucl, MaintainTask::Type::Notify, inst, 0, pl);
                return;
            }
            inst->product = std::move(product);
            post_maintain_task(nucl, MaintainTask::Type::Notify, inst, 0);
        });
    }
}

static bool check_all_inputs_ready(FrameInstance* inst) {
    return std::all_of(inst->inputs.begin(), inst->inputs.end(), [](FrameInstance* input) { return !!input->product; });
}

template<typename A, typename B> struct std::hash<std::pair<A, B>> {
    size_t operator()(const std::pair<A, B>& v) const noexcept {
        size_t seed = 0;
        boost::hash_combine(seed, v.first);
        boost::hash_combine(seed, v.second);
        return seed;
    }
};

static FrameInstance*
construct(Nucleus& nucl, std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>>& instances,
          Substrate* substrate, size_t frame_idx, IOutput::Callback* callback = nullptr) noexcept;

void maintainer(Nucleus& nucl) noexcept {
    std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>> instances;
    uint8_t gc_tick = 0;
    while (true) {
        nucl.maintain_semaphore.acquire();
        if (nucl.stop.load(std::memory_order_acquire))
            break;
        nucl.maintain_queue.consume_all([&](MaintainTask task) {
            switch (task.get_type()) {
            case MaintainTask::Type::Notify: {
                auto inst = static_cast<FrameInstance*>(task.get_pointer());
                auto exc = *reinterpret_cast<std::exception_ptr*>(task.get_payload().data());
                if (!exc)
                    for (auto output : inst->outputs)
                        if (check_all_inputs_ready(output))
                            post_work(nucl, output);
                if (inst->callback)
                    (*inst->callback)(inst->product.get(), exc);
                break;
            }
            case MaintainTask::Type::Construct: {
                auto substrate = static_cast<Substrate*>(task.get_pointer());
                auto frame_idx = task.get_value();
                auto callback = *reinterpret_cast<IOutput::Callback**>(task.get_payload().data());
                construct(nucl, instances, substrate, frame_idx, callback);
                break;
            }
            }
        });
        // XXX: UB?
        if (++gc_tick == 0)
            if (auto before = nucl.alloc_stat.get_current();
                before > static_cast<size_t>(nucl.config.mem_hint_mb) << 20) {
                size_t destroy_count = 0;
                for (auto it = instances.begin(); it != instances.end(); ++it) {
                    auto inst = it->second.get();
                    if (!inst->product)
                        goto next;
                    for (auto output : inst->outputs)
                        if (!output->product)
                            goto next;
                    it = instances.erase(it);
                    ++destroy_count;
                next:;
                }
                auto after = nucl.alloc_stat.get_current();
                nucl.logger.log(LogLevel::DEBUG,
                                format_c("Nucleus: lysosome triggered ({} frames collected; before: {}MB, after: {}MB)",
                                         destroy_count, before >> 20, after >> 20));
            }
    }
}

FrameInstance* construct(Nucleus& nucl,
                         std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>>& instances,
                         Substrate* substrate, size_t frame_idx, IOutput::Callback* callback) noexcept {
    auto key = std::make_pair(substrate, frame_idx);
    if (auto it = instances.find(key); it != instances.end())
        return it->second.get();

    auto promoter = substrate->filters.find(position_zero)->second;
    if (substrate->filters.size() == 1)
        for (const auto& thread : nucl.worker_threads)
            if (auto id = thread.get_id(); !substrate->filters.contains(id))
                substrate->filters.emplace(id, promoter.clone());

    size_t len = 0;
    auto deps = promoter->get_frame_dependency(frame_idx, &len);
    auto instc = std::make_unique<FrameInstance>(substrate, frame_idx);
    for (size_t i = 0; i < len; ++i) {
        auto dep = deps[i];
        auto input = construct(nucl, instances, &dynamic_cast<Substrate&>(*const_cast<ISubstrate*>(dep.substrate)),
                               dep.frame_idx);
        instc->inputs.emplace_back(input);
        input->outputs.emplace_back(instc.get());
    }

    if ((promoter->get_filter_flags() & ffMakeLinear) && frame_idx)
        if (auto prev = instances.find(std::make_pair(substrate, frame_idx - 1)); prev != instances.end()) {
            instc->inputs.emplace_back(prev->second.get());
            instc->false_dep = true;
        }

    instc->callback = std::unique_ptr<IOutput::Callback>(callback);
    auto inst = instances.emplace(key, std::move(instc)).first->second.get();

    if (check_all_inputs_ready(inst))
        post_work(nucl, inst);

    return inst;
}

class Output final : public Object, public IOutput, public Shuttle {
  public:
    cat_ptr<Substrate> substrate;

    void get_frame(size_t frame_idx, Callback cb) noexcept final {
        std::array<std::byte, MaintainTask::payload_size> pl;
        *reinterpret_cast<Callback**>(pl.data()) = new Callback(std::move(cb));
        post_maintain_task(nucl, MaintainTask::Type::Construct, substrate.get(), frame_idx, pl);
    }

    explicit Output(Nucleus& nucl, ISubstrate* substrate) noexcept
        : Shuttle(nucl), substrate(&dynamic_cast<Substrate&>(*substrate)) {}
};

void Nucleus::create_output(ISubstrate* substrate, IOutput** output) noexcept {
    create_instance<Output>(output, *this, substrate);
}
