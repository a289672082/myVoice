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
        check(contains(shell.suggestions("/con"), "/convert"), "command suggestion");
        check(contains(shell.suggestions("/engine s"), "/engine speech"), "value suggestion");
        check(contains(shell.suggestions("/overwrite o"), "/overwrite on"), "boolean suggestion");
        check(shell.execute("/engine singing").success, "singing command");
        check(shell.request.mode == myvoice::Mode::Singing, "singing mode not set");
        check(shell.execute("/source \"my test file.wav\"").success, "quoted filename");
        check(shell.request.source.filename() == "my test file.wav", "source lost space");
        check(shell.execute("/seed-root \"C:\\voice files\\seed-vc\"").success, "Windows path syntax");
        check(shell.execute("/overwrite on").success && shell.request.overwrite, "overwrite command");
        check(!shell.execute("/steps abc").success, "reject invalid integer");
        check(!shell.execute("/timeout 0").success, "reject bad timeout");
        check(!shell.execute("/source \"unclosed").success, "reject unclosed quote");
        check(!shell.execute("source xxx").success, "reject commands without slash");
        check(!shell.execute("/engine invalid").success, "reject unknown model");
        check(shell.execute("/show").message.find("singing") != std::string::npos, "show current settings");
        check(shell.execute("/convert").action == myvoice::CommandAction::Convert, "convert intent");
        check(shell.execute("/quit").action == myvoice::CommandAction::Quit, "quit intent");
        check(shell.execute("/help").message.find("/seed-root") != std::string::npos, "help text");
        check(!shell.execute("/unknown xxx").success, "reject unknown command");
        check(!shell.execute("/source one two").success, "reject ambiguous path");
        std::cout << "Shell/complete tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << "Shell/complete tests: " << e.what() << '\n';
        return 1;
    }
}
