/// @file cpu_fixture.hpp
/// @brief Shared CpuTest fixture: one flat 16 MiB machine for byte-program
///        unit tests (batch 3 onward).
#pragma once

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/cpu.hpp>
#include <micro_forge/arch/x86_64/decoded.hpp>
#include <micro_forge/kernel/bus.hpp>
#include <micro_forge/kernel/debug_exit.hpp>
#include <micro_forge/kernel/port_space.hpp>
#include <micro_forge/kernel/ram_region.hpp>
#include <micro_forge/kernel/scheduler.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

using micro_forge::arch::x86_64::Cpu;
using micro_forge::arch::x86_64::ExitStatus;
using micro_forge::arch::x86_64::FaultId;
using micro_forge::arch::x86_64::Mnemonic;
using micro_forge::arch::x86_64::Reg;
using micro_forge::kernel::Bus;
using micro_forge::kernel::DebugExitRegion;
using micro_forge::kernel::RamRegion;
using micro_forge::kernel::Scheduler;

constexpr uint64_t kCodeBase = 0x100000;
constexpr uint64_t kStackTop = 0x90000;

/// One flat 16 MiB machine: RAM at 0 (code at 0x100000, stack at
/// 0x90000), plus the batch-3 CPU. Tests load small byte programs and
/// inspect registers, flags, memory, and the exit status.
class CpuTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(bus_.map_ram(0, ram_.size(), ram_).has_value());
        cpu_.reset(kCodeBase, kStackTop);
    }

    /// Copies a byte program to the code base and re-arms the CPU. A
    /// trailing hlt terminates every program (walking into zero-filled
    /// RAM decodes as add [rax],al and scrambles the state under test).
    /// Register setup happens after this call, before run().
    void load(std::initializer_list<uint8_t> code) {
        std::ranges::copy(code, ram_.data() + kCodeBase);
        ram_.data()[kCodeBase + code.size()] = 0xF4;
        cpu_.reset(kCodeBase, kStackTop);
    }

    void poke(uint64_t address, std::initializer_list<uint8_t> bytes) {
        std::ranges::copy(bytes, ram_.data() + address);
    }

    Bus bus_;
    RamRegion ram_{0x1000000};
    Scheduler scheduler_;
    micro_forge::kernel::PortSpace ports_;
    Cpu cpu_{bus_, scheduler_, &ports_};
};
