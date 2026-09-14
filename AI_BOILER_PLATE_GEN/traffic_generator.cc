/*
 * Standalone random traffic generator for the address translation example.
 *
 * The driver intentionally mirrors the gem5 SimObject's direct-mapped TLB so
 * that hit/miss behavior can be explored without building gem5.
 */

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

struct TrafficOptions
{
    uint64_t requests = 32;
    uint64_t range_start = 0;
    uint64_t range_end = 0xFFFF;
    uint64_t seed = 12345;
    uint64_t page_bits = 12;
    uint64_t tlb_entries = 4;
    uint64_t hit_latency = 4;
    uint64_t miss_latency = 30;
    uint64_t physical_offset = 0x1000;
    bool verbose = false;
    std::string log_file;
};

struct TranslationResult
{
    uint64_t physical_address;
    bool hit;
    uint64_t latency;
};

class DirectMappedTranslator
{
  public:
    explicit DirectMappedTranslator(const TrafficOptions &options)
        : page_bits(options.page_bits),
          page_size(uint64_t{1} << options.page_bits),
          page_mask(page_size - 1),
          physical_offset(options.physical_offset),
          hit_latency(options.hit_latency),
          miss_latency(options.miss_latency),
          index_mask(options.tlb_entries - 1),
          entries(options.tlb_entries)
    {
    }

    TranslationResult translate(uint64_t virtual_address)
    {
        const uint64_t virtual_page = virtual_address >> page_bits;
        const std::size_t index = virtual_page & index_mask;
        Entry &entry = entries[index];
        const bool hit = entry.valid && entry.virtual_page == virtual_page;

        if (hit) {
            ++hits;
        } else {
            ++misses;
            entry.virtual_page = virtual_page;
            entry.physical_page = virtual_page + physical_offset;
            entry.valid = true;
        }

        const uint64_t physical_address =
            (entry.physical_page << page_bits) | (virtual_address & page_mask);
        return {physical_address, hit, hit ? hit_latency : miss_latency};
    }

    uint64_t hitCount() const
    {
        return hits;
    }

    uint64_t missCount() const
    {
        return misses;
    }

  private:
    struct Entry
    {
        uint64_t virtual_page = 0;
        uint64_t physical_page = 0;
        bool valid = false;
    };

    const uint64_t page_bits;
    const uint64_t page_size;
    const uint64_t page_mask;
    const uint64_t physical_offset;
    const uint64_t hit_latency;
    const uint64_t miss_latency;
    const uint64_t index_mask;
    std::vector<Entry> entries;
    uint64_t hits = 0;
    uint64_t misses = 0;
};

uint64_t
parseNumber(const std::string &text, const std::string &option)
{
    std::size_t characters = 0;
    const uint64_t value = std::stoull(text, &characters, 0);
    if (characters != text.size()) {
        throw std::invalid_argument("invalid value for " + option + ": " + text);
    }
    return value;
}

void
printUsage(const char *program)
{
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --requests N       Number of addresses (default: 32)\n"
              << "  --start ADDRESS    Inclusive virtual start (default: 0x0)\n"
              << "  --end ADDRESS      Inclusive virtual end (default: 0xffff)\n"
              << "  --seed N           Random seed (default: 12345)\n"
              << "  --page-bits N      Log2 page size (default: 12)\n"
              << "  --entries N        TLB entries, power of two (default: 4)\n"
              << "  --hit-latency N    Hit cost in cycles (default: 4)\n"
              << "  --miss-latency N   Miss cost in cycles (default: 30)\n"
              << "  --verbose          Print every generated request\n"
              << "  --log-file PATH    Dump configuration, requests, and summary\n"
              << "  --help             Show this help\n";
}

