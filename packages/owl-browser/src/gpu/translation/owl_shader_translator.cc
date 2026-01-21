/**
 * OWL Shader Translator Implementation
 *
 * Translates GLSL shaders to match the precision and behavior characteristics
 * of the target GPU profile for defeating GPU fingerprinting.
 */

#include "gpu/owl_shader_translator.h"
#include "gpu/owl_gpu_context.h"
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace owl {
namespace gpu {

// ============================================================================
// GLSLParser Implementation
// ============================================================================

namespace {

// GLSL keywords
const std::unordered_set<std::string> GLSL_KEYWORDS = {
    "attribute", "const", "uniform", "varying", "buffer", "shared",
    "centroid", "flat", "smooth", "noperspective",
    "layout", "patch", "sample", "break", "continue", "do", "for", "while",
    "switch", "case", "default", "if", "else", "subroutine",
    "in", "out", "inout", "float", "double", "int", "void", "bool",
    "true", "false", "invariant", "precise", "discard", "return",
    "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4",
    "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4",
    "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
    "bvec2", "bvec3", "bvec4", "uvec2", "uvec3", "uvec4",
    "dvec2", "dvec3", "dvec4",
    "lowp", "mediump", "highp", "precision",
    "sampler1D", "sampler2D", "sampler3D", "samplerCube",
    "sampler1DShadow", "sampler2DShadow", "samplerCubeShadow",
    "sampler1DArray", "sampler2DArray", "sampler1DArrayShadow",
    "sampler2DArrayShadow", "isampler1D", "isampler2D", "isampler3D",
    "isamplerCube", "isampler1DArray", "isampler2DArray",
    "usampler1D", "usampler2D", "usampler3D", "usamplerCube",
    "usampler1DArray", "usampler2DArray", "struct"
};

bool IsKeyword(const std::string& word) {
    return GLSL_KEYWORDS.find(word) != GLSL_KEYWORDS.end();
}

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

bool IsAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsAlphaNumeric(char c) {
    return IsAlpha(c) || IsDigit(c);
}

bool IsWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

} // anonymous namespace

std::vector<GLSLParser::Token> GLSLParser::Tokenize(const std::string& source) {
    std::vector<Token> tokens;
    size_t pos = 0;
    size_t line = 1;
    size_t column = 1;

    while (pos < source.length()) {
        char c = source[pos];

        // Track line and column
        if (c == '\n') {
            // Add newline as whitespace token
            tokens.push_back({TokenType::Whitespace, "\n", line, column});
            line++;
            column = 1;
            pos++;
            continue;
        }

        // Whitespace
        if (IsWhitespace(c)) {
            std::string ws;
            while (pos < source.length() && IsWhitespace(source[pos]) && source[pos] != '\n') {
                ws += source[pos++];
                column++;
            }
            tokens.push_back({TokenType::Whitespace, ws, line, column - ws.length()});
            continue;
        }

        // Single-line comment
        if (c == '/' && pos + 1 < source.length() && source[pos + 1] == '/') {
            std::string comment;
            while (pos < source.length() && source[pos] != '\n') {
                comment += source[pos++];
                column++;
            }
            tokens.push_back({TokenType::Comment, comment, line, column - comment.length()});
            continue;
        }

        // Multi-line comment
        if (c == '/' && pos + 1 < source.length() && source[pos + 1] == '*') {
            std::string comment = "/*";
            pos += 2;
            column += 2;
            while (pos + 1 < source.length() && !(source[pos] == '*' && source[pos + 1] == '/')) {
                if (source[pos] == '\n') {
                    line++;
                    column = 1;
                }
                comment += source[pos++];
                column++;
            }
            if (pos + 1 < source.length()) {
                comment += "*/";
                pos += 2;
                column += 2;
            }
            tokens.push_back({TokenType::Comment, comment, line, column - comment.length()});
            continue;
        }

        // Preprocessor directive
        if (c == '#') {
            std::string directive;
            while (pos < source.length() && source[pos] != '\n') {
                // Handle line continuation
                if (source[pos] == '\\' && pos + 1 < source.length() && source[pos + 1] == '\n') {
                    directive += source[pos++];
                    directive += source[pos++];
                    line++;
                    column = 1;
                    continue;
                }
                directive += source[pos++];
                column++;
            }
            tokens.push_back({TokenType::Preprocessor, directive, line, column - directive.length()});
            continue;
        }

        // Identifier or keyword
        if (IsAlpha(c)) {
            std::string ident;
            size_t start_col = column;
            while (pos < source.length() && IsAlphaNumeric(source[pos])) {
                ident += source[pos++];
                column++;
            }
            TokenType type = IsKeyword(ident) ? TokenType::Keyword : TokenType::Identifier;
            tokens.push_back({type, ident, line, start_col});
            continue;
        }

        // Number
        if (IsDigit(c) || (c == '.' && pos + 1 < source.length() && IsDigit(source[pos + 1]))) {
            std::string num;
            size_t start_col = column;

            // Handle hex
            if (c == '0' && pos + 1 < source.length() && (source[pos + 1] == 'x' || source[pos + 1] == 'X')) {
                num += source[pos++];
                num += source[pos++];
                column += 2;
                while (pos < source.length() && (IsDigit(source[pos]) ||
                       (source[pos] >= 'a' && source[pos] <= 'f') ||
                       (source[pos] >= 'A' && source[pos] <= 'F'))) {
                    num += source[pos++];
                    column++;
                }
            } else {
                // Decimal (with optional float components)
                while (pos < source.length() && IsDigit(source[pos])) {
                    num += source[pos++];
                    column++;
                }
                if (pos < source.length() && source[pos] == '.') {
                    num += source[pos++];
                    column++;
                    while (pos < source.length() && IsDigit(source[pos])) {
                        num += source[pos++];
                        column++;
                    }
                }
                // Exponent
                if (pos < source.length() && (source[pos] == 'e' || source[pos] == 'E')) {
                    num += source[pos++];
                    column++;
                    if (pos < source.length() && (source[pos] == '+' || source[pos] == '-')) {
                        num += source[pos++];
                        column++;
                    }
                    while (pos < source.length() && IsDigit(source[pos])) {
                        num += source[pos++];
                        column++;
                    }
                }
                // Float suffix
                if (pos < source.length() && (source[pos] == 'f' || source[pos] == 'F')) {
                    num += source[pos++];
                    column++;
                }
            }

            // Unsigned suffix
            if (pos < source.length() && (source[pos] == 'u' || source[pos] == 'U')) {
                num += source[pos++];
                column++;
            }

            tokens.push_back({TokenType::Number, num, line, start_col});
            continue;
        }

        // Multi-character operators
        std::string op;
        op += c;
        if (pos + 1 < source.length()) {
            char next = source[pos + 1];
            // Two-character operators
            if ((c == '=' && next == '=') || (c == '!' && next == '=') ||
                (c == '<' && next == '=') || (c == '>' && next == '=') ||
                (c == '&' && next == '&') || (c == '|' && next == '|') ||
                (c == '+' && next == '+') || (c == '-' && next == '-') ||
                (c == '+' && next == '=') || (c == '-' && next == '=') ||
                (c == '*' && next == '=') || (c == '/' && next == '=') ||
                (c == '<' && next == '<') || (c == '>' && next == '>') ||
                (c == '^' && next == '^')) {
                op += next;
                pos++;
                column++;
            }
        }

        // Determine operator vs punctuation
        TokenType type = TokenType::Operator;
        if (c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ';' || c == ',' || c == '.') {
            type = TokenType::Punctuation;
        }

        tokens.push_back({type, op, line, column});
        pos++;
        column++;
    }

    tokens.push_back({TokenType::EndOfFile, "", line, column});
    return tokens;
}

std::vector<GLSLParser::PrecisionDecl> GLSLParser::FindPrecisionDeclarations(
    const std::vector<Token>& tokens) {

    std::vector<PrecisionDecl> decls;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::Keyword && tokens[i].value == "precision") {
            // precision <qualifier> <type>;
            size_t j = i + 1;
            // Skip whitespace
            while (j < tokens.size() && tokens[j].type == TokenType::Whitespace) j++;

            if (j < tokens.size() && tokens[j].type == TokenType::Keyword) {
                std::string precision = tokens[j].value;
                j++;
                // Skip whitespace
                while (j < tokens.size() && tokens[j].type == TokenType::Whitespace) j++;

                if (j < tokens.size() && (tokens[j].type == TokenType::Keyword ||
                                          tokens[j].type == TokenType::Identifier)) {
                    PrecisionDecl decl;
                    decl.precision = precision;
                    decl.type = tokens[j].value;
                    decl.token_index = i;
                    decls.push_back(decl);
                }
            }
        }
    }

    return decls;
}

