#pragma once

/**
 * OWL Shader Translator
 *
 * Translates GLSL shaders to match the precision and behavior characteristics
 * of the target GPU profile. This is critical for defeating GPU fingerprinting
 * techniques that detect the real GPU through shader behavior.
 *
 * Key Functions:
 * - Precision normalization (match float precision to target GPU)
 * - GPU-specific quirk emulation
 * - Extension compatibility handling
 * - Deterministic noise injection for fingerprint masking
 */

#include "gpu/owl_gpu_virtualization.h"
#include "gpu/owl_gpu_profile.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace owl {
namespace gpu {

// Forward declarations
class GPUContext;

/**
 * Shader type enumeration
 */
enum class ShaderType {
    Vertex,
    Fragment,
    Compute
};

/**
 * Shader translation options
 */
struct ShaderTranslationOptions {
    // Precision handling
    bool normalize_precision = true;      // Normalize float precision
    PrecisionMode target_vertex_precision = PrecisionMode::HighP;
    PrecisionMode target_fragment_precision = PrecisionMode::HighP;

    // GPU quirk emulation
    bool emulate_quirks = true;           // Emulate GPU-specific quirks
    bool force_flush_denormals = false;   // Treat denormals as zero
    bool force_precise_math = true;       // Use precise sqrt/div

    // Extension handling
    bool strip_unsupported_extensions = true;
    bool emulate_missing_extensions = true;

    // Fingerprint masking
    bool inject_noise = false;            // Inject deterministic noise
    uint64_t noise_seed = 0;              // Seed for noise generation
    double noise_intensity = 0.001;       // Very small noise amount

    // Debug
    bool add_debug_comments = false;      // Add comments showing translations
    bool preserve_original = false;       // Keep original code in comments
};

/**
 * Shader translation result
 */
struct ShaderTranslationResult {
    bool success = false;
    std::string translated_source;
    std::string error_message;

    // Statistics
    int precision_changes = 0;
    int quirk_emulations = 0;
    int extension_changes = 0;

    // Original info (for debugging)
    std::string original_source;
    std::vector<std::string> warnings;
};

/**
 * GLSL Shader Parser - Lightweight parser for shader modification
 */
class GLSLParser {
public:
    /**
     * Token types
     */
    enum class TokenType {
        Keyword,
        Identifier,
        Number,
        Operator,
        Punctuation,
        Preprocessor,
        Comment,
        Whitespace,
        String,
        EndOfFile
    };

    /**
     * Token structure
     */
    struct Token {
        TokenType type;
        std::string value;
        size_t line;
        size_t column;
    };

    /**
     * Parse shader source into tokens
     */
    std::vector<Token> Tokenize(const std::string& source);

    /**
     * Find precision declarations
     */
    struct PrecisionDecl {
        std::string precision;  // highp, mediump, lowp
        std::string type;       // float, int, sampler2D, etc.
        size_t token_index;
    };
    std::vector<PrecisionDecl> FindPrecisionDeclarations(const std::vector<Token>& tokens);

    /**
     * Find extension directives
     */
    struct ExtensionDirective {
        std::string name;
        std::string behavior;  // enable, require, warn, disable
        size_t token_index;
    };
    std::vector<ExtensionDirective> FindExtensionDirectives(const std::vector<Token>& tokens);

    /**
     * Find function calls
     */
    struct FunctionCall {
        std::string name;
        size_t token_index;
        size_t args_start;
        size_t args_end;
    };
    std::vector<FunctionCall> FindFunctionCalls(const std::vector<Token>& tokens);

    /**
     * Rebuild source from tokens
     */
    std::string RebuildSource(const std::vector<Token>& tokens);
};

/**
 * Shader Translator
 *
 * Main class for translating shaders to match target GPU behavior
 */
class ShaderTranslator {
public:
    ShaderTranslator();
    ~ShaderTranslator();

    // Non-copyable
    ShaderTranslator(const ShaderTranslator&) = delete;
    ShaderTranslator& operator=(const ShaderTranslator&) = delete;

    // ==================== Translation ====================

    /**
     * Translate a shader source to match the target profile
     */
    ShaderTranslationResult Translate(const std::string& source,
                                       ShaderType type,
                                       const GPUProfile& profile,
                                       const ShaderTranslationOptions& options = {});

