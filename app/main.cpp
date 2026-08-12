#include <iostream>
#include <stdexcept>
#include <string>

#include "camera_app.h"

namespace {

bool ParsePositiveInt(const std::string& value, int* output) {
    try {
        *output = std::stoi(value);
    } catch (const std::exception&) {
        return false;
    }
    return *output > 0;
}

bool ParseNonNegativeInt(const std::string& value, int* output) {
    try {
        *output = std::stoi(value);
    } catch (const std::exception&) {
        return false;
    }
    return *output >= 0;
}

void PrintUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --debug                 Enable verbose SDK logging\n"
        << "  --log-path PATH         SDK log output path\n"
        << "  --serial SERIAL         Select camera by serial number\n"
        << "  --timeout-ms MS         Module call timeout in service mode\n"
        << "  --output-dir DIR        Default download directory for service tasks\n"
        << "  --command-defs PATH     Telemetry command definition file\n"
        << "  --command-inbox PATH    Telemetry command inbox file\n"
        << "  --port-map PATH         Camera port-to-slot mapping config (CAM-003)\n"
        << "  --can-config PATH       CAN service config file (interfaces, CAN IDs, intervals)\n"
        << "  --poll-interval-ms MS   Service status polling interval\n"
        << "  --loop-interval-ms MS   Service loop sleep interval\n"
        << "  --max-loops N           Exit service loop after N iterations, 0 means forever\n";
}

bool ParseArgs(int argc, char* argv[], youyeetoo::CameraAppOptions* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--debug") {
            options->debug = true;
        } else if (arg == "--log-path") {
            if (i + 1 >= argc) {
                std::cerr << "--log-path requires a path." << std::endl;
                return false;
            }
            options->log_path = argv[++i];
        } else if (arg == "--serial") {
            if (i + 1 >= argc) {
                std::cerr << "--serial requires a serial number." << std::endl;
                return false;
            }
            options->serial_number = argv[++i];
        } else if (arg == "--timeout-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--timeout-ms requires a value." << std::endl;
                return false;
            }
            try {
                options->timeout_ms = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "--timeout-ms requires an integer value." << std::endl;
                return false;
            }
            if (options->timeout_ms <= 0) {
                std::cerr << "--timeout-ms must be positive." << std::endl;
                return false;
            }
        } else if (arg == "--output-dir") {
            if (i + 1 >= argc) {
                std::cerr << "--output-dir requires a directory." << std::endl;
                return false;
            }
            options->output_dir = argv[++i];
        } else if (arg == "--poll-interval-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--poll-interval-ms requires a value." << std::endl;
                return false;
            }
            if (!ParsePositiveInt(argv[++i], &options->poll_interval_ms)) {
                std::cerr << "--poll-interval-ms must be a positive integer." << std::endl;
                return false;
            }
        } else if (arg == "--loop-interval-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--loop-interval-ms requires a value." << std::endl;
                return false;
            }
            if (!ParsePositiveInt(argv[++i], &options->loop_interval_ms)) {
                std::cerr << "--loop-interval-ms must be a positive integer." << std::endl;
                return false;
            }
        } else if (arg == "--max-loops") {
            if (i + 1 >= argc) {
                std::cerr << "--max-loops requires a value." << std::endl;
                return false;
            }
            if (!ParseNonNegativeInt(argv[++i], &options->max_loops)) {
                std::cerr << "--max-loops must be a non-negative integer." << std::endl;
                return false;
            }
        } else if (arg == "--command-defs") {
            if (i + 1 >= argc) {
                std::cerr << "--command-defs requires a path." << std::endl;
                return false;
            }
            options->telemetry_command_defs = argv[++i];
        } else if (arg == "--command-inbox") {
            if (i + 1 >= argc) {
                std::cerr << "--command-inbox requires a path." << std::endl;
                return false;
            }
            options->telemetry_command_inbox = argv[++i];
        } else if (arg == "--port-map") {
            if (i + 1 >= argc) {
                std::cerr << "--port-map requires a path." << std::endl;
                return false;
            }
            options->port_map_path = argv[++i];
        } else if (arg == "--can-config") {
            if (i + 1 >= argc) {
                std::cerr << "--can-config requires a path." << std::endl;
                return false;
            }
            options->can_config_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    youyeetoo::CameraAppOptions options;
    options.command = youyeetoo::CameraCommand::Service;
    if (!ParseArgs(argc, argv, &options)) {
        return 1;
    }

    youyeetoo::CameraApp app;
    return app.Run(options);
}
