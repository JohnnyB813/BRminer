#include "warthog_console.hpp"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace brminer::warthog {
namespace {

std::string cleanLine(std::string line) {
    for (auto& ch : line) if (static_cast<unsigned char>(ch) < 32 || ch == 127) ch = ' ';
    return line;
}

// ASCII cells keep meters aligned in Windows consoles, SSH, and terminals with
// different fonts/locales. Preserve the original UTF-8 text in plain logs.
std::string displayText(std::string_view text) {
    std::string result;
    for (std::size_t i{}; i < text.size(); ++i) {
        const auto ch{static_cast<unsigned char>(text[i])};
        if (ch < 32 || ch == 127) result += ' ';
        else if (ch < 128) result += static_cast<char>(ch);
        else {
            result += '?';
            while (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xc0) == 0x80) ++i;
        }
    }
    return result;
}

std::string timestamp() {
    const auto now{std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())};
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S");
    return out.str();
}

double positive(double value) { return std::isfinite(value) ? std::max(0.0, value) : 0.0; }

double janusRate(double sha, double verus) {
    return sha > 0 ? (10.0 / 3.0) * sha *
        (std::pow(0.005 + verus / sha, 0.3) - std::pow(0.005, 0.3)) : 0.0;
}

enum class Ink { normal, muted, label, value, good, warning, error, heading, footer, title };

const char* ansi(Ink ink) {
    switch (ink) {
    case Ink::muted: return "\x1b[0;90m";
    case Ink::label: return "\x1b[0;36m";
    case Ink::value: return "\x1b[1;32m";
    case Ink::good: return "\x1b[0;32m";
    case Ink::warning: return "\x1b[1;33m";
    case Ink::error: return "\x1b[1;31m";
    case Ink::heading: return "\x1b[0;30;42m";
    case Ink::footer: return "\x1b[0;30;46m";
    case Ink::title: return "\x1b[1;36m";
    default: return "\x1b[0m";
    }
}

struct Cell { char text{' '}; Ink ink{Ink::normal}; };

class Canvas {
public:
    const int width, height;
    Canvas(int columns, int rows) : width{std::clamp(columns - 1, 1, 511)},
        height{std::clamp(rows, 1, 200)}, cells(height, std::vector<Cell>(width)) {}

    void text(int x, int y, std::string_view value, Ink ink = Ink::normal, int limit = -1) {
        if (y < 0 || y >= height || x >= width) return;
        const auto clean{displayText(value)};
        const int length{std::min(static_cast<int>(clean.size()), limit < 0 ? width - x : limit)};
        for (int i{}; i < length && x + i < width; ++i)
            if (x + i >= 0) cells[y][x + i] = {clean[i], ink};
    }

    void field(int x, int y, int length, std::string value, Ink ink = Ink::normal, bool right = false) {
        if (length <= 0) return;
        value = displayText(value);
        if (static_cast<int>(value.size()) > length)
            value = length >= 4 ? value.substr(0, length - 3) + "..." : value.substr(0, length);
        text(x + (right ? length - static_cast<int>(value.size()) : 0), y, value, ink, length);
    }

    void band(int y, std::string_view label, Ink ink = Ink::heading) {
        text(0, y, std::string(width, ' '), ink);
        text(1, y, label, ink);
    }

    std::vector<std::string> lines(bool color) const {
        std::vector<std::string> result;
        for (const auto& row : cells) {
            std::string line;
            Ink previous{Ink::normal};
            for (const auto& cell : row) {
                if (color && cell.ink != previous) line += ansi(cell.ink);
                previous = cell.ink;
                line += cell.text;
            }
            if (color) line += "\x1b[0m";
            result.push_back(std::move(line));
        }
        return result;
    }
private:
    std::vector<std::vector<Cell>> cells;
};

std::pair<std::string, Ink> connectionLabel(MiningConnection connection) {
    switch (connection) {
    case MiningConnection::connected: return {"CONNECTED", Ink::good};
    case MiningConnection::waiting: return {"WAITING FOR WORK", Ink::warning};
    case MiningConnection::mining: return {"MINING", Ink::value};
    case MiningConnection::reconnecting: return {"RECONNECTING", Ink::warning};
    case MiningConnection::stopped: return {"STOPPED", Ink::muted};
    default: return {"CONNECTING", Ink::warning};
    }
}

