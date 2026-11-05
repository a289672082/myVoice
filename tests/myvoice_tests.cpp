#include "myvoice/VoiceConverter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
void file(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot write test file");
    stream << content;
}

template <typename Fn>
bool throws(const Fn& fn) {
    try { fn(); } catch (const std::exception&) { return true; }
    return false;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Workspace {
    fs::path dir;
    Workspace() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("myvoice-test-" + std::to_string(tick));
        fs::create_directories(dir / "seed root with spaces");
    }
    ~Workspace() { std::error_code ec; fs::remove_all(dir, ec); }
};
}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) { std::cerr << "Expected absolute path to Python executable\n"; return 2; }
    try {
        Workspace ws;
        const fs::path root = ws.dir / "seed root with spaces";
        const std::string fake_script = R"PY(
import argparse
import pathlib
import sys
import time
import wave

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--target', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--cfm-checkpoint-path', required=False)
parser.add_argument('--ar-checkpoint-path', required=False)
args, _ = parser.parse_known_args()
if args.cfm_checkpoint_path:
    assert pathlib.Path(args.cfm_checkpoint_path).is_file()
if args.ar_checkpoint_path:
    assert pathlib.Path(args.ar_checkpoint_path).is_file()
if 'fail' in pathlib.Path(args.source).name:
    print('BACKEND_TEST_ERROR_MARKER', file=sys.stderr)
    sys.exit(13)
if 'slow' in pathlib.Path(args.source).name:
    time.sleep(3)
out = pathlib.Path(args.output)
out.mkdir(parents=True, exist_ok=True)
with wave.open(str(out / 'converted.wav'), 'wb') as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(16000)
    wav.writeframes(b'\x00\x00' * 100)
)PY";
        file(root / "inference.py", fake_script);
        file(root / "inference_v2.py", fake_script);
        file(ws.dir / "female voice.wav", "source");
        file(ws.dir / "male voice.wav", "target");
        file(ws.dir / "fail.wav", "bad");
        file(ws.dir / "slow.wav", "slow");
        myvoice::Configuration cfg;
        cfg.python = fs::absolute(argv[1]);
        cfg.seed_vc_root = root;
        cfg.timeout = std::chrono::seconds(10);
        myvoice::VoiceConverter converter(cfg);
        myvoice::Request req;
        req.source = ws.dir / "female voice.wav";
        req.target = ws.dir / "male voice.wav";
        req.output = ws.dir / "result.wav";
        require(converter.convert(req).output == fs::absolute(req.output), "speech/singing output path");
        require(fs::file_size(req.output) > 44, "output WAV too small");
        require(throws([&] { (void)converter.convert(req); }), "existing file protection");
        req.overwrite = true;
        file(ws.dir / "fine-tuned CFM.pth", "mock seed-vc CFM checkpoint");
        file(ws.dir / "fine-tuned AR.pth", "mock seed-vc AR checkpoint");
        req.mode = myvoice::Mode::Speech;
        req.output = ws.dir / "fine tuned output.wav";
        req.seed_cfm_model = ws.dir / "fine-tuned CFM.pth";
        req.seed_ar_model = ws.dir / "fine-tuned AR.pth";
        require(converter.convert(req).output == fs::absolute(req.output),
                "Seed-VC V2 CFM/AR fine-tuned inference");
        req.seed_cfm_model.clear();
        req.seed_ar_model.clear();
        req.output = ws.dir / "result.wav";
        req.mode = myvoice::Mode::Singing;
        require(converter.convert(req).output == fs::absolute(req.output), "speech/singing output path");
        req.output = req.source;
        require(throws([&] { (void)converter.convert(req); }), "input overwrite protection");
        req.output = ws.dir / "failure.wav";
        req.source = ws.dir / "fail.wav";
        require(throws([&] { (void)converter.convert(req); }), "expected failure");
        require(!fs::exists(req.output), "unexpected output after failure");
        cfg.capture_child_output = true;
        myvoice::VoiceConverter quiet_converter(cfg);
        bool saw_backend_log = false;
        try { (void)quiet_converter.convert(req); }
        catch (const std::exception& e) {
            saw_backend_log = std::string(e.what()).find("BACKEND_TEST_ERROR_MARKER") != std::string::npos;
        }
        require(saw_backend_log, "quiet mode must capture Python stderr in failure");
        req.output = ws.dir / "timeout.wav";
        req.source = ws.dir / "slow.wav";
        cfg.timeout = std::chrono::milliseconds(100);
        require(throws([&] { (void)myvoice::VoiceConverter(cfg).convert(req); }), "expected timeout");
        require(!fs::exists(req.output), "unexpected output after failure");
        req.source = ws.dir / "female voice.wav";
        req.diffusion_steps = 0;
        require(throws([&] { (void)converter.convert(req); }), "expected failure");

        // 训练完成的 RVC .pth 可直接离线变声，无需重新输入目标参考音频。
        const auto rvc = ws.dir / "RVC root with spaces";
        fs::create_directories(rvc / "infer");
        file(rvc / "infer/cli.py", R"PY(
import argparse
import pathlib
import wave
parser=argparse.ArgumentParser()
parser.add_argument('--model', required=True)
parser.add_argument('--input', required=True)
parser.add_argument('--output', required=True)
args,_=parser.parse_known_args()
assert pathlib.Path(args.model).is_file()
with wave.open(args.output, 'wb') as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(16000)
    wav.writeframes(b'\x00\x00'*100)
)PY");
        file(ws.dir / "trained male.pth", "pretend model weights");
        cfg.rvc_root = rvc;
        cfg.timeout = std::chrono::seconds(10);
        myvoice::Request rr;
        rr.mode = myvoice::Mode::Rvc;
        rr.source = ws.dir / "female voice.wav";
        rr.trained_model = ws.dir / "trained male.pth";
        rr.output = ws.dir / "rvc output.wav";
        require(myvoice::VoiceConverter(cfg).convert(rr).output == fs::absolute(rr.output),
                "RVC inference path");
        rr.trained_model.clear();
        require(throws([&]{ (void)myvoice::VoiceConverter(cfg).convert(rr); }),
                "RVC model required");
        std::cout << "All myVoice tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test error: " << e.what() << '\n';
        return 1;
    }
}