std::vector<GLSLParser::ExtensionDirective> GLSLParser::FindExtensionDirectives(
    const std::vector<Token>& tokens) {

    std::vector<ExtensionDirective> directives;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::Preprocessor) {
            const std::string& value = tokens[i].value;
            if (value.find("#extension") != std::string::npos) {
                // Parse #extension name : behavior
                std::regex ext_regex(R"(#extension\s+(\w+)\s*:\s*(\w+))");
                std::smatch match;
                if (std::regex_search(value, match, ext_regex) && match.size() == 3) {
                    ExtensionDirective directive;
                    directive.name = match[1].str();
                    directive.behavior = match[2].str();
                    directive.token_index = i;
                    directives.push_back(directive);
                }
            }
        }
    }

    return directives;
}

std::vector<GLSLParser::FunctionCall> GLSLParser::FindFunctionCalls(
    const std::vector<Token>& tokens) {

    std::vector<FunctionCall> calls;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::Identifier) {
            // Look ahead for opening parenthesis
            size_t j = i + 1;
            while (j < tokens.size() && tokens[j].type == TokenType::Whitespace) j++;

            if (j < tokens.size() && tokens[j].type == TokenType::Punctuation &&
                tokens[j].value == "(") {

                FunctionCall call;
                call.name = tokens[i].value;
                call.token_index = i;
                call.args_start = j;

                // Find closing parenthesis
                int paren_depth = 1;
                j++;
                while (j < tokens.size() && paren_depth > 0) {
                    if (tokens[j].value == "(") paren_depth++;
                    else if (tokens[j].value == ")") paren_depth--;
                    j++;
                }
                call.args_end = j - 1;

                calls.push_back(call);
            }
        }
    }

    return calls;
}

