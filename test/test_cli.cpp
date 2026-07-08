// CLI behavior tests (B4). Drives the `micro-forge` binary via popen and
// asserts stdout / exit code / snapshot contents.
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>

#ifndef MICRO_FORGE_BIN
#    error "MICRO_FORGE_BIN must be defined"
#endif
#ifndef CLI_HELLO_ELF
#    error "CLI_HELLO_ELF must be defined"
#endif

namespace {

struct CmdResult {
    std::string out;
    int code;
};

// Run `micro-forge <args>`, capturing merged stdout+stderr.
CmdResult run_cli(const std::string& args) {
    std::string cmd = std::string(MICRO_FORGE_BIN) + " " + args + " 2>&1";
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) {
        return {"", -1};
    }
    std::string out;
    char buf[256];
    while (std::fgets(buf, sizeof(buf), p)) {
        out += buf;
    }
    int raw = ::pclose(p);
    int code = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
    return {out, code};
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace

// run hello.elf → stdout contains "Hello", exit 0.
TEST(Cli, RunHelloOutputsString) {
    auto r =
        run_cli(std::string("run ") + CLI_HELLO_ELF + " --max-steps 100000");
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_NE(r.out.find("Hello"), std::string::npos) << r.out;
}

// No subcommand → usage on stderr, exit 2.
TEST(Cli, NoArgsReturnsUsage) {
    auto r = run_cli("");
    EXPECT_EQ(r.code, 2);
    EXPECT_NE(r.out.find("usage"), std::string::npos);
}

// --snapshot-json writes a parseable JSON containing the cpu region; also
// guards the hex-sticky regression (r10/r11/r12, not ra/rb/rc).
TEST(Cli, SnapshotJsonHasCpuRegion) {
    auto r = run_cli(std::string("run ") + CLI_HELLO_ELF +
                     " --max-steps 50000 --snapshot-json /tmp/cli_snap.json");
    EXPECT_EQ(r.code, 0) << r.out;
    std::string js = read_file("/tmp/cli_snap.json");
    ASSERT_FALSE(js.empty());
    EXPECT_NE(js.find("\"cpu\""), std::string::npos);
    EXPECT_NE(js.find("\"regs\""), std::string::npos);
    EXPECT_NE(js.find("\"r10\""), std::string::npos);
    EXPECT_EQ(js.find("\"ra\""), std::string::npos);
}

// A firmware whose reset vector points at unmapped 0x10000000 → Faulted,
// exit 1.
TEST(Cli, UnmappedPcFaults) {
    {
        std::ofstream f("/tmp/cli_bad.bin", std::ios::binary);
        const uint32_t vt[2] = {0x20005000u, 0x10000001u}; // SP, PC(unmapped)
        f.write(reinterpret_cast<const char*>(vt), sizeof(vt));
    }
    auto r = run_cli("run /tmp/cli_bad.bin --base 0x08000000 --max-steps 50");
    EXPECT_NE(r.code, 0);
    EXPECT_NE(r.out.find("Faulted"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("InstructionFetchFault"), std::string::npos) << r.out;
}

// probe hello.elf → the simulator implements every opcode this firmware uses
// (it runs and prints "Hello"), so probe mode reports full coverage, exit 0 (A4).
TEST(Cli, ProbeHelloReportsNoMissingOpcodes) {
    auto r = run_cli(std::string("probe ") + CLI_HELLO_ELF +
                     " --max-steps 100000");
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_NE(r.out.find("[probe]"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("no missing opcodes"), std::string::npos) << r.out;
}

// probe a firmware whose reset vector lands on 0xFFFF (an unimplemented
// opcode) → probe mode records it and skips, reporting the missing encoding.
TEST(Cli, ProbeReportsMissingOpcode) {
    {
        std::ofstream f("/tmp/cli_probe.bin", std::ios::binary);
        const uint32_t sp = 0x20005000u;
        const uint32_t pc = 0x08000009u; // reset → 0x08000008 (thumb)
        const uint16_t bad_insn = 0xFFFFu;
        f.write(reinterpret_cast<const char*>(&sp), 4);
        f.write(reinterpret_cast<const char*>(&pc), 4);
        f.write(reinterpret_cast<const char*>(&bad_insn), 2);
    }
    auto r =
        run_cli("probe /tmp/cli_probe.bin --base 0x08000000 --max-steps 50");
    EXPECT_EQ(r.code, 0) << r.out;
    EXPECT_NE(r.out.find("missing opcode"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("0xFFFF"), std::string::npos) << r.out;
}
