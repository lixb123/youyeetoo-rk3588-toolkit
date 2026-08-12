#include "camera_app.h"

#include "main_service.h"

namespace youyeetoo {
int CameraApp::Run(const CameraAppOptions& options) {
    MainServiceApp service_app;
    return service_app.Run(options);
}

}  // namespace youyeetoo
