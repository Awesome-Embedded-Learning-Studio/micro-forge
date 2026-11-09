/**
 * @file cinux_play.cpp
 * @brief Interactive Cinux shell: boot the kernel, wire COM1 to the terminal.
 * @version 0.1.0
 *
 * Usage:
 *   mf-cinux-play [kernel.bin] [ext2.img]
 *
 * The tool boots the image to the shell prompt, then pumps the emulator in
 * short bursts: bytes typed on the terminal go into the 16550 receiver, and
 * everything the guest writes to the console comes back out. Ctrl-] quits.
 */

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <print>
#include <string>
#include <vector>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

// The tool is a straight terminal pump: casting and const style follow the
// POSIX/termios surface rather than the library's tidy profile.
// NOLINTBEGIN

#include "micro_forge/loader/cinux_loader.hpp"
#include "micro_forge/machine.hpp"

namespace {

constexpr const char* kDefaultKernel = "/home/charliechen/Cinux/build/kernel/kernel.bin";
constexpr const char* kDefaultDisk = "/home/charliechen/Cinux/build/boot/ext2.img";
/// Instruction budget for the boot phase (the shell prompt arrives around
/// 3.9e8 retired instructions in the release build).
constexpr unsigned long long kBootBudget = 400000000ULL;
/// Instructions per interactive turn: keeps the terminal responsive.
constexpr unsigned long long kTurnBudget = 3000000ULL;

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {(std::istreambuf_iterator<char>(input)), {}};
}

/// SIGINT-free terminal reader: saves and restores termios.
class RawTerminal {
public:
    RawTerminal() {
        if (isatty(STDIN_FILENO) == 0) {
            return;
        }
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) {
            return;
        }
        termios raw = saved_;
        raw.c_lflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            active_ = true;
        }
    }
    ~RawTerminal() {
        if (active_) {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        }
    }
    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;

private:
    termios saved_{};
    bool active_ = false;
};

/// Waits up to @p timeout_ms for terminal input; returns the available
/// bytes (empty on timeout).
std::string poll_stdin(int timeout_ms) {
    pollfd entry{};
    entry.fd = STDIN_FILENO;
    entry.events = POLLIN;
    std::string out;
    const int ready = poll(&entry, 1, timeout_ms);
    if (ready <= 0) {
        return out;
    }
    char buffer[256];
    const ssize_t got = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (got > 0) {
        out.assign(buffer, static_cast<size_t>(got));
    }
    return out;
}

/// Prints console bytes that appeared since @p printed, updating the mark.
void flush_console(const std::vector<char>& console, size_t& printed, bool raw_mode) {
    if (console.size() <= printed) {
        return;
    }
    const size_t count = console.size() - printed;
    if (raw_mode) {
        (void)write(STDOUT_FILENO, console.data() + printed, count);
    } else {
        std::fwrite(console.data() + printed, 1, count, stdout);
        std::fflush(stdout);
    }
    printed = console.size();
}

} // namespace

int main(int argc, char** argv) {
    const char* kernel_path = argc > 1 ? argv[1] : kDefaultKernel;
    const char* disk_path = argc > 2 ? argv[2] : kDefaultDisk;

    const auto image = read_file(kernel_path);
    if (image.empty()) {
        std::println(stderr, "cannot read kernel image: {}", kernel_path);
        return 1;
    }
    const auto disk = read_file(disk_path);
    if (disk.empty()) {
        std::println(stderr, "cannot read disk image: {}", disk_path);
        return 1;
    }

    micro_forge::Machine machine(0x10000000);
    machine.attach_disk(disk);
    if (!micro_forge::loader::boot_cinux(machine.ram(), machine.cpu(), image).has_value()) {
        std::println(stderr, "boot_cinux refused the image");
        return 1;
    }

    const bool raw_mode = isatty(STDIN_FILENO) != 0;
    std::println("[play] booting {} ({} bytes) + {} ({} bytes)", kernel_path, image.size(),
                 disk_path, disk.size());
    std::println("[play] waiting for the shell prompt (about a minute)...");

    auto status = machine.run(kBootBudget);
    size_t printed = 0;
    flush_console(machine.cpu().console_sink(), printed, raw_mode);
    if (status.kind != micro_forge::arch::x86_64::ExitStatus::Kind::kRunning) {
        std::println(stderr, "[play] guest stopped during boot (kind={} rip=0x{:X})",
                     unsigned(status.kind), status.rip);
        return 1;
    }
    std::println("\n[play] interactive: type commands, Ctrl-] quits.");
    std::fflush(stdout);

    RawTerminal terminal;
    for (;;) {
        const std::string typed = poll_stdin(5);
        bool quit = false;
        for (char c : typed) {
            if (c == '\x1d') { // Ctrl-]
                quit = true;
                break;
            }
            // The terminal sends LF; the UART console expects CR.
            machine.inject_serial(static_cast<uint8_t>(c == '\n' ? '\r' : c));
        }
        status = machine.run(kTurnBudget);
        flush_console(machine.cpu().console_sink(), printed, raw_mode);
        if (status.kind != micro_forge::arch::x86_64::ExitStatus::Kind::kRunning) {
            std::println("\n[play] guest stopped (kind={} rip=0x{:X})", unsigned(status.kind),
                         status.rip);
            return 0;
        }
        if (quit) {
            std::println("\n[play] bye");
            return 0;
        }
    }
}
// NOLINTEND
