#include <unordered_map>
#include <unordered_set>

#include <boost/functional/hash.hpp>

#include <catimpl.h>

struct FrameInstance {
    const cat_ptr<Substrate> substrate;
    cat_ptr<const IFrame> product;
    boost::container::small_vector<FrameInstance*, 10> inputs;
    boost::container::small_vector<FrameInstance*, 30> outputs;
    std::unique_ptr<IOutput::Callback> callback;
    FrameData* frame_data;
    size_t tick;
    std::atomic_flag taken;
    bool false_dep;
    bool single_threaded;
    unsigned indulgence;

    FrameInstance(Substrate* substrate, FrameData* frame_data, size_t tick) noexcept
        : substrate(substrate), frame_data(frame_data), tick(tick), false_dep(false), single_threaded(false),
          indulgence(0) {}
};

bool FrameInstanceTickGreater::operator()(const FrameInstance* l, const FrameInstance* r) noexcept {
    return l->tick > r->tick;
}

static std::thread::id position_zero;

Substrate::Substrate(Nucleus& nucl, cat_ptr<const IFilter> filter) noexcept : Shuttle(nucl) {
    filters[position_zero] = filter.usurp_or_clone();
}

VideoInfo Substrate::get_video_info() const noexcept {
    return filters.find(position_zero)->second->get_video_info();
}
INucleus* Substrate::get_nucleus() noexcept {
    return &this->nucl;
}

void Nucleus::register_filter(const IFilter* in, ISubstrate** out) noexcept {
    create_instance<Substrate>(out, *this, wrap_cat_ptr(in));
}

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
static void callbacker(Nucleus&) noexcept;

void Nucleus::react() noexcept {
    if (maintainer_thread)
        return;
    maintainer_thread = Thread(maintainer, std::ref(*this));
    set_thread_priority(maintainer_thread.value(), 1);
    callback_thread = Thread(callbacker, std::ref(*this));
    set_thread_priority(callback_thread.value(), 1);
    for (size_t i = 0; i < config.thread_count; ++i)
        worker_threads.emplace_back(worker, std::ref(*this));
    logger.log(LogLevel::DEBUG, "Nucleus: reaction started");
}

bool Nucleus::is_reacting() const noexcept {
    return !!maintainer_thread;
}

Nucleus::~Nucleus() {
    stop.store(true, std::memory_order_release);
    for (auto&& t [[maybe_unused]] : worker_threads)
        work_semaphore.release();
    maintain_semaphore.release();
    callback_semaphore.release();
    for (auto&& t : worker_threads)
        t.join();
    if (maintainer_thread)
        maintainer_thread->join();
    if (callback_thread)
        callback_thread->join();
}

[[noreturn, maybe_unused]] static void bug() {
    throw std::logic_error("bug!");
}

static std::exception_ptr move_out_exc(std::array<std::byte, MaintainTask::payload_size> pl) noexcept {
    auto pexc = reinterpret_cast<std::exception_ptr*>(pl.data());
    auto exc = *pexc;
    std::destroy_at(pexc);
    return exc;
}

static std::array<std::byte, MaintainTask::payload_size> move_in_exc(std::exception_ptr exc) noexcept {
    std::array<std::byte, MaintainTask::payload_size> pl;
    new (reinterpret_cast<std::exception_ptr*>(pl.data())) std::exception_ptr(exc);
    return pl;
}

template<typename... Args> static void post_maintain_task(Nucleus& nucl, Args&&... args) noexcept {
    nucl.maintain_queue.push(MaintainTask::create(std::forward<Args>(args)...));
    nucl.maintain_semaphore.release();
}

static void post_work_direct(Nucleus& nucl, FrameInstance* inst) noexcept {
    nucl.work_queue_lock.acquire();
    nucl.work_queue.push(inst);
    nucl.work_queue_lock.release();
    nucl.work_semaphore.release();
}

