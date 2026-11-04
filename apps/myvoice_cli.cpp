#include "myvoice/VoiceConverter.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

void usage() {
    std::cout
        << "myvoice_cli --seed-root <Seed-VC path> --source <input.wav> --target <reference.wav> "
           "--output <result.wav> [options]\n"
        << "  --engine speech|singing  Default: speech (Seed-VC V2)\n"
        << "  --python <executable>     Python interpreter in the Seed-VC virtual environment\n"
        << "  --steps <1..200>         Diffusion steps, default: 30\n"
        << "  --timeout <seconds>      Process deadline, default: 900\n"
        << "  --overwrite              Replace existing output (never replaces inputs)\n"
        << "  --help                   Show this help\n";
}

int run(const std::vector<fs::path>& args) {
    if (args.empty()) { usage(); return 2; }
    myvoice::Configuration configuration;
    myvoice::Request request;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto key = args[i].string();
        if (key == "--help" || key == "-h") { usage(); return 0; }
        if (key == "--overwrite") { request.overwrite = true; continue; }
        if (i + 1 >= args.size()) throw std::invalid_argument("Missing value for " + key);
        const auto value = args[++i];
        if (key == "--seed-root") configuration.seed_vc_root = value;
        else if (key == "--source") request.source = value;
        else if (key == "--target") request.target = value;
        else if (key == "--output") request.output = value;
        else if (key == "--python") configuration.python = value;
        else if (key == "--steps") request.diffusion_steps = std::stoi(value.string());
        else if (key == "--timeout") {
            const int seconds = std::stoi(value.string());
            if (seconds <= 0 || seconds > 86400) throw std::invalid_argument("timeout must be 1..86400 seconds");
            configuration.timeout = std::chrono::seconds(seconds);
        } else if (key == "--engine") {
            if (value == fs::path("speech")) request.mode = myvoice::Mode::Speech;
            else if (value == fs::path("singing")) request.mode = myvoice::Mode::Singing;
            else throw std::invalid_argument("engine must be speech or singing");
        } else throw std::invalid_argument("Unknown option: " + key);
    }
    const auto result = myvoice::VoiceConverter(std::move(configuration)).convert(request);
    std::cout << "Conversion successful: " << result.output.string() << " ("
              << result.elapsed.count() << " ms)\n";
    return 0;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    try {
        std::vector<fs::path> args;
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        return run(args);
    } catch (const std::exception& ex) {
        std::cerr << "myVoice error: " << ex.what() << '\n';
        return 1;
    }
}
