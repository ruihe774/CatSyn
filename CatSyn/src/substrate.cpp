#include <set>
#include <unordered_map>
#include <unordered_set>

#include <boost/container/flat_set.hpp>
#include <boost/functional/hash.hpp>

#include <catimpl.h>

struct FrameInstance {
    const cat_ptr<Substrate> substrate;
    cat_ptr<const IFrame> product;
    boost::container::small_vector<FrameInstance*, 10> inputs;
    boost::container::small_vector<FrameInstance*, 30> outputs;
    cat_ptr<ICallback> callback;
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

bool FrameInstanceTickGreater::operator()(const FrameInstance* l, const FrameInstance* r) const noexcept {
    if (auto cmp = l->tick <=> r->tick; cmp == 0)
        return l > r;
    else
        return cmp > 0;
}

Substrate::Substrate(Nucleus& nucl, cat_ptr<const IFilter> filter) noexcept {
    this->filter = filter.usurp_or_clone();
}

VideoInfo Substrate::get_video_info() const noexcept {
    return filter->get_video_info();
}

ISubstrate* Nucleus::register_filter(const IFilter* filter) noexcept {
    auto& out = substrates[filter];
    if (!out)
        create_instance<Substrate>(out.put(), *this, wrap_cat_ptr(filter));
    return out.get();
}

void Nucleus::unregister_filter(const IFilter* filter) noexcept {
    substrates.erase(filter);
}

MaintainTask::MaintainTask(cat_ptr<Substrate> substrate, size_t frame_idx, cat_ptr<ICallback> callback) noexcept
    : variant(Construct{std::move(substrate), frame_idx, std::move(callback)}) {}
MaintainTask::MaintainTask(FrameInstance* inst, std::exception_ptr exc) noexcept : variant(Notify{inst, exc}) {}

static void worker(Nucleus&);
static void maintainer(Nucleus&);
static void callbacker(Nucleus&);

void Nucleus::react() noexcept {
    if (maintainer_thread)
        return;
    maintainer_thread = JThread(maintainer, std::ref(*this));
    set_thread_priority(maintainer_thread.value(), 1);
    callback_thread = JThread(callbacker, std::ref(*this));
    set_thread_priority(callback_thread.value(), 1);
    for (size_t i = 0; i < config.thread_count; ++i)
        worker_threads.emplace_back(worker, std::ref(*this));
    logger.log(LogLevel::DEBUG, "Nucleus: reaction started");
}

bool Nucleus::is_reacting() const noexcept {
    return !!maintainer_thread;
}

Nucleus::~Nucleus() {
    maintain_queue.request_stop();
    callback_queue.request_stop();
    work_queue.request_stop();
}

template<typename... Args> static void post_maintain_task(Nucleus& nucl, Args&&... args) noexcept {
    nucl.maintain_queue.push(MaintainTask{std::forward<Args>(args)...});
}

static void post_work_direct(Nucleus& nucl, FrameInstance* inst) noexcept {
    nucl.work_queue.push(inst);
}

void worker(Nucleus& nucl) {
    boost::container::flat_set<Substrate*> inited;
    nucl.work_queue.stream([&](FrameInstance* inst) {
        if (inst->taken.test_and_set(std::memory_order_acq_rel))
            return;
        auto substrate = inst->substrate.get();
        auto filter = substrate->filter.get();
        std::atomic_uint* init_atomic = nullptr;
        if (auto filter1 = dynamic_cast<IFilter1*>(filter); filter1)
            init_atomic = filter1->get_thread_init_atomic();
        WedgeLock lock;
        if (init_atomic) {
            WedgeLock inner{*init_atomic};
            std::swap(lock, inner);
            if (inited.contains(substrate)) {
                if (!lock.try_lock_shared()) [[unlikely]]
                    goto repost;
            } else [[unlikely]] {
                if (lock.try_lock_exclusive())
                    inited.emplace(substrate);
                else
                    goto repost;
            }
        }
        {
            boost::container::small_vector<const IFrame*, 10> input_frames;
            for (auto input : inst->inputs)
                input_frames.push_back(input->product.get());
            cat_ptr<const IFrame> product;
            try {
                filter->process_frame(input_frames.data(), &inst->frame_data, product.put_const());
                filter->drop_frame_data(inst->frame_data);
            } catch (...) {
                post_maintain_task(nucl, inst, std::current_exception());
                return;
            }
            inst->product = std::move(product);
            post_maintain_task(nucl, inst);
            return;
        }
    repost:
        ++inst->tick;
        inst->taken.clear(std::memory_order_release);
        nucl.work_queue.push(inst);
    });
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
construct(Nucleus& nucl, size_t tick,
          std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>>& instances,
          std::unordered_set<FrameInstance*>& alive,
          std::unordered_map<Substrate*, std::pair<bool, std::set<FrameInstance*, FrameInstanceTickGreater>>>& neck,
          std::unordered_set<std::pair<Substrate*, size_t>>& history, std::unordered_map<Substrate*, unsigned>& miss,
          Substrate* substrate, size_t frame_idx, cat_ptr<ICallback> callback = {}, bool missed = false) noexcept;

static void kill_tree(Nucleus& nucl, FrameInstance* inst, std::unordered_set<FrameInstance*>& alive,
                      std::exception_ptr exc) noexcept;

static void post_work(
    Nucleus& nucl, FrameInstance* inst,
    std::unordered_map<Substrate*, std::pair<bool, std::set<FrameInstance*, FrameInstanceTickGreater>>>& neck) noexcept;

static void post_callback(Nucleus& nucl, cat_ptr<ICallback>&& callback, cat_ptr<const IFrame> frame,
                          std::exception_ptr exc) noexcept {
    auto cb{std::move(callback)};
    nucl.callback_queue.push(CallbackTask{[=]() { cb->invoke(frame.get(), exc); }});
}

void maintainer(Nucleus& nucl) {
    std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>> instances;
    std::unordered_set<FrameInstance*> alive;
    std::unordered_map<Substrate*, std::pair<bool, std::set<FrameInstance*, FrameInstanceTickGreater>>> neck;
    std::unordered_set<std::pair<Substrate*, size_t>> history;
    std::unordered_map<Substrate*, unsigned> miss;
    size_t tick = 0;
    while (true) {
        bool constructed = false;
        nucl.maintain_queue.consume_all([&](MaintainTask&& task) {
            if (task.index()) {
                auto& t = std::get<Notify>(task);
                auto inst = t.inst;
                auto exc = t.exc;
                if (alive.find(inst) != alive.end()) {
                    if (inst->single_threaded) {
                        auto& item = neck[inst->substrate.get()];
                        item.first = false;
                        item.second.erase(inst);
                        inst->single_threaded = false;
                    }
                    if (!exc)
                        for (auto output : inst->outputs)
                            if (alive.find(output) != alive.end() && !output->product && check_all_inputs_ready(output))
                                post_work(nucl, output, neck);
                    if (exc) {
                        kill_tree(nucl, inst, alive, exc);
                        for (auto it = instances.begin(); it != instances.end();) {
                            if (alive.find(it->second.get()) == alive.end())
                                it = instances.erase(it);
                            else
                                ++it;
                        }
                    } else if (inst->callback)
                        post_callback(nucl, std::move(inst->callback), inst->product, exc);
                }
            } else {
                auto& t = std::get<Construct>(task);
                construct(nucl, tick, instances, alive, neck, history, miss, t.substrate.get(), t.frame_idx,
                          std::move(t.callback));
                constructed = true;
            }
            for (auto& item : neck)
                if (auto& sec = item.second; !sec.first && !sec.second.empty()) {
                    auto top = sec.second.end();
                    post_work_direct(nucl, *--top);
                    sec.second.erase(top);
                    sec.first = true;
                }
        });
        ++tick;
        if (constructed) {
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
            if (history.size() > 65535)
                history.clear();
        }
    }
}

FrameInstance*
construct(Nucleus& nucl, size_t tick,
          std::unordered_map<std::pair<Substrate*, size_t>, std::unique_ptr<FrameInstance>>& instances,
          std::unordered_set<FrameInstance*>& alive,
          std::unordered_map<Substrate*, std::pair<bool, std::set<FrameInstance*, FrameInstanceTickGreater>>>& neck,
          std::unordered_set<std::pair<Substrate*, size_t>>& history, std::unordered_map<Substrate*, unsigned>& miss,
          Substrate* substrate, size_t frame_idx, cat_ptr<ICallback> callback, bool missed) noexcept {
    auto key = std::make_pair(substrate, frame_idx);
    if (auto it = instances.find(key); it != instances.end()) {
        auto inst = it->second.get();
        if (callback) {
            if (inst->product)
                post_callback(nucl, std::move(callback), inst->product, {});
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

    auto filter = substrate->filter.get();
    FrameData* frame_data = nullptr;
    filter->get_frame_data(frame_idx, &frame_data);
    auto instc = std::make_unique<FrameInstance>(substrate, frame_data, tick);
    for (size_t i = 0; i < frame_data->dependency_count; ++i) {
        auto dep = frame_data->dependencies[i];
        auto input = construct(nucl, tick, instances, alive, neck, history, miss,
                               &dynamic_cast<Substrate&>(*const_cast<ISubstrate*>(dep.substrate)), dep.frame_idx,
                               nullptr, missed);
        instc->inputs.emplace_back(input);
        input->outputs.emplace_back(instc.get());
    }

    auto ff = filter->get_filter_flags();
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

void kill_tree(Nucleus& nucl, FrameInstance* inst, std::unordered_set<FrameInstance*>& alive,
               std::exception_ptr exc) noexcept {
    inst->substrate->filter->drop_frame_data(inst->frame_data);
    if (inst->callback)
        post_callback(nucl, std::move(inst->callback), nullptr, exc);
    for (auto output : inst->outputs)
        if (alive.find(output) != alive.end())
            kill_tree(nucl, output, alive, exc);
    alive.erase(inst);
}

void post_work(Nucleus& nucl, FrameInstance* inst,
               std::unordered_map<Substrate*, std::pair<bool, std::set<FrameInstance*, FrameInstanceTickGreater>>>&
                   neck) noexcept {
    if (!inst->single_threaded)
        post_work_direct(nucl, inst);
    else
        neck[inst->substrate.get()].second.insert(inst);
}

void callbacker(Nucleus& nucl) {
    nucl.callback_queue.stream([](CallbackTask&& task) { task.callback(); });
}

class Output final : public Object, virtual public IOutput, public Shuttle {
  public:
    cat_ptr<Substrate> substrate;

    void get_frame(size_t frame_idx, ICallback* cb) noexcept final {
        post_maintain_task(nucl, substrate, frame_idx, cb);
    }

    explicit Output(Nucleus& nucl, ISubstrate* substrate) noexcept
        : Shuttle(nucl), substrate(&dynamic_cast<Substrate&>(*substrate)) {}
};

void Nucleus::create_output(ISubstrate* substrate, IOutput** output) noexcept {
    create_instance<Output>(output, *this, substrate);
}