void worker(Nucleus& nucl) noexcept {
    while (true) {
        auto wait_start = std::chrono::steady_clock::now();
        nucl.work_semaphore.acquire();
        auto wait_end = std::chrono::steady_clock::now();
        nucl.accountant.bubble += std::chrono::duration_cast<std::chrono::nanoseconds>(wait_end - wait_start).count();
        if (nucl.stop.load(std::memory_order_acquire))
            break;
        nucl.work_queue_lock.acquire();
        auto inst = nucl.work_queue.top();
        nucl.work_queue.pop();
        nucl.work_queue_lock.release();
        ([&]() {
            if (inst->taken.test_and_set(std::memory_order_acq_rel))
                return;
            boost::container::small_vector<const IFrame*, 12> input_frames;
            for (auto input : inst->inputs)
                input_frames.push_back(input->product.get());
            cat_ptr<const IFrame> product;
            try {
                inst->substrate->filters[std::this_thread::get_id()]->process_frame(
                    input_frames.data(), inst->frame_data, product.put_const());
            } catch (...) {
                post_maintain_task(nucl, MaintainTask::Type::Notify, inst, 0, move_in_exc(std::current_exception()));
                return;
            }
            inst->product = std::move(product);
            post_maintain_task(nucl, MaintainTask::Type::Notify, inst, 0);
        })();
    }
}

static bool check_all_inputs_ready(FrameInstance* inst) {
    return std::all_of(inst->inputs.begin(), inst->inputs.end(), [](FrameInstance* input) { return !!input->product; });
}