    /**
     * Translate using current context's profile
     */
    ShaderTranslationResult Translate(const std::string& source,
                                       ShaderType type,
                                       GPUContext* context,
                                       const ShaderTranslationOptions& options = {});

    /**
     * Quick check if shader needs translation
     */
    bool NeedsTranslation(const std::string& source,
                          const GPUProfile& profile);

    // ==================== Precision Normalization ====================

    /**
     * Normalize precision declarations to match target
     */
    std::string NormalizePrecision(const std::string& source,
                                    ShaderType type,
                                    const GPUProfile& profile);

    /**
     * Insert precision emulation code
     * Adds functions that emulate lower precision math
     */
    std::string InsertPrecisionEmulation(const std::string& source,
                                          PrecisionMode source_precision,
                                          PrecisionMode target_precision);

    // ==================== GPU Quirk Emulation ====================

    /**
     * Apply GPU-specific quirks to shader
     */
    std::string ApplyGPUQuirks(const std::string& source,
                                GPUVendor vendor,
                                GPUArchitecture architecture);

    /**
     * Known GPU quirks by vendor
     */
    struct GPUQuirk {
        std::string name;
        std::string description;
        std::function<std::string(const std::string&)> apply;
    };

    std::vector<GPUQuirk> GetQuirksForVendor(GPUVendor vendor);
    std::vector<GPUQuirk> GetQuirksForArchitecture(GPUArchitecture arch);

    // ==================== Extension Handling ====================

    /**
     * Filter extensions to match target profile
     */
    std::string FilterExtensions(const std::string& source,
                                  const std::vector<std::string>& supported_extensions);

    /**
     * Add extension emulation code
     */
    std::string EmulateExtension(const std::string& source,
                                  const std::string& extension_name);

    // ==================== Noise Injection ====================

    /**
     * Inject deterministic noise into shader calculations
     * This adds tiny variations to floating-point operations that are
     * consistent across runs with the same seed, masking GPU-specific behavior
     */
    std::string InjectNoise(const std::string& source,
                            uint64_t seed,
                            double intensity);

    // ==================== Validation ====================

    /**
     * Validate translated shader (basic syntax check)
     */
    bool ValidateShader(const std::string& source, ShaderType type);

    /**
     * Get validation errors
     */
    std::vector<std::string> GetValidationErrors() const;

    // ==================== Statistics ====================

    struct TranslatorStats {
        uint64_t shaders_translated = 0;
        uint64_t precision_changes = 0;
        uint64_t quirk_applications = 0;
        uint64_t extension_filters = 0;
        uint64_t noise_injections = 0;
        uint64_t translation_errors = 0;
    };

    TranslatorStats GetStats() const;
    void ResetStats();

private:
    // Internal translation helpers
    std::string TranslateInternal(const std::string& source,
                                   ShaderType type,
                                   const GPUProfile& profile,
                                   const ShaderTranslationOptions& options,
                                   ShaderTranslationResult& result);

    // Precision emulation code generators
    std::string GeneratePrecisionEmulationFunctions(PrecisionMode target);
    std::string GenerateDenormalFlushCode();

    // Quirk handlers
    void InitializeQuirkHandlers();
    std::unordered_map<GPUVendor, std::vector<GPUQuirk>> vendor_quirks_;
    std::unordered_map<GPUArchitecture, std::vector<GPUQuirk>> arch_quirks_;

    // Extension emulators
    void InitializeExtensionEmulators();
    std::unordered_map<std::string, std::function<std::string(const std::string&)>> extension_emulators_;

    // Parser
    std::unique_ptr<GLSLParser> parser_;

    // Statistics
    mutable std::mutex stats_mutex_;
    TranslatorStats stats_;

    // Validation errors
    std::vector<std::string> validation_errors_;
};

/**
 * Precision Emulator
 *
 * Provides functions to emulate different floating-point precision levels
 */
class PrecisionEmulator {
public:
    /**
     * Emulate mediump float (16-bit) behavior in highp
     */
    static float EmulateMediumP(float value);

    /**
     * Emulate lowp float (10-bit) behavior in highp
     */
    static float EmulateLowP(float value);

    /**
     * Round to target precision
     */
    static float RoundToPrecision(float value, int mantissa_bits);

    /**
     * Flush denormals to zero
     */
    static float FlushDenormal(float value);

    /**
     * Generate GLSL code for precision emulation
     */
    static std::string GenerateGLSL(PrecisionMode target);
};

} // namespace gpu
} // namespace owl
