#include <stdio.h>

#include <porphyrin.h>

UserLogSink sink;

static VSMessageType loglevel_to_msgtype(catsyn::LogLevel level) noexcept {
    switch (level) {
    case catsyn::LogLevel::DEBUG:
        return mtDebug;
    case catsyn::LogLevel::INFO:
    case catsyn::LogLevel::WARNING:
        return mtWarning;
    default:
        panic("unknown log level");
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
    std::destroy_at(this);
    handler = other.handler;
    freer = other.freer;
    userData = other.userData;
    id = other.id;
    other.freer = nullptr;
    return *this;
}

UserLogSink::HandlerInstance::HandlerInstance(VSMessageHandler handler, VSMessageHandlerFree freer, void* userData,
                                              int id) noexcept
    : handler(handler), freer(freer), userData(userData), id(id) {}

void UserLogSink::send_log(catsyn::LogLevel level, const char* msg) noexcept {
    auto mt = loglevel_to_msgtype(level);
    for (auto&& handler : handlers)
        handler.handler(mt, msg, handler.userData);
}

void setMessageHandler(VSMessageHandler handler, void* userData) noexcept {
    sink.handlers.clear();
    if (handler) {
        sink.handlers.emplace_back(handler, nullptr, userData, 0);
        core->nucl->get_logger()->set_sink(&sink);
    } else
        core->nucl->get_logger()->set_sink(nullptr);
}

int addMessageHandler(VSMessageHandler handler, VSMessageHandlerFree free, void* userData) noexcept {
    if (handler) {
        auto empty = sink.handlers.empty();
        auto id = empty ? 0 : sink.handlers.back().id + 1;
        sink.handlers.emplace_back(handler, free, userData, id);
        if (empty)
            core->nucl->get_logger()->set_sink(&sink);
        return id;
    } else {
        sink.handlers.clear();
        core->nucl->get_logger()->set_sink(nullptr);
        return -1;
    }
}

int removeMessageHandler(int id) noexcept {
    if (auto it =
            std::find_if(sink.handlers.begin(), sink.handlers.end(), [id](auto&& handler) { return handler.id == id; });
        it != sink.handlers.end()) {
        sink.handlers.erase(it);
        if (sink.handlers.empty())
            core->nucl->get_logger()->set_sink(nullptr);
        return 1;
    } else
        return 0;
}

static catsyn::LogLevel msgtype_to_loglevel(int mt, const char* msg) noexcept {
    switch (mt) {
    case mtDebug:
        return catsyn::LogLevel::DEBUG;
    case mtWarning:
    case mtCritical:
        return catsyn::LogLevel::WARNING;
    case mtFatal:
        panic(msg);
    default:
        panic("unknown message type");
    }
}

void logMessage(int mt, const char* msg) noexcept {
    if (core)
        core->nucl->get_logger()->log(msgtype_to_loglevel(mt, msg), msg);
    else
        fprintf(stderr, "%s\n", msg);
}