[[noreturn]] static void unhandled_exception(std::exception_ptr exc) {
    std::rethrow_exception(exc);
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
construct(Nucleus& nucl, size_t tick,
          std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>>& instances,
          std::unordered_set<FrameInstance*>& alive,
          std::unordered_map<Substrate*, std::pair<bool, std::unordered_set<FrameInstance*>>>& neck,
          std::unordered_set<std::pair<Substrate*, size_t>>& history, std::unordered_map<Substrate*, unsigned>& miss,
          Substrate* substrate, size_t frame_idx, std::unique_ptr<IOutput::Callback> callback = {},
          bool missed = false) noexcept;

static bool kill_tree(FrameInstance* inst, std::unordered_set<FrameInstance*>& alive, std::exception_ptr exc) noexcept;

static void
post_work(Nucleus& nucl, FrameInstance* inst,
          std::unordered_map<Substrate*, std::pair<bool, std::unordered_set<FrameInstance*>>>& neck) noexcept;

void maintainer(Nucleus& nucl) noexcept {
    std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>> instances;
    std::unordered_set<FrameInstance*> alive;
    std::unordered_map<Substrate*, std::pair<bool, std::unordered_set<FrameInstance*>>> neck;
    std::unordered_set<std::pair<Substrate*, size_t>> history;
    std::unordered_map<Substrate*, unsigned> miss;
    size_t tick = 0;
    auto last_gc = std::chrono::steady_clock::now();
    size_t last_bubble = 0;
    while (true) {
        nucl.maintain_semaphore.acquire();
        if (nucl.stop.load(std::memory_order_acquire))
            break;
        nucl.maintain_queue.consume_all([&](MaintainTask task) {
            switch (task.get_type()) {
            case MaintainTask::Type::Notify: {
                auto inst = static_cast<FrameInstance*>(task.get_pointer());
                auto exc = move_out_exc(task.get_payload());
                if (alive.find(inst) == alive.end())
                    break;
                if (inst->single_threaded) {
                    neck[inst->substrate.get()].first = false;
                    inst->single_threaded = false;
                }
                if (!exc)
                    for (auto output : inst->outputs)
                        if (alive.find(output) != alive.end() && !output->product && check_all_inputs_ready(output))
                            post_work(nucl, output, neck);
                if (exc) {
                    if (kill_tree(inst, alive, exc))
                        for (auto it = instances.begin(); it != instances.end();) {
                            if (alive.find(it->second.get()) == alive.end())
                                it = instances.erase(it);
                            else
                                ++it;
                        }
                    else
                        unhandled_exception(exc);
                } else if (inst->callback) {
                    (*inst->callback)(inst->product.get(), exc);
                    inst->callback.reset();
                }
                break;
            }
            case MaintainTask::Type::Construct: {
                auto substrate = static_cast<Substrate*>(task.get_pointer());
                auto frame_idx = task.get_value();
                auto callback = *reinterpret_cast<IOutput::Callback**>(task.get_payload().data());
                construct(nucl, tick, instances, alive, neck, history, miss, substrate, frame_idx,
                          std::unique_ptr<IOutput::Callback>(callback));
                break;
            }
            }
            for (auto& item : neck)
                if (auto& sec = item.second; !sec.first && !sec.second.empty()) {
                    auto bg = sec.second.begin();
                    post_work_direct(nucl, *bg);
                    sec.second.erase(bg);
                    sec.first = true;
                }
        });

        if (++tick % 256 == 0) {
            for (auto it = instances.begin(); it != instances.end();) {
                auto inst = it->second.get();
                if (!inst->product || inst->callback || inst->single_threaded)
                    goto next;
                for (auto output : inst->outputs)
                    if (alive.find(output) != alive.end() && !output->product)
                        goto next;
                if (inst->indulgence--)
                    goto next;
                it = instances.erase(it);
                alive.erase(inst);
                continue;
            next:
                ++it;
            }
            if (history.size() > 65535) {
                nucl.logger.log(LogLevel::DEBUG, "Nucleus: history forgotten");
                history.clear();
            }
            auto cur_bubble = nucl.accountant.bubble;
            auto cur_time = std::chrono::steady_clock::now();
            auto bubble_delta = cur_bubble - last_bubble;
            auto time_delta = std::chrono::duration_cast<std::chrono::nanoseconds>(cur_time - last_gc).count();
            last_bubble = cur_bubble;
            last_gc = cur_time;
            if (auto bubble_ratio = bubble_delta * 100 / time_delta; bubble_ratio > 1)
                nucl.logger.log(LogLevel::DEBUG,
                                format_c("Nucleus: bubble {}% {}/{} μs", bubble_delta * 100 / time_delta,
                                         bubble_delta / 1000, time_delta / 1000));
        }
    }
}

FrameInstance* construct(Nucleus& nucl, size_t tick,
                         std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>>& instances,
                         std::unordered_set<FrameInstance*>& alive,
                         std::unordered_map<Substrate*, std::pair<bool, std::unordered_set<FrameInstance*>>>& neck,
                         std::unordered_set<std::pair<Substrate*, size_t>>& history,
                         std::unordered_map<Substrate*, unsigned>& miss, Substrate* substrate, size_t frame_idx,
                         std::unique_ptr<IOutput::Callback> callback, bool missed) noexcept {
    auto key = std::make_pair(substrate, frame_idx);
    if (auto it = instances.find(key); it != instances.end()) {
        auto inst = it->second.get();
        if (callback) {
            if (inst->product)
                (*callback)(inst->product.get(), {});
            else
                inst->callback = std::move(callback);
        }
        return inst;
    }
    if (auto it = history.find(key); it != history.end() && !missed) {
        nucl.logger.log(LogLevel::DEBUG, format_c("Nucleus: frame {} of substrate {} need to recalculate", frame_idx,
                                                  static_cast<void*>(substrate)));
        missed = true;
        ++miss[substrate];
    } else
        history.emplace(key);

    auto promoter = substrate->filters.find(position_zero)->second;
    if (substrate->filters.size() == 1)
        for (const auto& thread : nucl.worker_threads)
            if (auto id = thread.get_id(); !substrate->filters.contains(id))
                substrate->filters.emplace(id, promoter.clone());

    FrameData* frame_data = nullptr;
    promoter->get_frame_data(frame_idx, &frame_data);
    auto instc = std::make_unique<FrameInstance>(substrate, frame_data, tick);
    for (size_t i = 0; i < frame_data->dependency_count; ++i) {
        auto dep = frame_data->dependencies[i];
        auto input = construct(nucl, tick, instances, alive, neck, history, miss,
                               &dynamic_cast<Substrate&>(*const_cast<ISubstrate*>(dep.substrate)), dep.frame_idx,
                               nullptr, missed);
        instc->inputs.emplace_back(input);
        input->outputs.emplace_back(instc.get());
    }

    auto ff = promoter->get_filter_flags();
    if ((ff & ffMakeLinear) && frame_idx)
        if (auto prev = instances.find(std::make_pair(substrate, frame_idx - 1)); prev != instances.end()) {
            auto input = prev->second.get();
            instc->inputs.emplace_back(input);
            input->outputs.emplace_back(instc.get());
            instc->false_dep = true;
        }
    if (ff & ffSingleThreaded)
        instc->single_threaded = true;

    if (auto it = miss.find(substrate); it != miss.end())
        instc->indulgence = it->second / 8;

    instc->callback = std::move(callback);
    auto inst = instances.emplace(key, std::move(instc)).first->second.get();
    alive.emplace(inst);

    if (check_all_inputs_ready(inst))
        post_work(nucl, inst, neck);

    return inst;
}

bool kill_tree(FrameInstance* inst, std::unordered_set<FrameInstance*>& alive, std::exception_ptr exc) noexcept {
    bool handled = false;
    if (inst->callback) {
        (*inst->callback)(nullptr, exc);
        handled = true;
    }
    for (auto output : inst->outputs)
        if (alive.find(output) != alive.end() && kill_tree(output, alive, exc))
            handled = true;
    alive.erase(inst);
    return handled;
}

void post_work(Nucleus& nucl, FrameInstance* inst,
               std::unordered_map<Substrate*, std::pair<bool, std::unordered_set<FrameInstance*>>>& neck) noexcept {
    if (!inst->single_threaded)
        post_work_direct(nucl, inst);
    else
        neck[inst->substrate.get()].second.insert(inst);
}

void callbacker(Nucleus& nucl) noexcept {
    while (true) {
        nucl.callback_semaphore.acquire();
        if (nucl.stop.load(std::memory_order_acquire))
            break;
        nucl.callback_queue.consume_all([](CallbackTask task) {
            auto callback = std::unique_ptr<IOutput::Callback>(task.callback);
            auto frame = cat_ptr<const IFrame>(task.frame, false);
            auto exc = move_out_exc(task.exc);
            (*callback)(frame.get(), exc);
        });
    }
}

class Output final : public Object, public IOutput, public Shuttle {
  public:
    cat_ptr<Substrate> substrate;

    void get_frame(size_t frame_idx, Callback cb) noexcept final {
        std::array<std::byte, MaintainTask::payload_size> pl;
        *reinterpret_cast<Callback**>(pl.data()) =
            new Callback([cb = std::move(cb), &nucl = this->nucl](const IFrame* frame, std::exception_ptr exc) {
                if (frame)
                    frame->add_ref();
                nucl.callback_queue.push(CallbackTask{
                    new Callback(std::move(const_cast<IOutput::Callback&>(cb))),
                    frame,
                    move_in_exc(exc),
                });
                nucl.callback_semaphore.release();
            });
        post_maintain_task(nucl, MaintainTask::Type::Construct, substrate.get(), frame_idx, pl);
    }

    explicit Output(Nucleus& nucl, ISubstrate* substrate) noexcept
        : Shuttle(nucl), substrate(&dynamic_cast<Substrate&>(*substrate)) {}
};

void Nucleus::create_output(ISubstrate* substrate, IOutput** output) noexcept {
    create_instance<Output>(output, *this, substrate);
}
