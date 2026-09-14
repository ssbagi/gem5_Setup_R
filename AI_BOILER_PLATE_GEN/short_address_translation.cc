/*
 * Small address-translation example.
 *
 * This is intentionally independent of gem5 so the core mapping can be
 * compiled and tested in isolation.
 */

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

struct TranslationResult
{
    uint64_t physical_address;
    bool hit;
};

TranslationResult
translate(uint64_t virtual_address, uint64_t page_size,
           uint64_t physical_offset)
{
    const uint64_t page_offset = virtual_address % page_size;
    const uint64_t virtual_page = virtual_address / page_size;
    const uint64_t physical_page = virtual_page + physical_offset;

    return {physical_page * page_size + page_offset, true};
}

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
              << "  --address ADDRESS          Virtual address (default: 0x2345)\n"
              << "  --page-size BYTES          Page size (default: 4096)\n"
              << "  --physical-offset PAGES    Page-number offset (default: 0x1000)\n"
              << "  --log-file PATH             Write all sections to PATH\n"
              << "  --help                      Show this help\n";
}

int
main(int argc, char **argv)
{
    try {
        uint64_t virtual_address = 0x2345;
        uint64_t page_size = 4096;
        uint64_t physical_offset = 0x1000;
        std::string log_file;

        for (int argument = 1; argument < argc; ++argument) {
            const std::string option = argv[argument];
            if (option == "--help") {
                printUsage(argv[0]);
                return 0;
            }
            if (argument + 1 >= argc) {
                throw std::invalid_argument("missing value for " + option);
            }
            const std::string value = argv[++argument];
            if (option == "--address") {
                virtual_address = parseNumber(value, option);
            } else if (option == "--page-size") {
                page_size = parseNumber(value, option);
            } else if (option == "--physical-offset") {
                physical_offset = parseNumber(value, option);
            } else if (option == "--log-file") {
                log_file = value;
            } else {
                throw std::invalid_argument("unknown option: " + option);
            }
        }

        if (page_size == 0) {
            throw std::invalid_argument("--page-size must be non-zero");
        }

        const TranslationResult result =
            translate(virtual_address, page_size, physical_offset);

        std::ofstream log;
        if (!log_file.empty()) {
            log.open(log_file);
            if (!log) {
                throw std::runtime_error("could not open log file: " +
                                         log_file);
            }
            log << "=== CONFIGURATION ===\n"
                << "virtual address: 0x" << std::hex << virtual_address
                << "\npage size: " << std::dec << page_size
                << " bytes\nphysical offset: 0x" << std::hex
                << physical_offset << std::dec << " pages\n\n"
                << "=== TRANSLATION ===\n"
                << "VA=0x" << std::hex << virtual_address
                << " PA=0x" << result.physical_address << std::dec
                << " hit=" << std::boolalpha << result.hit << "\n\n"
                << "=== SUMMARY ===\n"
                        << "  virtual address: 0x" << std::hex
                        << virtual_address << "\n"
                        << "  physical address: 0x" << result.physical_address
                        << std::dec << "\n"
                        << "  page size: " << page_size << " bytes\n"
                        << "  physical offset: 0x" << std::hex
                        << physical_offset << std::dec << " pages\n"
                        << "  hit: " << std::boolalpha << result.hit << '\n';
        }

        std::cout << "PA=0x" << std::hex << result.physical_address
                  << " hit=" << std::boolalpha << result.hit << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}