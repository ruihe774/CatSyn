#include <fmt/core.h>

#include <porphyrin.h>

std::vector<std::unique_ptr<VSCore>> cores;
std::shared_mutex cores_mutex;

VSCore* createCore(int threads) noexcept {
    static struct VersionString {
        char buf[1024];
        VersionString() {
            fmt::format_to(buf,
                           "{}\n"
                           "Metalloporphyrin {} ({})\n"
                           "{}\n"
                           "Simulating VapourSynth Core R{}, API {}.{}\n",
                           catsyn::get_version().string, version_string, description, copyright, vs_core_version,
                           VAPOURSYNTH_API_MAJOR, VAPOURSYNTH_API_MINOR);
        }
    } vs;

    catsyn::cat_ptr<catsyn::INucleus> nucl;
    catsyn::create_nucleus(nucl.put());
    auto core = std::unique_ptr<VSCore>(
        new VSCore{std::move(nucl), VSCoreInfo{vs.buf, vs_core_version, VAPOURSYNTH_API_VERSION, 0, 0, 0}});
    if (threads > 0) {
        auto cfg = core->nucl->get_config();
        cfg.thread_count = threads;
        core->nucl->set_config(cfg);
    }
    auto pcore = core.get();
    std::unique_lock<std::shared_mutex> lock(cores_mutex);
    if (!sink.handlers.empty())
        core->nucl->get_logger()->set_sink(&sink);
    if (!cores.empty())
        core->nucl->get_logger()->log(
            catsyn::LogLevel::WARNING,
            "Metalloporphyrin: multiple cores created; logs will only be sent to the first created core");
    cores.emplace_back(std::move(core));
    return pcore;
}

[[noreturn]] static void invalid_core_pointer() {
    throw std::runtime_error("invalid VSCore pointer");
}

void freeCore(VSCore* core) noexcept {
    std::unique_lock<std::shared_mutex> lock(cores_mutex);
    if (auto it = std::find_if(cores.begin(), cores.end(), [core](const auto& p) { return p.get() == core; });
        it != cores.end())
        cores.erase(it);
    else
        invalid_core_pointer();
}

const VSCoreInfo* getCoreInfo(VSCore* core) noexcept {
    auto cfg = core->nucl->get_config();
    core->ci.numThreads = static_cast<int>(cfg.thread_count);
    core->ci.maxFramebufferSize = static_cast<int64_t>(cfg.mem_hint_mb) << 20;
    return &core->ci;
}

void getCoreInfo2(VSCore* core, VSCoreInfo* info) noexcept {
    *info = *getCoreInfo(core);
}

int64_t setMaxCacheSize(int64_t bytes, VSCore* core) noexcept {
    auto cfg = core->nucl->get_config();
    cfg.mem_hint_mb = bytes >> 20;
    core->nucl->set_config(cfg);
    return bytes;
}

int setThreadCount(int threads, VSCore* core) noexcept {
    auto cfg = core->nucl->get_config();
    cfg.thread_count = threads > 0 ? threads : std::thread::hardware_concurrency();
    core->nucl->set_config(cfg);
    return static_cast<int>(cfg.thread_count);
}
