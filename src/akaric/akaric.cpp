// MIT License
//
// Copyright (c) 2020 椎名深雪
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <json.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <akari/core/akari.h>
#include <akaric/parser.h>
#include <akaric/codegen.h>
// Akari Unified Shading Language Compiler
using namespace akari;
const char *backend_help = R"(One of:
    cpp : C++17
    cuda: CUDA
    glsl: (Not Implemented)
    metal: (Not Implemented)

    If not supplied will be inferred from output file suffix
)";
static std::string input, output, backend;
static bool verbose = false;
void parse(int argc, const char **argv) {
    try {
        cxxopts::Options options("akaric", " - Akari Unified Shading Language Compiler");
        options.show_positional_help();
        {
            auto opt = options.allow_unrecognised_options().add_options();
            opt("i,input", "Input: A build.json file", cxxopts::value<std::string>());
            opt("o,output", "Output filename", cxxopts::value<std::string>());
            opt("b, backend", backend_help, cxxopts::value<std::string>());
            opt("v,verbose", "Verbose output (includes debug info)");
            opt("help", "Show this help");
            options.parse_positional("input");
            auto result = options.parse(argc, argv);
            if (!result.count("input")) {
                std::cerr << options.help() << std::endl;
                fmt::print(stderr, "error: Input file must be provided\n");
                exit(1);
            }
            if (!result.count("output")) {

                std::cerr << options.help() << std::endl;
                fmt::print(stderr, "error: Output file must be provided\n");
                exit(1);
            }
            if (result.count("verbose")) {
                verbose = true;
            }
            input = result["input"].as<std::string>();
            output = result["output"].as<std::string>();
            if (!result.count("backend")) {
                auto ext = fs::path(output).extension().string();
                if (ext == ".cu") {
                    backend = "cuda";
                } else if (ext == ".cpp") {
                    backend = "cpp";
                }
            } else {
                backend = result["backend"].as<std::string>();
            }
        }
    } catch (std::exception &e) {
        std::cerr << "error parsing options: " << e.what() << std::endl;
        exit(1);
    }
}
int main(int argc, const char **argv) {
    using namespace akari::asl;
    using namespace nlohmann;
    try {
        parse(argc, argv);
        json build_config;
        {
            std::ifstream in(input);
            std::stringstream src;
            src << in.rdbuf();
            build_config = json::parse(src.str());
        }
        if (!build_config.contains("name")) {
            std::cerr << "module name must be specified" << std::endl;
            exit(1);
        }
        if (!build_config.contains("src")) {
            std::cerr << "source files must be specified" << std::endl;
            exit(1);
        }
        Parser parser;
        Module module;
        module.name = build_config["name"].get<std::string>();
        if (build_config.contains("type-parameters")) {
            auto params = build_config["type-parameters"];
            if (!params.is_array()) {
                std::cerr << "type-parameters must be array" << std::endl;
                exit(1);
            }
            for (auto &p : params) {
                module.type_parameters.emplace_back(p.get<std::string>());
                parser.add_type_parameter(p.get<std::string>());
            }
        }

        std::unique_ptr<CodeGenerator> codegen;
        if (backend == "cpp") {
            codegen = cpp_generator();
        } else if (backend == "cuda") {
            codegen = cuda_generator();
        } else {
            std::cerr << backend << " backend is not implemented" << std::endl;
            exit(1);
        }
        std::vector<std::string> sources;
        for (auto src_file : build_config["src"]) {
            sources.emplace_back(src_file.get<std::string>());
        }
        {
            CurrentPathGuard __;
            fs::current_path(fs::absolute(fs::path(input)).parent_path());
            auto out = parser(sources);
            for (auto &unit : out) {
                if (verbose) {
                    json _;
                    unit.tree->dump_json(_);
                    std::cout << _.dump(1) << std::endl;
                }
                module.translation_units.emplace_back(unit.tree);
            }
        }
        std::ofstream os(output);
        os << codegen->generate(BuildConfig{}, module);
    } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
}