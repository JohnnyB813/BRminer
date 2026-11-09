#include "warthog_console.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <limits>

namespace {

std::string withoutAnsi(const std::string& line) {
    std::string result;
    for (std::size_t i{}; i < line.size(); ++i) {
        if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {
            i += 2;
            while (i < line.size() && !(line[i] >= '@' && line[i] <= '~')) ++i;
        } else result += line[i];
    }
    return result;
}

brminer::warthog::MiningDashboard exampleDashboard() {
    using namespace brminer::warthog;
    MiningDashboard model;
    model.backend = "CUDA";
    model.threads = 10;
    model.connection = MiningConnection::mining;
    model.details = {"192.168.1.66:3456", "AMD Ryzen 7 5800X 8-Core Processor",
        "NVIDIA GeForce RTX 4060", "sync"};
    model.stats = {4290, 391430000, 411400000, 1030000000, 0, 17510000, 1651000000000, 12, 0, 1};
    brminer::GpuTelemetry gpu;
    gpu.name = "NVIDIA GeForce RTX 4060";
    gpu.driver = "610.88";
    gpu.temperature_c = 75;
    gpu.utilization_pct = 97;
    gpu.power_w = 109.2;
    gpu.core_mhz = 2775;
    gpu.memory_mhz = 8251;
    gpu.clock_reasons = 0;
    model.stats.gpus.push_back(gpu);
    model.events = {{"11:58:07", "Connected to 192.168.1.66:3456.", ConsoleEventLevel::success},
        {"11:58:07", "Payout address authorized; waiting for work.", ConsoleEventLevel::info},
        {"11:58:08", "Adaptive CUDA profile loaded for this hardware.", ConsoleEventLevel::info},
        {"11:58:12", "New job received: 06d3c695eae674fc1d316c80e041fc5154d6647c98126e37ed3e3cfb7a5c7", ConsoleEventLevel::info},
        {"11:59:34", "Share accepted by the Stratum server.", ConsoleEventLevel::success},
        {"11:59:54", "BLOCK ACCEPTED by the Stratum server.", ConsoleEventLevel::success},
        {"12:03:27", "Pool reconnecting; retrying in 10 seconds.", ConsoleEventLevel::warning},
        {"12:03:37", "Connected to 192.168.1.66:3456.", ConsoleEventLevel::success},
        {"12:04:55", "Share accepted by the Stratum server.", ConsoleEventLevel::success}};
    return model;
}

} // namespace

