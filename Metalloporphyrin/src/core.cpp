#include <fmt/core.h>

#include <porphyrin.h>

std::vector<std::unique_ptr<VSCore>> cores;
std::shared_mutex cores_mutex;

VSCore* createCore(int threads, bool temporary) noexcept {
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
    static bool has_temp_core = false;
    if (has_temp_core) {
        has_temp_core = false;
        auto core = cores.front().get();
        setThreadCount(threads, core);
        return core;
    }
    has_temp_core = temporary;
    catsyn::cat_ptr<catsyn::INucleus> nucl;
    catsyn::create_nucleus(nucl.put());
    std::unique_ptr<VSCore> core(
        new VSCore{std::move(nucl), VSCoreInfo{vs.buf, vs_core_version, VAPOURSYNTH_API_VERSION, 0, 0, 0}});
    if (threads > 0) {
        auto cfg = core->nucl->get_config();
        cfg.thread_count = threads;
        core->nucl->set_config(cfg);
    }
    auto ribosomes = core->nucl->get_ribosomes();
    auto size = ribosomes->size();
    auto vsr = new VSRibosome(core.get());
    ribosomes->set_key(size, vsr->get_identifier());
    ribosomes->set(size, vsr);
    auto finders = core->nucl->get_enzyme_finders();
    catsyn::cat_ptr<catsyn::IEnzymeFinder> finder;
    core->nucl->get_factory()->create_dll_enzyme_finder("@/vapoursynth64/coreplugins/", finder.put());
    finders->set(catsyn::ITable::npos, finder.get());
    core->nucl->get_factory()->create_dll_enzyme_finder("@/vapoursynth64/plugins/", finder.put());
    finders->set(catsyn::ITable::npos, finder.get());
    core->nucl->synthesize_enzymes();
    std::unique_lock<std::shared_mutex> lock(cores_mutex);
    if (!sink.handlers.empty())
        core->nucl->get_logger()->set_sink(&sink);
    if (!cores.empty())
        core->nucl->get_logger()->log(
            catsyn::LogLevel::WARNING,
            "Metalloporphyrin: multiple cores created; logs will only be sent to the first created core");
    auto pcore = core.get();
    cores.emplace_back(std::move(core));
    return pcore;
}

VSCore* createCore(int threads) noexcept {
    return createCore(threads, false);
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