std::pair<std::string, Ink> eventLabel(ConsoleEventLevel level) {
    switch (level) {
    case ConsoleEventLevel::success: return {"OK", Ink::good};
    case ConsoleEventLevel::warning: return {"WARN", Ink::warning};
    case ConsoleEventLevel::error: return {"ERROR", Ink::error};
    default: return {"INFO", Ink::label};
    }
}

std::string shareInterval(const MiningStats& stats) {
    if (!(stats.difficulty > 0 && stats.janus > 0)) return "waiting";
    const double seconds{stats.difficulty / stats.janus};
    if (!std::isfinite(seconds)) return "waiting";
    std::ostringstream out;
    out << '~' << std::fixed << std::setprecision(1);
    if (seconds >= 3600) out << seconds / 3600 << " h";
    else if (seconds >= 60) out << seconds / 60 << " min";
    else out << seconds << " s";
    return out.str();
}

std::string difficultyText(double difficulty) {
    if (!(difficulty > 0 && std::isfinite(difficulty))) return "waiting";
    std::ostringstream out;
    out << std::setprecision(4) << difficulty;
    return out.str();
}

std::string rejectionText(const MiningStats& stats) {
    const double total{static_cast<double>(stats.accepted) + static_cast<double>(stats.rejected)};
    std::ostringstream out;
    out << stats.rejected << " (" << std::fixed << std::setprecision(1)
        << (total > 0 ? 100.0 * stats.rejected / total : 0.0) << "%)";
    return out.str();
}

void metric(Canvas& canvas, int x, int y, int width,
            const std::string& label, double value, Ink ink) {
    const std::string caption{label + ": "};
    const int offset{static_cast<int>(caption.size())};
    canvas.text(x, y, caption, Ink::label);
    canvas.field(x + offset, y, width - offset, formatHashrate(value), ink);
}

int devices(Canvas& canvas, const MiningDashboard& model, int y) {
    const bool wide{canvas.width >= 110};
    const std::array<int, 7> widths{canvas.width - (wide ? 65 : 54), 14, 7, 7, 9, 10, wide ? 10 : 0};
    std::array<int, 7> positions{};
    positions[0] = 1;
    for (std::size_t i{1}; i < positions.size(); ++i) positions[i] = positions[i - 1] + widths[i - 1] + 1;
    const auto row = [&](int line, const std::array<std::string, 7>& values, Ink name = Ink::normal,
                         Ink temperature = Ink::normal) {
        for (std::size_t i{}; i < values.size(); ++i)
            canvas.field(positions[i], line, widths[i], values[i], i == 0 ? name : (i == 2 ? temperature : Ink::normal), i > 0);
    };
    canvas.band(y, "DEVICE");
    for (std::size_t i{1}; i < widths.size(); ++i)
        canvas.field(positions[i], y, widths[i], std::array<std::string, 7>{"", "HASHRATE", "TEMP", "USE", "POWER", "CORE", "MEM"}[i], Ink::heading, true);
    ++y;
    row(y++, {"CPU " + (model.details.cpu.empty() ? std::string{"processor"} : model.details.cpu),
        formatHashrate(model.stats.verus), "--", "--", "--", "--", "--"}, Ink::label);
    const auto count{model.stats.gpus.empty() ? (model.backend == "CPU" ? std::size_t{} : std::size_t{1}) : model.stats.gpus.size()};
    for (std::size_t i{}; i < count; ++i) {
        // Leave at least three event rows and the footer, even on multi-GPU rigs.
        if (y >= canvas.height - 7) {
            canvas.text(1, y++, "+ " + std::to_string(count - i) + " more GPU(s); enlarge the terminal", Ink::muted);
            break;
        }
        const GpuTelemetry empty;
        const auto& gpu{model.stats.gpus.empty() ? empty : model.stats.gpus[i]};
        const auto name{gpu.name.empty() ? model.details.gpu : gpu.name};
        const auto heat{!gpu.temperature_c ? Ink::muted : *gpu.temperature_c >= 85 ? Ink::error :
            *gpu.temperature_c >= 75 ? Ink::warning : Ink::good};
        row(y++, {"GPU " + std::to_string(i) + ' ' + name,
            count == 1 ? formatHashrate(model.stats.gpu_sha) : "aggregate above",
            formatSensor(gpu.temperature_c, " C"), formatSensor(gpu.utilization_pct, "%"),
            formatSensor(gpu.power_w, " W", 1), formatSensor(gpu.core_mhz, " MHz"),
            formatSensor(gpu.memory_mhz, " MHz")}, Ink::label, heat);
        if (count == 1 && y < canvas.height - 6) {
            canvas.field(1, y++, canvas.width - 2, "Clock limits: " + clockReasonText(gpu.clock_reasons),
                gpu.clock_reasons && *gpu.clock_reasons != 0 ? Ink::warning : Ink::muted);
            if (y < canvas.height - 7)
                canvas.field(1, y++, canvas.width - 2, "Driver " + (gpu.driver.empty() ? std::string{"N/A"} : gpu.driver) +
                    " | Memory " + formatSensor(gpu.memory_mhz, " MHz") +
                    (model.details.pipeline.empty() ? "" : " | Pipeline " + model.details.pipeline), Ink::muted);
        }
    }
    return y;
}

} // namespace