std::string GLSLParser::RebuildSource(const std::vector<Token>& tokens) {
    std::stringstream ss;
    for (const auto& token : tokens) {
        if (token.type != TokenType::EndOfFile) {
            ss << token.value;
        }
    }
    return ss.str();
}

// ============================================================================
// ShaderTranslator Implementation
// ============================================================================

ShaderTranslator::ShaderTranslator()
    : parser_(std::make_unique<GLSLParser>()) {
    InitializeQuirkHandlers();
    InitializeExtensionEmulators();
}

ShaderTranslator::~ShaderTranslator() = default;

ShaderTranslationResult ShaderTranslator::Translate(const std::string& source,
                                                      ShaderType type,
                                                      const GPUProfile& profile,
                                                      const ShaderTranslationOptions& options) {
    ShaderTranslationResult result;
    result.original_source = source;

    // Note: No try/catch since exceptions are disabled in CEF builds
    result.translated_source = TranslateInternal(source, type, profile, options, result);

    // Check if translation succeeded (indicated by non-empty result)
    if (!result.translated_source.empty()) {
        result.success = true;

        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.shaders_translated++;
        stats_.precision_changes += result.precision_changes;
        stats_.quirk_applications += result.quirk_emulations;
        stats_.extension_filters += result.extension_changes;
    } else {
        result.success = false;
        result.error_message = "Translation failed";
        result.translated_source = source;  // Return original on error

        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.translation_errors++;
    }

    return result;
}

ShaderTranslationResult ShaderTranslator::Translate(const std::string& source,
                                                      ShaderType type,
                                                      GPUContext* context,
                                                      const ShaderTranslationOptions& options) {
    if (!context) {
        ShaderTranslationResult result;
        result.success = false;
        result.error_message = "No GPU context provided";
        result.translated_source = source;
        return result;
    }

    return Translate(source, type, context->GetProfile(), options);
}

bool ShaderTranslator::NeedsTranslation(const std::string& source,
                                         const GPUProfile& profile) {
    // Quick check for potential translation needs
    auto tokens = parser_->Tokenize(source);

    // Check precision declarations
    auto precisions = parser_->FindPrecisionDeclarations(tokens);
    if (!precisions.empty()) return true;

    // Check extensions
    auto extensions = parser_->FindExtensionDirectives(tokens);
    if (!extensions.empty()) return true;

    // Check for functions that might need quirk handling
    auto calls = parser_->FindFunctionCalls(tokens);
    for (const auto& call : calls) {
        if (call.name == "sqrt" || call.name == "inversesqrt" ||
            call.name == "normalize" || call.name == "fract" ||
            call.name == "mod" || call.name == "pow") {
            return true;
        }
    }

    return false;
}