int main(int argc, char** argv) {
    using namespace brminer::warthog;
    const MiningStats stats{3600, 477600000, 477700000, 1050000000, 100000,
        23680000, 1581733920120, 1, 0, 1};
    auto monitored{stats};
    brminer::GpuTelemetry gpu;
    gpu.temperature_c = 79;
    gpu.utilization_pct = 98;
    gpu.core_mhz = 2760;
    gpu.memory_mhz = 8251;
    gpu.clock_reasons = 0;
    monitored.gpus.push_back(gpu);
    if (argc > 1 && std::string{argv[1]} == "--snapshot") {
        for (const auto& line : formatMiningDashboard(exampleDashboard(), 132, 30, true)) std::cout << line << '\n';
        return 0;
    }
    if (argc > 1 && (std::string{argv[1]} == "--live-demo" || std::string{argv[1]} == "--live-once")) {
        MiningConsole console;
        console.start("CUDA", 14, false, false, exampleDashboard().details);
        if (!console.isLive()) return 2;
        console.connection(MiningConnection::mining);
        console.update(stats);
        if (std::string{argv[1]} == "--live-once") return 0;
        for (int i{}; i < 50; ++i) {
            console.event("SIMULATED new job " + std::to_string(i));
            auto current{stats};
            if (i >= 10) current.gpus = monitored.gpus;
            current.uptime += i * .1;
            current.janus *= 1 + .04 * std::sin(i * .3);
            console.update(current);
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        console.event("SIMULATED BLOCK FOUND; submitted.");
        console.shares(2, 0, 2);
        console.event("SIMULATED BLOCK ACCEPTED.", ConsoleEventLevel::success);
        std::this_thread::sleep_for(std::chrono::seconds{3});
        return 0;
    }
    const auto table{formatMiningTable(stats, "CUDA", 14)};
    assert(formatHashrate(1050000000) == "1.05 GH/s");
    assert(formatSensor(std::nullopt, " W") == "N/A");
    assert(formatSensor(0.0, " W") == "0 W");
    assert(brminer::clockReasonText(std::nullopt) == "N/A");
    assert(brminer::clockReasonText(0) == "none");
    assert(brminer::clockReasonText(4) == "power cap");
    assert(formatUptime(3600) == "01:00:00");
    assert(formatUptime(90061) == "25:01:01");
    assert(formatUptime(std::numeric_limits<double>::quiet_NaN()) == "00:00:00");
    assert(table.find("477.60 MH/s") < table.find("1.05 GH/s"));
    std::istringstream lines{table};
    for (std::string line; std::getline(lines, line); ) {
        if (!line.empty() && (line.front() == '+' || line.front() == '|'))
            assert(line.size() == 96);
    }
    const auto waiting{formatMiningTable({}, "CPU", 1)};
    const auto sensor_table{formatMiningTable(monitored, "CUDA", 14)};
    assert(sensor_table.find("GPU sensors") != std::string::npos);
    assert(sensor_table.find("N/A") != std::string::npos);
    assert(sensor_table.find("GPU 0 clock limits: none") != std::string::npos);
    assert(waiting.find("nan") == std::string::npos);
    assert(waiting.find("waiting") != std::string::npos);
    assert(waiting.find("0 (0.0%)") != std::string::npos);
    std::istringstream compact{formatMiningTable(stats, "CUDA", 14, 79)};
    for (std::string line; std::getline(compact, line); ) {
        if (!line.empty() && (line.front() == '+' || line.front() == '|'))
            assert(line.size() == 79);
    }

    MiningRateWindow rates;
    rates.sample(0, 0, 0, 0);
    for (int i{1}; i <= 20; ++i) {
        const auto sample{rates.sample(i, i * 100000ULL, i * 1000000000ULL, i * 20000000ULL)};
        assert(sample.gpu_sha == 1e9 && sample.cpu_sha == 1e5 && sample.verus == 2e7);
        assert(std::abs(sample.janus - sample.janus_average) < .01);
    }
    const auto slowing{rates.sample(30, 2000000, 20000000000, 400000000)};
    assert(slowing.gpu_sha == 0 && slowing.janus == 0 && slowing.janus_average > 0);
    const auto reset{rates.sample(0, 0, 0, 0)};
    assert(reset.janus == 0 && reset.janus_average == 0);

    auto dashboard{exampleDashboard()};
    for (const auto [columns, rows] : {std::pair{132, 30}, {120, 35}, {80, 25}, {45, 14}, {20, 6}, {1, 1}}) {
        const auto colored{formatMiningDashboard(dashboard, columns, rows, true)};
        const auto plain{formatMiningDashboard(dashboard, columns, rows, false)};
        assert(static_cast<int>(colored.size()) == rows);
        assert(colored.size() == plain.size());
        for (std::size_t i{}; i < colored.size(); ++i) {
            assert(withoutAnsi(colored[i]) == plain[i]);
            assert(static_cast<int>(plain[i].size()) <= std::max(1, columns - 1));
            assert(plain[i].find('\x1b') == std::string::npos);
        }
        assert(miningDashboardDelta(colored, colored).empty());
    }
    const auto before{formatMiningDashboard(dashboard, 120, 35, true)};
    dashboard.stats.accepted++;
    const auto after{formatMiningDashboard(dashboard, 120, 35, true)};
    const auto delta{miningDashboardDelta(before, after)};
    assert(!delta.empty() && delta.find("\x1b[2J") == std::string::npos && delta.find("\x1b[2K") == std::string::npos);
    assert(delta.find("Share difficulty") == std::string::npos);
    dashboard.events.push_back({"12:05:00", "ERROR: " + std::string(10000, 'x') + "\x1b[2J", ConsoleEventLevel::error});
    const auto long_event{formatMiningDashboard(dashboard, 80, 25, true)};
    std::string rendered;
    for (const auto& line : long_event) rendered += line + '\n';
    assert(rendered.find("\x1b[2J") == std::string::npos);
    assert(rendered.find("\x1b[1;31m") != std::string::npos && rendered.find("...") != std::string::npos);
    dashboard.stats.gpus.assign(16, gpu);
    const auto multiple{formatMiningDashboard(dashboard, 80, 25, false)};
    rendered.clear();
    for (const auto& line : multiple) rendered += line + '\n';
    assert(rendered.find("more GPU(s)") != std::string::npos && rendered.find("EVENTS") != std::string::npos);

    std::ostringstream captured;
    auto* original{std::cout.rdbuf(captured.rdbuf())};
    {
        MiningConsole console;
        console.start("CPU", 1, true);
        console.update(stats);
        console.event("plain log", ConsoleEventLevel::error);
    }
    std::cout.rdbuf(original);
    assert(captured.str().find('\x1b') == std::string::npos && captured.str().find("plain log") != std::string::npos);
    if (argc > 1) std::cout << table;
}