MiningStats MiningRateWindow::sample(double uptime, std::uint64_t cpu_sha, std::uint64_t gpu_sha,
                                     std::uint64_t verus) {
    uptime = positive(uptime);
    MiningStats result;
    result.uptime = uptime;
    if (samples.empty() || uptime < samples.back().time || cpu_sha < samples.back().cpu_sha ||
        gpu_sha < samples.back().gpu_sha || verus < samples.back().verus) {
        samples = {{uptime, cpu_sha, gpu_sha, verus}};
        janus_work = 0;
        start_time = uptime;
        return result;
    }
    const auto previous{samples.back()};
    if (uptime <= previous.time) return result;
    const double elapsed{uptime - previous.time};
    janus_work += janusRate((static_cast<double>(cpu_sha - previous.cpu_sha) + static_cast<double>(gpu_sha - previous.gpu_sha)) / elapsed,
        static_cast<double>(verus - previous.verus) / elapsed) * elapsed;
    samples.push_back({uptime, cpu_sha, gpu_sha, verus});
    while (samples.size() > 2 && samples[1].time <= uptime - 10.0) samples.pop_front();
    const auto& first{samples.front()};
    const double window{uptime - first.time};
    result.cpu_sha = static_cast<double>(cpu_sha - first.cpu_sha) / window;
    result.gpu_sha = static_cast<double>(gpu_sha - first.gpu_sha) / window;
    result.verus = static_cast<double>(verus - first.verus) / window;
    result.janus = janusRate(result.cpu_sha + result.gpu_sha, result.verus);
    result.janus_average = janus_work / (uptime - start_time);
    return result;
}