std::string ShaderTranslator::TranslateInternal(const std::string& source,
                                                  ShaderType type,
                                                  const GPUProfile& profile,
                                                  const ShaderTranslationOptions& options,
                                                  ShaderTranslationResult& result) {
    std::string translated = source;

    // Step 1: Normalize precision
    if (options.normalize_precision) {
        std::string before = translated;
        translated = NormalizePrecision(translated, type, profile);
        if (translated != before) {
            result.precision_changes++;
        }
    }

    // Step 2: Apply GPU quirks
    if (options.emulate_quirks) {
        std::string before = translated;
        translated = ApplyGPUQuirks(translated, profile.GetVendor(), profile.GetArchitecture());
        if (translated != before) {
            result.quirk_emulations++;
        }
    }

    // Step 3: Filter extensions
    if (options.strip_unsupported_extensions) {
        std::string before = translated;
        translated = FilterExtensions(translated, profile.GetCapabilities().extensions);
        if (translated != before) {
            result.extension_changes++;
        }
    }

    // Step 4: Inject noise if requested
    if (options.inject_noise) {
        translated = InjectNoise(translated, options.noise_seed, options.noise_intensity);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.noise_injections++;
    }

    // Step 5: Add debug comments if requested
    if (options.add_debug_comments) {
        std::stringstream ss;
        ss << "// OWL Shader Translation\n";
        ss << "// Target: " << profile.GetCapabilities().renderer << "\n";
        ss << "// Precision changes: " << result.precision_changes << "\n";
        ss << "// Quirk emulations: " << result.quirk_emulations << "\n";
        if (options.preserve_original) {
            ss << "/* Original source:\n" << source << "\n*/\n";
        }
        translated = ss.str() + translated;
    }

    return translated;
}

// ==================== Precision Normalization ====================

std::string ShaderTranslator::NormalizePrecision(const std::string& source,
                                                   ShaderType type,
                                                   const GPUProfile& profile) {
    auto tokens = parser_->Tokenize(source);
    auto precisions = parser_->FindPrecisionDeclarations(tokens);

    // Determine target precision based on profile and shader type
    PrecisionMode target_precision;
    if (type == ShaderType::Vertex) {
        target_precision = profile.GetCapabilities().vertex_shader_precision;
    } else {
        target_precision = profile.GetCapabilities().fragment_shader_precision;
    }

    std::string target_str;
    switch (target_precision) {
        case PrecisionMode::LowP: target_str = "lowp"; break;
        case PrecisionMode::MediumP: target_str = "mediump"; break;
        case PrecisionMode::HighP:
        default: target_str = "highp"; break;
    }

    // Modify precision declarations
    for (const auto& decl : precisions) {
        // Find and replace the precision qualifier
        for (size_t i = decl.token_index; i < tokens.size(); ++i) {
            if (tokens[i].type == GLSLParser::TokenType::Keyword &&
                (tokens[i].value == "highp" || tokens[i].value == "mediump" ||
                 tokens[i].value == "lowp")) {
                tokens[i].value = target_str;
                break;
            }
        }
    }

    // Add default precision if not present for fragment shaders
    if (type == ShaderType::Fragment && precisions.empty()) {
        std::stringstream prefix;
        prefix << "precision " << target_str << " float;\n";

        // Find version directive or start
        bool inserted = false;
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].type == GLSLParser::TokenType::Preprocessor &&
                tokens[i].value.find("#version") != std::string::npos) {
                // Insert after version line
                for (size_t j = i + 1; j < tokens.size(); ++j) {
                    if (tokens[j].type == GLSLParser::TokenType::Whitespace &&
                        tokens[j].value.find('\n') != std::string::npos) {
                        tokens.insert(tokens.begin() + j + 1,
                            {GLSLParser::TokenType::Keyword, prefix.str(), 0, 0});
                        inserted = true;
                        break;
                    }
                }
                break;
            }
        }
        if (!inserted) {
            tokens.insert(tokens.begin(),
                {GLSLParser::TokenType::Keyword, prefix.str(), 0, 0});
        }
    }

    return parser_->RebuildSource(tokens);
}