TrafficOptions
parseOptions(int argc, char **argv)
{
    TrafficOptions options;
    for (int argument = 1; argument < argc; ++argument) {
        const std::string option = argv[argument];
        if (option == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }
        if (option == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (argument + 1 >= argc) {
            throw std::invalid_argument("missing value for " + option);
        }
        const std::string value = argv[++argument];
        if (option == "--requests") {
            options.requests = parseNumber(value, option);
        } else if (option == "--start") {
            options.range_start = parseNumber(value, option);
        } else if (option == "--end") {
            options.range_end = parseNumber(value, option);
        } else if (option == "--seed") {
            options.seed = parseNumber(value, option);
        } else if (option == "--page-bits") {
            options.page_bits = parseNumber(value, option);
        } else if (option == "--entries") {
            options.tlb_entries = parseNumber(value, option);
        } else if (option == "--hit-latency") {
            options.hit_latency = parseNumber(value, option);
        } else if (option == "--miss-latency") {
            options.miss_latency = parseNumber(value, option);
        } else if (option == "--log-file") {
            options.log_file = value;
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    return options;
}

void
validateOptions(const TrafficOptions &options)
{
    if (options.page_bits == 0 || options.page_bits >= 63) {
        throw std::invalid_argument("--page-bits must be in the range [1, 62]");
    }
    if (options.tlb_entries == 0 ||
        (options.tlb_entries & (options.tlb_entries - 1)) != 0) {
        throw std::invalid_argument("--entries must be a non-zero power of two");
    }
    if (options.range_start > options.range_end) {
        throw std::invalid_argument("--start must not be greater than --end");
    }
}

void
writeRequest(std::ostream &output, uint64_t request,
             uint64_t virtual_address, const TranslationResult &result)
{
    output << "request=" << request
           << " VA=0x" << std::hex << virtual_address
           << " PA=0x" << result.physical_address << std::dec
           << " " << (result.hit ? "hit" : "miss")
           << " latency=" << result.latency << " cycles\n";
}

void
writeSummary(std::ostream &output, const TrafficOptions &options,
             uint64_t hits, uint64_t misses, uint64_t total_cycles)
{
    const double hit_rate = options.requests == 0
        ? 0.0
        : static_cast<double>(hits) / options.requests;

    output << "Traffic summary\n"
           << "  requests: " << options.requests << "\n"
           << "  range: 0x" << std::hex << options.range_start
           << "-0x" << options.range_end << std::dec << "\n"
           << "  seed: " << options.seed << "\n"
           << "  hits: " << hits << "\n"
           << "  misses: " << misses << "\n"
           << "  hit rate: " << std::fixed << std::setprecision(2)
           << hit_rate * 100.0 << "%\n"
           << "  total latency: " << total_cycles << " cycles\n"
           << "  average latency: "
           << (options.requests == 0
                   ? 0.0
                   : static_cast<double>(total_cycles) / options.requests)
           << " cycles\n";
}

int
main(int argc, char **argv)
{
    try {
        const TrafficOptions options = parseOptions(argc, argv);
        validateOptions(options);

        std::mt19937_64 random(options.seed);
        std::uniform_int_distribution<uint64_t> address(
            options.range_start, options.range_end);
        DirectMappedTranslator translator(options);
        std::ofstream log;

        if (!options.log_file.empty()) {
            log.open(options.log_file);
            if (!log) {
                throw std::runtime_error("could not open log file: " +
                                         options.log_file);
            }
            log << "=== CONFIGURATION ===\n"
                << "requests: " << options.requests << "\n"
                << "range: 0x" << std::hex << options.range_start
                << "-0x" << options.range_end << std::dec << "\n"
                << "seed: " << options.seed << "\n"
                << "page bits: " << options.page_bits << "\n"
                << "TLB entries: " << options.tlb_entries << "\n"
                << "hit latency: " << options.hit_latency << " cycles\n"
                << "miss latency: " << options.miss_latency << " cycles\n\n"
                << "=== REQUESTS ===\n";
        }

        uint64_t total_cycles = 0;
        for (uint64_t request = 0; request < options.requests; ++request) {
            const uint64_t virtual_address = address(random);
            const TranslationResult result = translator.translate(virtual_address);
            total_cycles += result.latency;

            if (options.verbose) {
                writeRequest(std::cout, request, virtual_address, result);
            }
            if (log) {
                writeRequest(log, request, virtual_address, result);
            }
        }

        const uint64_t hits = translator.hitCount();
        const uint64_t misses = translator.missCount();
        writeSummary(std::cout, options, hits, misses, total_cycles);
        if (log) {
            log << "\n=== SUMMARY ===\n";
            writeSummary(log, options, hits, misses, total_cycles);
        }
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}