std::vector<std::string> formatMiningDashboard(const MiningDashboard& model, int columns, int rows, bool color) {
    Canvas canvas{columns, rows};
    const auto& stats{model.stats};
    const auto [status, status_ink]{connectionLabel(model.connection)};
    const bool full{canvas.width >= 78 && canvas.height >= 20};
    const std::string title{"BRminer / WARTHOG  " + model.backend + "  " + std::to_string(model.threads) + " CPU threads"};
    canvas.field(1, 0, canvas.width - 2, title, Ink::title);
    const std::string uptime{"Up " + formatUptime(stats.uptime)};
    if (canvas.width >= 78) canvas.field(canvas.width - 15, 0, 14, uptime, Ink::label, true);
    canvas.field(1, 1, canvas.width - static_cast<int>(status.size()) - 5,
        "Pool " + (model.details.pool.empty() ? std::string{"not configured"} : model.details.pool), Ink::muted);
    canvas.field(canvas.width - static_cast<int>(status.size()) - 1, 1, static_cast<int>(status.size()), status, status_ink);
    int y{3};
    if (full) {
        const int panel{(canvas.width - 4) / 3};
        metric(canvas, 1, y, panel, "JANUS (10s)", stats.janus, Ink::value);
        metric(canvas, panel + 2, y, panel, "GPU SHA256t", stats.gpu_sha, Ink::label);
        metric(canvas, 2 * panel + 3, y, panel, "CPU Verus", stats.verus, Ink::good);
        y += 2;
        const std::string counters{"Accepted " + std::to_string(stats.accepted) + "   Rejected " + rejectionText(stats) +
            "   Blocks " + std::to_string(stats.blocks)};
        canvas.field(1, y, std::max(50, canvas.width - 28), counters, stats.rejected ? Ink::warning : Ink::good);
        canvas.field(std::max(52, canvas.width - 27), y++, std::min(26, canvas.width - 53),
            "AVG " + formatHashrate(stats.janus_average), Ink::value, true);
        canvas.text(1, y++, "Share difficulty " + difficultyText(stats.difficulty) +
            "   Est. share " + shareInterval(stats), Ink::muted);
        ++y;
        y = devices(canvas, model, y);
        ++y;
    } else {
        // Keep the primary rate and events usable in small SSH panes.
        y = 2;
        canvas.field(1, y++, canvas.width - 2, "JANUS (10s) " + formatHashrate(stats.janus), Ink::value);
        if (canvas.height >= 12) {
            canvas.field(1, y++, canvas.width - 2, "AVG " + formatHashrate(stats.janus_average) + " | " + uptime, Ink::label);
        }
        if (canvas.height >= 16) {
            canvas.field(1, y++, canvas.width - 2, "GPU " + formatHashrate(stats.gpu_sha) + " | Verus " + formatHashrate(stats.verus), Ink::label);
            canvas.field(1, y++, canvas.width - 2, "Diff " + difficultyText(stats.difficulty) + " | Est. " + shareInterval(stats), Ink::muted);
        }
        if (canvas.height >= 9)
            canvas.field(1, y++, canvas.width - 2, "Accepted " + std::to_string(stats.accepted) + " / Rejected " +
                std::to_string(stats.rejected) + " / Blocks " + std::to_string(stats.blocks), stats.rejected ? Ink::warning : Ink::good);
    }
    y = std::min(y, std::max(0, canvas.height - 3));
    if (canvas.height >= 6) {
        canvas.band(y++, canvas.width >= 60 ? "TIME       LEVEL  EVENTS (newest below)" : "EVENTS (newest below)");
        const auto available{static_cast<std::size_t>(std::max(0, canvas.height - y - 1))};
        const auto first{model.events.size() > available ? model.events.size() - available : 0};
        for (auto i{first}; i < model.events.size(); ++i) {
            const auto& event{model.events[i]};
            const auto [label, ink]{eventLabel(event.level)};
            if (canvas.width >= 60) {
                canvas.text(1, y, event.time, Ink::muted, 8);
                canvas.text(12, y, label, ink, 5);
                canvas.field(19, y++, canvas.width - 20, event.message, ink == Ink::label ? Ink::normal : ink);
            } else canvas.field(1, y++, canvas.width - 2, event.time + ' ' + event.message, ink);
        }
    }
    if (canvas.height >= 4) canvas.band(canvas.height - 1, canvas.width >= 78 ?
        "Ctrl+C Stop  |  Refresh 1s  |  Rates: rolling 10s" :
        "Ctrl+C Stop | Refresh 1s", Ink::footer);
    return canvas.lines(color);
}

std::string miningDashboardDelta(const std::vector<std::string>& previous, const std::vector<std::string>& next) {
    std::string output;
    for (std::size_t i{}; i < next.size(); ++i) {
        if (i < previous.size() && previous[i] == next[i]) continue;
        // Overwrite before erasing the tail; clearing first exposes a blank row.
        output += "\x1b[" + std::to_string(i + 1) + ";1H" + next[i] + "\x1b[K";
    }
    return output;
}

struct MiningConsole::Impl {
    std::mutex mutex;
    MiningDashboard model;
    std::vector<std::string> previous;
    bool live{}, quiet{}, color{};
    int width{}, height{};
#if defined(_WIN32)
    HANDLE output{GetStdHandle(STD_OUTPUT_HANDLE)};
    DWORD original_mode{};
    bool changed_mode{};
#endif

    ~Impl() {
        if (live) {
            if (color) std::cout << "\x1b[0m";
            std::cout << "\x1b[?25h\x1b[r\x1b[?1049l" << std::flush;
        }
#if defined(_WIN32)
        if (changed_mode) SetConsoleMode(output, original_mode);
#endif
    }