std::string ShaderTranslator::InsertPrecisionEmulation(const std::string& source,
                                                         PrecisionMode source_precision,
                                                         PrecisionMode target_precision) {
    if (source_precision == target_precision) return source;
    if (target_precision == PrecisionMode::HighP) return source;  // No emulation needed

    std::string emulation = GeneratePrecisionEmulationFunctions(target_precision);

    // Insert after version directive
    std::regex version_regex(R"(#version\s+\d+(\s+\w+)?\s*\n)");
    std::smatch match;
    if (std::regex_search(source, match, version_regex)) {
        return match.prefix().str() + match.str() + emulation + match.suffix().str();
    }

    // No version, insert at start
    return emulation + source;
}

std::string ShaderTranslator::GeneratePrecisionEmulationFunctions(PrecisionMode target) {
    std::stringstream ss;
    ss << "// OWL Precision Emulation\n";

    if (target == PrecisionMode::MediumP) {
        ss << R"(
float _owl_mediump(float v) {
    // Emulate mediump: ~16-bit float, 10-bit mantissa
    float sign = v < 0.0 ? -1.0 : 1.0;
    float abs_v = abs(v);
    if (abs_v < 6.1e-5) return 0.0;  // Below mediump min
    if (abs_v > 65504.0) return sign * 65504.0;  // Clamp to mediump max
    // Quantize mantissa to 10 bits
    float e = floor(log2(abs_v));
    float m = abs_v / exp2(e);
    m = floor(m * 1024.0) / 1024.0;
    return sign * m * exp2(e);
}
vec2 _owl_mediump(vec2 v) { return vec2(_owl_mediump(v.x), _owl_mediump(v.y)); }
vec3 _owl_mediump(vec3 v) { return vec3(_owl_mediump(v.x), _owl_mediump(v.y), _owl_mediump(v.z)); }
vec4 _owl_mediump(vec4 v) { return vec4(_owl_mediump(v.x), _owl_mediump(v.y), _owl_mediump(v.z), _owl_mediump(v.w)); }
)";
    } else if (target == PrecisionMode::LowP) {
        ss << R"(
float _owl_lowp(float v) {
    // Emulate lowp: ~8-bit fixed-point [-2,2] range
    return clamp(floor(v * 128.0 + 0.5) / 128.0, -2.0, 2.0);
}
vec2 _owl_lowp(vec2 v) { return vec2(_owl_lowp(v.x), _owl_lowp(v.y)); }
vec3 _owl_lowp(vec3 v) { return vec3(_owl_lowp(v.x), _owl_lowp(v.y), _owl_lowp(v.z)); }
vec4 _owl_lowp(vec4 v) { return vec4(_owl_lowp(v.x), _owl_lowp(v.y), _owl_lowp(v.z), _owl_lowp(v.w)); }
)";
    }

    return ss.str();
}

std::string ShaderTranslator::GenerateDenormalFlushCode() {
    return R"(
// OWL Denormal Flush
float _owl_flush_denormal(float v) {
    return abs(v) < 1.175494e-38 ? 0.0 : v;
}
)";
}

// ==================== GPU Quirk Emulation ====================

void ShaderTranslator::InitializeQuirkHandlers() {
    // NVIDIA quirks
    vendor_quirks_[GPUVendor::NVIDIA] = {
        {"nvidia_sqrt_precision",
         "NVIDIA sqrt has slightly different precision",
         [](const std::string& src) {
             // NVIDIA tends to have more precise sqrt
             return src;  // No modification needed, just tracking
         }},
        {"nvidia_pow_zero",
         "NVIDIA pow(0,x) behavior",
         [](const std::string& src) {
             return src;
         }}
    };

    // AMD quirks
    vendor_quirks_[GPUVendor::AMD] = {
        {"amd_inversesqrt",
         "AMD inversesqrt may use fast approximation",
         [](const std::string& src) {
             return src;
         }},
        {"amd_mod_negative",
         "AMD mod with negative numbers",
         [](const std::string& src) {
             return src;
         }}
    };

    // Intel quirks
    vendor_quirks_[GPUVendor::Intel] = {
        {"intel_denormals",
         "Intel may flush denormals",
         [](const std::string& src) {
             return src;
         }},
        {"intel_fast_math",
         "Intel driver fast math optimizations",
         [](const std::string& src) {
             return src;
         }}
    };

    // Apple quirks
    vendor_quirks_[GPUVendor::Apple] = {
        {"apple_metal_precision",
         "Apple Metal precision differences",
         [](const std::string& src) {
             return src;
         }},
        {"apple_half_precision",
         "Apple favors half precision where possible",
         [](const std::string& src) {
             return src;
         }}
    };
}

