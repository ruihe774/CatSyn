#include <fmt/core.h>

#include <porphyrin.h>

VSCore* createCore() noexcept {
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
    auto core = new VSCore{nucl, VSCoreInfo{vs.buf, vs_core_version, VAPOURSYNTH_API_VERSION, 0, 0, 0}};

    auto factory = nucl->get_factory();

    auto vsr = new VSRibosome;
    nucl->get_ribosomes()->set(catsyn::ITable::npos, vsr, vsr->get_identifier());

    auto finders = nucl->get_enzyme_finders();
    catsyn::cat_ptr<catsyn::IEnzymeFinder> finder;
    factory->create_dll_enzyme_finder("@/vapoursynth64/coreplugins/", finder.put());
    finders->set(catsyn::ITable::npos, finder.get(), nullptr);
    factory->create_dll_enzyme_finder("@/vapoursynth64/plugins/", finder.put());
    finders->set(catsyn::ITable::npos, finder.get(), nullptr);

    return core;
}

std::unique_ptr<VSCore> core{createCore()};
static bool created{false};

[[noreturn]] static void multiple_cores() {
    throw std::logic_error("only one core can be created per process");
}

VSCore* createCore(int threadCount) noexcept {
    if (created)
        multiple_cores();
    created = true;
    setThreadCount(threadCount, core.get());
    core->nucl->synthesize_enzymes();
    return core.get();
}

void freeCore(VSCore*) noexcept {
    core.reset(createCore());
    created = false;
}

const VSCoreInfo* getCoreInfo(VSCore*) noexcept {
    auto cfg = core->nucl->get_config();
    core->ci.numThreads = static_cast<int>(cfg.thread_count);
    core->ci.maxFramebufferSize = static_cast<int64_t>(cfg.mem_hint_mb) << 20;
    return &core->ci;
}

void getCoreInfo2(VSCore*, VSCoreInfo* info) noexcept {
    *info = *getCoreInfo(core.get());
}

int64_t setMaxCacheSize(int64_t bytes, VSCore*) noexcept {
    auto cfg = core->nucl->get_config();
    cfg.mem_hint_mb = bytes >> 20;
    core->nucl->set_config(cfg);
    return bytes;
}

int setThreadCount(int threads, VSCore*) noexcept {
    auto cfg = core->nucl->get_config();
    cfg.thread_count = threads;
    core->nucl->set_config(cfg);
    return getCoreInfo(core.get())->numThreads;
}