    void refresh() {
        if (!live) return;
#if defined(_WIN32)
        CONSOLE_SCREEN_BUFFER_INFO screen{};
        if (!GetConsoleScreenBufferInfo(output, &screen)) return;
        const int next_width{screen.srWindow.Right - screen.srWindow.Left + 1};
        const int next_height{screen.srWindow.Bottom - screen.srWindow.Top + 1};
#else
        winsize screen{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &screen) != 0) return;
        const int next_width{screen.ws_col};
        const int next_height{screen.ws_row};
#endif
        if (next_width < 1 || next_height < 1) return;
        if (next_width != width || next_height != height) previous.clear();
        width = next_width; height = next_height;
        auto frame{formatMiningDashboard(model, width, height, color)};
        const auto bytes{miningDashboardDelta(previous, frame)};
        if (!bytes.empty()) std::cout.write(bytes.data(), static_cast<std::streamsize>(bytes.size())).flush();
        previous = std::move(frame);
    }

    void addEvent(const std::string& message, ConsoleEventLevel level) {
        const auto time{timestamp()};
        const auto clean{cleanLine(message)};
        model.events.push_back({time, clean, level});
        if (model.events.size() > 1000) model.events.pop_front();
        if (live) refresh();
        else std::cout << '[' << time << "] " << clean << '\n' << std::flush;
    }
};

MiningConsole::MiningConsole() : impl{std::make_unique<Impl>()} {}
MiningConsole::~MiningConsole() = default;

void MiningConsole::start(std::string backend, std::uint32_t threads, bool diagnostics, bool quiet,
                          MiningConsoleDetails details) {
    std::lock_guard lock{impl->mutex};
    impl->model.backend = std::move(backend);
    impl->model.threads = threads;
    impl->model.details = std::move(details);
    impl->quiet = quiet;
    const char* term{std::getenv("TERM")};
    const char* mode{std::getenv("BRMINER_CONSOLE")};
    const bool plain{(term && std::string_view{term} == "dumb") || (mode && std::string_view{mode} == "plain")};
    const char* no_color{std::getenv("NO_COLOR")};
    impl->color = !(no_color && *no_color);
#if defined(_WIN32)
    if (!diagnostics && !quiet && !plain && GetConsoleMode(impl->output, &impl->original_mode)) {
        impl->live = SetConsoleMode(impl->output,
            impl->original_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        impl->changed_mode = impl->live;
    }
#else
    impl->live = !diagnostics && !quiet && !plain && isatty(STDOUT_FILENO) && term && *term;
#endif
    if (impl->live) std::cout << "\x1b[?1049h\x1b[r\x1b[?25l";
    impl->refresh();
}

void MiningConsole::update(const MiningStats& stats) {
    std::lock_guard lock{impl->mutex};
    auto& current{impl->model.stats};
    const auto accepted{current.accepted}, rejected{current.rejected}, blocks{current.blocks};
    current = stats;
    current.accepted = std::max(accepted, stats.accepted);
    current.rejected = std::max(rejected, stats.rejected);
    current.blocks = std::max(blocks, stats.blocks);
    if (impl->quiet) return;
    if (impl->live) {
        impl->refresh();
    } else std::cout << formatMiningTable(current, impl->model.backend, impl->model.threads) << std::flush;
}

void MiningConsole::shares(std::uint64_t accepted, std::uint64_t rejected, std::uint64_t blocks) {
    std::lock_guard lock{impl->mutex};
    impl->model.stats.accepted = accepted;
    impl->model.stats.rejected = rejected;
    impl->model.stats.blocks = blocks;
    impl->refresh();
}

void MiningConsole::connection(MiningConnection status) {
    std::lock_guard lock{impl->mutex};
    impl->model.connection = status;
    impl->refresh();
}

void MiningConsole::event(const std::string& message, ConsoleEventLevel level) {
    std::lock_guard lock{impl->mutex};
    if (!impl->quiet) impl->addEvent(message, level);
}

void MiningConsole::diagnostic(const std::string& line) {
    std::lock_guard lock{impl->mutex};
    if (impl->quiet) return;
    if (impl->live) impl->addEvent(line, ConsoleEventLevel::info);
    else std::cout << line << '\n' << std::flush;
}

bool MiningConsole::isLive() const { return impl->live; }

} // namespace brminer::warthog
