#include "protocol.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>

namespace tetra::greeter {

using nlohmann::json;

std::string frame(const std::string& text) {
    const auto length = static_cast<uint32_t>(text.size());
    std::string out(sizeof(length), '\0');
    std::memcpy(out.data(), &length, sizeof(length));
    out += text;
    return out;
}

std::vector<std::string> unframe(std::string& buffer) {
    std::vector<std::string> messages;
    std::size_t offset = 0;
    while (buffer.size() - offset >= sizeof(uint32_t)) {
        uint32_t length = 0;
        std::memcpy(&length, buffer.data() + offset, sizeof(length));
        if (buffer.size() - offset - sizeof(length) < length) {
            break;
        }
        messages.push_back(buffer.substr(offset + sizeof(length), length));
        offset += sizeof(length) + length;
    }
    buffer.erase(0, offset);
    return messages;
}

Reply parse_reply(const std::string& text) {
    Reply reply;
    const json message = json::parse(text, nullptr, false);
    if (!message.is_object() || !message.contains("type") || !message["type"].is_string()) {
        return reply;
    }
    auto string = [&](const char* key) {
        auto it = message.find(key);
        return it != message.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    const std::string type = string("type");
    if (type == "success") {
        reply.type = Reply::Type::Success;
    } else if (type == "error") {
        reply.type = Reply::Type::Error;
        reply.error_type = string("error_type");
        reply.description = string("description");
    } else if (type == "auth_message") {
        const std::string kind = string("auth_message_type");
        if (kind == "visible") {
            reply.prompt = Reply::Prompt::Visible;
        } else if (kind == "secret") {
            reply.prompt = Reply::Prompt::Secret;
        } else if (kind == "info") {
            reply.prompt = Reply::Prompt::Info;
        } else if (kind == "error") {
            reply.prompt = Reply::Prompt::Error;
        } else {
            return reply;
        }
        reply.type = Reply::Type::AuthMessage;
        reply.message = string("auth_message");
    }
    return reply;
}

std::string create_session(const std::string& username) {
    return json{{"type", "create_session"}, {"username", username}}.dump();
}

std::string post_auth_message_response(const std::optional<std::string>& response) {
    json request{{"type", "post_auth_message_response"}};
    if (response) {
        request["response"] = *response;
    }
    return request.dump();
}

std::string start_session(const std::vector<std::string>& cmd, const std::vector<std::string>& env) {
    return json{{"type", "start_session"}, {"cmd", cmd}, {"env", env}}.dump();
}

std::string cancel_session() {
    return json{{"type", "cancel_session"}}.dump();
}

} // namespace tetra::greeter