std::string ShaderTranslator::ApplyGPUQuirks(const std::string& source,
                                               GPUVendor vendor,
                                               GPUArchitecture architecture) {
    std::string result = source;

    // Apply vendor-specific quirks
    auto vendor_it = vendor_quirks_.find(vendor);
    if (vendor_it != vendor_quirks_.end()) {
        for (const auto& quirk : vendor_it->second) {
            result = quirk.apply(result);
        }
    }

    // Apply architecture-specific quirks
    auto arch_it = arch_quirks_.find(architecture);
    if (arch_it != arch_quirks_.end()) {
        for (const auto& quirk : arch_it->second) {
            result = quirk.apply(result);
        }
    }

    return result;
}

std::vector<ShaderTranslator::GPUQuirk> ShaderTranslator::GetQuirksForVendor(GPUVendor vendor) {
    auto it = vendor_quirks_.find(vendor);
    if (it != vendor_quirks_.end()) {
        return it->second;
    }
    return {};
}

std::vector<ShaderTranslator::GPUQuirk> ShaderTranslator::GetQuirksForArchitecture(GPUArchitecture arch) {
    auto it = arch_quirks_.find(arch);
    if (it != arch_quirks_.end()) {
        return it->second;
    }
    return {};
}

// ==================== Extension Handling ====================

void ShaderTranslator::InitializeExtensionEmulators() {
    // OES_standard_derivatives emulation
    extension_emulators_["GL_OES_standard_derivatives"] = [](const std::string& src) {
        // dFdx/dFdy are usually available but can be emulated with differences
        return src;
    };

    // OES_texture_float emulation
    extension_emulators_["GL_OES_texture_float"] = [](const std::string& src) {
        return src;
    };

    // EXT_shader_texture_lod emulation
    extension_emulators_["GL_EXT_shader_texture_lod"] = [](const std::string& src) {
        // texture2DLod can be emulated with bias
        return src;
    };
}

std::string ShaderTranslator::FilterExtensions(const std::string& source,
                                                 const std::vector<std::string>& supported_extensions) {
    auto tokens = parser_->Tokenize(source);
    auto directives = parser_->FindExtensionDirectives(tokens);

    std::unordered_set<std::string> supported_set(
        supported_extensions.begin(), supported_extensions.end());

    // Remove unsupported extensions
    for (auto it = directives.rbegin(); it != directives.rend(); ++it) {
        if (supported_set.find(it->name) == supported_set.end()) {
            // Comment out the extension directive
            tokens[it->token_index].value = "// " + tokens[it->token_index].value +
                " // OWL: Extension not supported by target profile";
        }
    }

    return parser_->RebuildSource(tokens);
}

std::string ShaderTranslator::EmulateExtension(const std::string& source,
                                                 const std::string& extension_name) {
    auto it = extension_emulators_.find(extension_name);
    if (it != extension_emulators_.end()) {
        return it->second(source);
    }
    return source;
}

// ==================== Noise Injection ====================

std::string ShaderTranslator::InjectNoise(const std::string& source,
                                           uint64_t seed,
                                           double intensity) {
    // Insert noise function and modify color outputs
    std::stringstream noise_func;
    noise_func << R"(
// OWL Deterministic Noise for Fingerprint Masking
float _owl_noise(vec2 co) {
    float seed = )" << static_cast<double>(seed % 10000) / 10000.0 << R"(;
    return fract(sin(dot(co, vec2(12.9898 + seed, 78.233 + seed))) * 43758.5453);
}
vec3 _owl_apply_noise(vec3 color, vec2 coord) {
    float n = (_owl_noise(coord) - 0.5) * )" << intensity << R"(;
    return clamp(color + n, 0.0, 1.0);
}
)";

    // Find version directive
    std::regex version_regex(R"(#version\s+\d+(\s+\w+)?\s*\n)");
    std::smatch match;
    if (std::regex_search(source, match, version_regex)) {
        return match.prefix().str() + match.str() + noise_func.str() + match.suffix().str();
    }

    return noise_func.str() + source;
}

