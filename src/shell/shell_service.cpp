#include "shell_service.hpp"

#include "shell.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace tetra::shell {

namespace {
constexpr const char* kName = "org.tetra.Shell";
constexpr const char* kPath = "/org/tetra/Shell";
constexpr const char* kInterface = "org.tetra.Shell1";
constexpr const char* kPortalName = "org.freedesktop.impl.portal.desktop.tetra";
constexpr const char* kCancelled = "org.tetra.Shell1.Error.Cancelled";
constexpr const char* kFailed = "org.tetra.Shell1.Error.Failed";
constexpr const char* kDenied = "org.freedesktop.DBus.Error.AccessDenied";
} // namespace

ShellService::ShellService(Shell& shell, sdbus::IConnection& connection)
    : shell_(shell), connection_(connection) {
    bus_ = sdbus::createProxy(connection_, sdbus::ServiceName{"org.freedesktop.DBus"},
                              sdbus::ObjectPath{"/org/freedesktop/DBus"});
    object_ = sdbus::createObject(connection_, sdbus::ObjectPath{kPath});
    object_
        ->addVTable(sdbus::registerMethod("Screenshot")
                        .withInputParamNames("interactive")
                        .withOutputParamNames("path")
                        .implementedAs([this](sdbus::Result<std::string>&& result, bool interactive) {
                            if (!caller_is_portal()) {
                                result.returnError(sdbus::Error(sdbus::Error::Name{kDenied},
                                                                "Only the tetra portal may ask for this"));
                                return;
                            }
                            auto shared = std::make_shared<sdbus::Result<std::string>>(std::move(result));
                            // Without a picture: the user backed out of the
                            // picker (interactive), or it failed.
                            shell_.screenshots().request(
                                interactive, [shared, interactive](std::optional<std::filesystem::path> path) {
                                    if (path) {
                                        shared->returnResults(path->string());
                                    } else {
                                        shared->returnError(sdbus::Error(
                                            sdbus::Error::Name{interactive ? kCancelled : kFailed},
                                            "No screenshot"));
                                    }
                                });
                        }))
        .forInterface(sdbus::InterfaceName{kInterface});
    connection_.requestName(sdbus::ServiceName{kName});
}

ShellService::~ShellService() {
    try {
        connection_.releaseName(sdbus::ServiceName{kName});
    } catch (const sdbus::Error&) {
        // the bus is gone
    }
}

bool ShellService::caller_is_portal() {
    const sdbus::Message message = object_->getCurrentlyProcessedMessage();
    const char* sender = message.getSender();
    if (sender == nullptr) {
        return false;
    }
    try {
        std::string owner;
        bus_->callMethod("GetNameOwner")
            .onInterface(sdbus::InterfaceName{"org.freedesktop.DBus"})
            .withArguments(std::string(kPortalName))
            .storeResultsTo(owner);
        return owner == sender;
    } catch (const sdbus::Error&) {
        return false; // no portal running
    }
}

} // namespace tetra::shell
