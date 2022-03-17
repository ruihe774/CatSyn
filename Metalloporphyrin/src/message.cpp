#include <stdio.h>

#include <porphyrin.h>

UserLogSink sink;

static VSMessageType loglevel_to_msgtype(catsyn::LogLevel level) {
    switch (level) {
    case catsyn::LogLevel::DEBUG:
        return mtDebug;
    case catsyn::LogLevel::INFO:
    case catsyn::LogLevel::WARNING:
        return mtWarning;
    default:
        throw std::logic_error("unknown log level");
    }
}

UserLogSink::HandlerInstance::~HandlerInstance() {
    if (freer)
        freer(userData);
}

UserLogSink::HandlerInstance::HandlerInstance(HandlerInstance&& other) noexcept
    : handler(other.handler), freer(other.freer), userData(other.userData), id(other.id) {
    other.freer = nullptr;
}

UserLogSink::HandlerInstance& UserLogSink::HandlerInstance::operator=(UserLogSink::HandlerInstance&& other) noexcept {
    handler = other.handler;
    freer = other.freer;
    userData = other.userData;
    id = other.id;
    other.freer = nullptr;
    return *this;
}

UserLogSink::HandlerInstance::HandlerInstance(VSMessageHandler handler, VSMessageHandlerFree freer, void* userData, int id) noexcept
    : handler(handler), freer(freer), userData(userData), id(id) {}

void UserLogSink::send_log(catsyn::LogLevel level, const char* msg) noexcept {
    auto mt = loglevel_to_msgtype(level);
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    for (const auto& handler : handlers)
        handler.handler(mt, msg, handler.userData);
}

void setMessageHandler(VSMessageHandler handler, void* userData) noexcept {
    std::unique_lock<std::shared_mutex> cores_lock(cores_mutex);
    sink.handlers.clear();
    if (handler) {
        sink.handlers.emplace_back(handler, nullptr, userData, 0);
        for (auto& core : cores)
            core->nucl->get_logger()->set_sink(&sink);
    } else
        for (auto& core : cores)
            core->nucl->get_logger()->set_sink(nullptr);
}

int addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void* userData) noexcept {
    std::unique_lock<std::shared_mutex> cores_lock(cores_mutex);
    if (handler) {
        auto empty = sink.handlers.empty();
        auto id = empty ? 0 : sink.handlers.back().id + 1;
        sink.handlers.emplace_back(handler, free, userData, id);
        if (empty)
            for (auto& core : cores)
                core->nucl->get_logger()->set_sink(&sink);
        return id;
    } else {
        sink.handlers.clear();
        for (auto& core : cores)
            core->nucl->get_logger()->set_sink(nullptr);
        return -1;
    }
}

int removeMessageHandler(int id) noexcept {
    std::unique_lock<std::shared_mutex> cores_lock(cores_mutex);
    if (auto it = std::find_if(sink.handlers.begin(), sink.handlers.end(),
                               [id](const auto& handler) { return handler.id == id; });
        it != sink.handlers.end()) {
        sink.handlers.erase(it);
        return 1;
    } else
        return 0;
}

static catsyn::LogLevel msgtype_to_loglevel(int mt, const char* msg) {
    switch (mt) {
    case mtDebug:
        return catsyn::LogLevel::DEBUG;
    case mtWarning:
    case mtCritical:
        return catsyn::LogLevel::WARNING;
    case mtFatal:
        throw std::runtime_error(msg);
    default:
        throw std::logic_error("unknown message type");
    }
}

void logMessage(int mt, const char* msg) noexcept {
    std::shared_lock<std::shared_mutex> lock(cores_mutex);
    if (cores.empty())
        fprintf(stderr, "%s\n", msg);
    else
        cores.front()->nucl->get_logger()->log(msgtype_to_loglevel(mt, msg), msg);
}