// ==================== Validation ====================

bool ShaderTranslator::ValidateShader(const std::string& source, ShaderType type) {
    validation_errors_.clear();

    // Basic syntax validation
    auto tokens = parser_->Tokenize(source);

    // Check for matching braces
    int brace_count = 0;
    int paren_count = 0;
    int bracket_count = 0;

    for (const auto& token : tokens) {
        if (token.value == "{") brace_count++;
        else if (token.value == "}") brace_count--;
        else if (token.value == "(") paren_count++;
        else if (token.value == ")") paren_count--;
        else if (token.value == "[") bracket_count++;
        else if (token.value == "]") bracket_count--;

        if (brace_count < 0) {
            validation_errors_.push_back("Unmatched closing brace at line " +
                std::to_string(token.line));
        }
        if (paren_count < 0) {
            validation_errors_.push_back("Unmatched closing parenthesis at line " +
                std::to_string(token.line));
        }
        if (bracket_count < 0) {
            validation_errors_.push_back("Unmatched closing bracket at line " +
                std::to_string(token.line));
        }
    }

    if (brace_count != 0) {
        validation_errors_.push_back("Unmatched braces in shader");
    }
    if (paren_count != 0) {
        validation_errors_.push_back("Unmatched parentheses in shader");
    }
    if (bracket_count != 0) {
        validation_errors_.push_back("Unmatched brackets in shader");
    }

    // Check for main function
    bool has_main = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == GLSLParser::TokenType::Identifier &&
            tokens[i].value == "main") {
            // Check if followed by parentheses
            size_t j = i + 1;
            while (j < tokens.size() && tokens[j].type == GLSLParser::TokenType::Whitespace) j++;
            if (j < tokens.size() && tokens[j].value == "(") {
                has_main = true;
                break;
            }
        }
    }

    if (!has_main) {
        validation_errors_.push_back("Shader missing main() function");
    }

    return validation_errors_.empty();
}

std::vector<std::string> ShaderTranslator::GetValidationErrors() const {
    return validation_errors_;
}

// ==================== Statistics ====================

ShaderTranslator::TranslatorStats ShaderTranslator::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void ShaderTranslator::ResetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = TranslatorStats{};
}

// ============================================================================
// PrecisionEmulator Implementation
// ============================================================================

float PrecisionEmulator::EmulateMediumP(float value) {
    return RoundToPrecision(value, 10);  // ~16-bit float has 10-bit mantissa
}

float PrecisionEmulator::EmulateLowP(float value) {
    // lowp is typically fixed-point [-2, 2] with 8-bit precision
    if (value < -2.0f) return -2.0f;
    if (value > 2.0f) return 2.0f;
    return std::round(value * 128.0f) / 128.0f;
}

float PrecisionEmulator::RoundToPrecision(float value, int mantissa_bits) {
    if (value == 0.0f || !std::isfinite(value)) return value;

    // Get the magnitude
    int exp;
    float mantissa = std::frexp(value, &exp);

    // Round mantissa to specified bits
    float scale = static_cast<float>(1 << mantissa_bits);
    mantissa = std::round(mantissa * scale) / scale;

    return std::ldexp(mantissa, exp);
}

float PrecisionEmulator::FlushDenormal(float value) {
    // Check if denormal (very small number)
    if (std::fpclassify(value) == FP_SUBNORMAL) {
        return 0.0f;
    }
    return value;
}

std::string PrecisionEmulator::GenerateGLSL(PrecisionMode target) {
    std::stringstream ss;

    if (target == PrecisionMode::MediumP) {
        ss << R"(
// Precision emulation: mediump
float emulate_mediump(float v) {
    if (abs(v) < 6.1e-5) return 0.0;
    if (abs(v) > 65504.0) return sign(v) * 65504.0;
    float e = floor(log2(abs(v)));
    float m = abs(v) / exp2(e);
    m = floor(m * 1024.0 + 0.5) / 1024.0;
    return sign(v) * m * exp2(e);
}
)";
    } else if (target == PrecisionMode::LowP) {
        ss << R"(
// Precision emulation: lowp
float emulate_lowp(float v) {
    return clamp(floor(v * 128.0 + 0.5) / 128.0, -2.0, 2.0);
}
)";
    }

    return ss.str();
}

} // namespace gpu
} // namespace owl
