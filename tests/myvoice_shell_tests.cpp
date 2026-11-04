#include "myvoice/CommandShell.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool passed, const std::string& message) {
    if (!passed) throw std::runtime_error(message);
}
bool contains(const std::vector<myvoice::Suggestion>& results, const std::string& replacement) {
    return std::any_of(results.begin(), results.end(), [&](const myvoice::Suggestion& s) {
        return s.replacement == replacement;
    });
}
}
int main() {
    try {
        myvoice::CommandShell shell;
        using myvoice::ConsoleMenu;
        using myvoice::CommandAction;
        check(shell.menu() == ConsoleMenu::Main, "must start at main menu");
        check(contains(shell.suggestions("/S"), "/Seed-VC"), "Seed-VC main menu completion");
        check(contains(shell.suggestions("/r"), "/RVC"), "RVC case-insensitive completion");
        check(!contains(shell.suggestions("/con"), "/convert"), "conversion must not be available in main menu");
        check(!shell.execute("/convert").success, "main must reject convert");
        check(!shell.execute("/train").success, "main must reject train");
        check(!shell.execute("/source some.wav").success, "main must reject engine options");
        check(!shell.set_option("/source", "some.wav").success, "set_option must check menu too");
        check(shell.execute("/help").message.find("/Seed-VC") != std::string::npos, "main help");
        check(shell.execute("/show").message.find("Main") != std::string::npos, "main show");
        check(!shell.execute("/back").success, "no /back in main");
        check(!shell.execute("/Seed-VC argument").success, "navigation takes no arguments");

        check(shell.execute("/Seed-VC").success, "enter seed menu");
        check(shell.menu() == ConsoleMenu::SeedVC, "must be in Seed-VC menu");
        check(contains(shell.suggestions("/con"), "/convert"), "Seed-VC conversion completion");
        check(contains(shell.suggestions("/engine s"), "/engine speech"), "Seed-VC engine suggestion");
        check(!contains(shell.suggestions("/tr"), "/train"), "Seed-VC must hide RVC training");
        check(!shell.execute("/train").success, "Seed-VC training is not supported");
        check(!shell.execute("/model model.pth").success, "Seed-VC must reject RVC model");
        check(!shell.execute("/rvc-root path").success, "Seed-VC must reject RVC root");
        check(!shell.execute("/RVC").success, "must return to main before switching");
        check(!shell.execute("/engine rvc").success, "Seed-VC cannot load RVC mode");
        check(shell.execute("/engine singing").success, "set Seed-VC singing");
        check(shell.execute("/source \"my test file.wav\"").success, "quoted filename");
        check(shell.execute("/target target.wav").success, "Seed-VC target reference");
        check(shell.execute("/output seed_result.wav").success, "Seed-VC output");
        check(shell.execute("/python seed_python").success, "Seed-VC Python");
        check(shell.execute("/seed-root \"C:\\voice files\\seed-vc\"").success, "Windows path syntax");
        check(shell.execute("/overwrite on").success && shell.conversion_request().overwrite, "Seed-VC overwrite");
        check(shell.conversion_request().source.filename() == "my test file.wav", "source lost spaces");
        check(shell.conversion_request().mode == myvoice::Mode::Singing, "singing mode not set");
        check(shell.execute("/show").message.find("Target reference") != std::string::npos, "Seed-VC summary");
        check(shell.execute("/convert").action == CommandAction::Convert, "Seed-VC conversion action");
        check(!shell.execute("/steps abc").success, "reject invalid integer");
        check(!shell.execute("/timeout 0").success, "reject bad timeout");
        check(!shell.execute("/source \"unclosed").success, "reject unclosed quote");
        check(!shell.execute("source xxx").success, "reject commands without slash");
        check(!shell.execute("/source one two").success, "reject ambiguous path");

        check(shell.execute("/back").success && shell.menu() == ConsoleMenu::Main, "return to main");
        check(shell.execute("/rvc").success && shell.menu() == ConsoleMenu::Rvc, "enter RVC case-insensitively");
        check(contains(shell.suggestions("/tr"), "/train"), "RVC training completion");
        check(contains(shell.suggestions("/device c"), "/device cuda"), "RVC device completion");
        check(!contains(shell.suggestions("/engine"), "/engine singing"), "RVC menu should hide Seed-VC engine");
        check(!shell.execute("/engine singing").success, "RVC rejects Seed-VC engine");
        check(!shell.execute("/target target.wav").success, "RVC must reject target reference");
        check(!shell.execute("/steps 30").success, "RVC must reject diffusion steps");
        check(shell.conversion_request().mode == myvoice::Mode::Rvc, "RVC mode must be fixed");
        check(shell.execute("/source rvc_source.wav").success, "RVC source");
        check(shell.execute("/output rvc_result.wav").success, "RVC output");
        check(shell.execute("/python rvc_python").success, "RVC Python");
        check(shell.execute("/model male.pth").success, "RVC model");
        check(shell.execute("/rvc-root \"test root\"").success, "RVC root path");
        check(shell.execute("/dataset \"some voices\"").success, "dataset path");
        check(shell.execute("/device cuda").success, "CUDA mode");
        check(shell.execute("/epochs 50").success && shell.training.epochs == 50, "epochs");
        check(shell.execute("/index off").success && !shell.training.build_index, "index mode");
        check(!shell.execute("/epochs 0").success, "bad epochs validation");
        check(!shell.execute("/train-log").success, "missing RVC log expected");
        check(shell.execute("/train").action == CommandAction::Train, "RVC training action");
        check(shell.execute("/convert").action == CommandAction::Convert, "RVC conversion action");
        check(shell.execute("/show").message.find("Train name:") != std::string::npos, "RVC summary");
        check(shell.conversion_configuration().python == std::filesystem::path("rvc_python"), "RVC Python not applied");

        check(shell.execute("/back").success, "back from RVC");
        check(shell.execute("/Seed-VC").success, "re-enter Seed-VC");
        check(shell.conversion_request().mode == myvoice::Mode::Singing, "Seed-VC mode persisted");
        check(shell.conversion_request().source.filename() == "my test file.wav", "Seed-VC source persisted");
        check(shell.conversion_request().output.filename() == "seed_result.wav", "Seed-VC output persisted");
        check(shell.conversion_configuration().python == std::filesystem::path("seed_python"), "Seed-VC Python isolated");
        check(shell.execute("/quit").action == CommandAction::Quit, "quit intent");
        check(!shell.execute("/unknown xxx").success, "reject unknown command");
        std::cout << "Menu routing, isolation, completion tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << "Shell/menu tests: " << e.what() << '\n';
        return 1;
    }
}
