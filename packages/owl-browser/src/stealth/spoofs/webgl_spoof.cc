#include "stealth/spoofs/webgl_spoof.h"
#include "util/logger.h"
#include <sstream>
#include <iomanip>

namespace owl {
namespace spoofs {

WebGLSpoof::Config WebGLSpoof::Config::FromVM(const VirtualMachine& vm) {
    Config config;
    config.vendor = vm.gpu.vendor;
    config.renderer = vm.gpu.renderer;
    config.unmasked_vendor = vm.gpu.unmasked_vendor;
    config.unmasked_renderer = vm.gpu.unmasked_renderer;
    config.version = vm.gpu.webgl_version;
    config.version2 = vm.gpu.webgl2_version;
    config.shading_language = vm.gpu.shading_language;
    config.max_texture_size = vm.gpu.max_texture_size;
    config.max_cube_map_texture_size = vm.gpu.max_cube_map_texture_size;
    config.max_render_buffer_size = vm.gpu.max_render_buffer_size;
    config.max_vertex_attribs = vm.gpu.max_vertex_attribs;
    config.max_vertex_uniform_vectors = vm.gpu.max_vertex_uniform_vectors;
    config.max_vertex_texture_units = vm.gpu.max_vertex_texture_units;
    config.max_varying_vectors = vm.gpu.max_varying_vectors;
    config.max_fragment_uniform_vectors = vm.gpu.max_fragment_uniform_vectors;
    config.max_texture_units = vm.gpu.max_texture_units;
    config.max_combined_texture_units = vm.gpu.max_combined_texture_units;
    config.max_viewport_dims_w = vm.gpu.max_viewport_dims_w;
    config.max_viewport_dims_h = vm.gpu.max_viewport_dims_h;
    config.aliased_line_width_min = vm.gpu.aliased_line_width_min;
    config.aliased_line_width_max = vm.gpu.aliased_line_width_max;
    config.aliased_point_size_min = vm.gpu.aliased_point_size_min;
    config.aliased_point_size_max = vm.gpu.aliased_point_size_max;
    config.max_samples = vm.gpu.max_samples;
    config.samples = vm.gpu.samples;
    config.sample_buffers = vm.gpu.sample_buffers;
    config.max_anisotropy = vm.gpu.max_anisotropy;
    config.max_3d_texture_size = vm.gpu.max_3d_texture_size;
    config.max_array_texture_layers = vm.gpu.max_array_texture_layers;
    config.max_color_attachments = vm.gpu.max_color_attachments;
    config.max_draw_buffers = vm.gpu.max_draw_buffers;
    config.max_uniform_buffer_bindings = vm.gpu.max_uniform_buffer_bindings;
    config.max_uniform_block_size = vm.gpu.max_uniform_block_size;
    config.max_combined_uniform_blocks = vm.gpu.max_combined_uniform_blocks;
    config.extensions = vm.gpu.webgl_extensions;
    config.extensions2 = vm.gpu.webgl2_extensions;
    
    // Precision formats
    config.vertex_high_float = {vm.gpu.vertex_high_float.range_min, 
                                 vm.gpu.vertex_high_float.range_max, 
                                 vm.gpu.vertex_high_float.precision};
    config.vertex_medium_float = {vm.gpu.vertex_medium_float.range_min,
                                   vm.gpu.vertex_medium_float.range_max,
                                   vm.gpu.vertex_medium_float.precision};
    config.vertex_low_float = {vm.gpu.vertex_low_float.range_min,
                                vm.gpu.vertex_low_float.range_max,
                                vm.gpu.vertex_low_float.precision};
    config.fragment_high_float = {vm.gpu.fragment_high_float.range_min,
                                   vm.gpu.fragment_high_float.range_max,
                                   vm.gpu.fragment_high_float.precision};
    config.fragment_medium_float = {vm.gpu.fragment_medium_float.range_min,
                                     vm.gpu.fragment_medium_float.range_max,
                                     vm.gpu.fragment_medium_float.precision};
    config.fragment_low_float = {vm.gpu.fragment_low_float.range_min,
                                  vm.gpu.fragment_low_float.range_max,
                                  vm.gpu.fragment_low_float.precision};
    
    config.seed = vm.gpu.renderer_hash_seed;
    return config;
}

bool WebGLSpoof::Inject(CefRefPtr<CefFrame> frame, const Config& config) {
    if (!frame) {
        LOG_DEBUG("WebGLSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(config);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("WebGLSpoof", "Injected WebGL spoofs into frame");
    return true;
}

std::string WebGLSpoof::GenerateScript(const Config& config) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n";
    ss << "  const _seedSym = Symbol.for('__owl_canvas_seed__');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('webgl')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";
    
    // Configuration object
    ss << "  // WebGL configuration\n";
    ss << "  const __webglConfig = {\n";
    ss << "    vendor: \"" << EscapeJS(config.vendor) << "\",\n";
    ss << "    renderer: \"" << EscapeJS(config.renderer) << "\",\n";
    ss << "    unmaskedVendor: \"" << EscapeJS(config.unmasked_vendor) << "\",\n";
    ss << "    unmaskedRenderer: \"" << EscapeJS(config.unmasked_renderer) << "\",\n";
    ss << "    version: \"" << EscapeJS(config.version) << "\",\n";
    ss << "    version2: \"" << EscapeJS(config.version2) << "\",\n";
    ss << "    shadingLanguage: \"" << EscapeJS(config.shading_language) << "\",\n";
    ss << "    shadingLanguageV2: \"" << EscapeJS(config.shading_language_v2) << "\",\n";
    ss << "    maxTextureSize: " << config.max_texture_size << ",\n";
    ss << "    maxCubeMapTextureSize: " << config.max_cube_map_texture_size << ",\n";
    ss << "    maxRenderBufferSize: " << config.max_render_buffer_size << ",\n";
    ss << "    maxVertexAttribs: " << config.max_vertex_attribs << ",\n";
    ss << "    maxVertexUniformVectors: " << config.max_vertex_uniform_vectors << ",\n";
    ss << "    maxVertexTextureUnits: " << config.max_vertex_texture_units << ",\n";
    ss << "    maxVaryingVectors: " << config.max_varying_vectors << ",\n";
    ss << "    maxFragmentUniformVectors: " << config.max_fragment_uniform_vectors << ",\n";
    ss << "    maxTextureUnits: " << config.max_texture_units << ",\n";
    ss << "    maxCombinedTextureUnits: " << config.max_combined_texture_units << ",\n";
    ss << "    maxViewportDims: [" << config.max_viewport_dims_w << ", " << config.max_viewport_dims_h << "],\n";
    ss << "    aliasedLineWidthRange: [" << config.aliased_line_width_min << ", " << config.aliased_line_width_max << "],\n";
    ss << "    aliasedPointSizeRange: [" << config.aliased_point_size_min << ", " << config.aliased_point_size_max << "],\n";
    ss << "    maxSamples: " << config.max_samples << ",\n";
    ss << "    samples: " << config.samples << ",\n";
    ss << "    sampleBuffers: " << config.sample_buffers << ",\n";
    ss << "    maxAnisotropy: " << config.max_anisotropy << ",\n";
    ss << "    max3DTextureSize: " << config.max_3d_texture_size << ",\n";
    ss << "    maxArrayTextureLayers: " << config.max_array_texture_layers << ",\n";
    ss << "    maxColorAttachments: " << config.max_color_attachments << ",\n";
    ss << "    maxDrawBuffers: " << config.max_draw_buffers << ",\n";
    ss << "    maxUniformBufferBindings: " << config.max_uniform_buffer_bindings << ",\n";
    ss << "    maxUniformBlockSize: " << config.max_uniform_block_size << ",\n";
    ss << "    maxCombinedUniformBlocks: " << config.max_combined_uniform_blocks << ",\n";
    ss << "    extensions: " << VectorToJSArray(config.extensions) << ",\n";
    ss << "    extensions2: " << VectorToJSArray(config.extensions2) << ",\n";
    ss << "    contextAttributes: {\n";
    ss << "      antialias: " << (config.antialias ? "true" : "false") << ",\n";
    ss << "      desynchronized: " << (config.desynchronized ? "true" : "false") << ",\n";
    ss << "      powerPreference: \"" << EscapeJS(config.power_preference) << "\"\n";
    ss << "    },\n";
    
    // Precision formats
    ss << "    vertexHighFloat: [" << config.vertex_high_float.range_min << ", " 
       << config.vertex_high_float.range_max << ", " << config.vertex_high_float.precision << "],\n";
    ss << "    vertexMediumFloat: [" << config.vertex_medium_float.range_min << ", " 
       << config.vertex_medium_float.range_max << ", " << config.vertex_medium_float.precision << "],\n";
    ss << "    vertexLowFloat: [" << config.vertex_low_float.range_min << ", " 
       << config.vertex_low_float.range_max << ", " << config.vertex_low_float.precision << "],\n";
    ss << "    vertexHighInt: [" << config.vertex_high_int.range_min << ", " 
       << config.vertex_high_int.range_max << ", " << config.vertex_high_int.precision << "],\n";
    ss << "    vertexMediumInt: [" << config.vertex_medium_int.range_min << ", " 
       << config.vertex_medium_int.range_max << ", " << config.vertex_medium_int.precision << "],\n";
    ss << "    vertexLowInt: [" << config.vertex_low_int.range_min << ", " 
       << config.vertex_low_int.range_max << ", " << config.vertex_low_int.precision << "],\n";
    ss << "    fragmentHighFloat: [" << config.fragment_high_float.range_min << ", " 
       << config.fragment_high_float.range_max << ", " << config.fragment_high_float.precision << "],\n";
    ss << "    fragmentMediumFloat: [" << config.fragment_medium_float.range_min << ", " 
       << config.fragment_medium_float.range_max << ", " << config.fragment_medium_float.precision << "],\n";
    ss << "    fragmentLowFloat: [" << config.fragment_low_float.range_min << ", " 
       << config.fragment_low_float.range_max << ", " << config.fragment_low_float.precision << "],\n";
    ss << "    fragmentHighInt: [" << config.fragment_high_int.range_min << ", " 
       << config.fragment_high_int.range_max << ", " << config.fragment_high_int.precision << "],\n";
    ss << "    fragmentMediumInt: [" << config.fragment_medium_int.range_min << ", " 
       << config.fragment_medium_int.range_max << ", " << config.fragment_medium_int.precision << "],\n";
    ss << "    fragmentLowInt: [" << config.fragment_low_int.range_min << ", " 
       << config.fragment_low_int.range_max << ", " << config.fragment_low_int.precision << "]\n";
    ss << "  };\n\n";
    
    // Generate hooks
    ss << GenerateGetParameterHook(config);
    ss << GenerateShaderPrecisionHook(config);
    ss << GenerateExtensionsHook(config);
    ss << GenerateContextAttributesHook(config);
    ss << GenerateReadPixelsHook();
    ss << GenerateGetContextHook(config);
    
    ss << "})();\n";
    
    return ss.str();
}

std::string WebGLSpoof::GenerateGetParameterHook(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // getParameter HOOK
  // Spoof all WebGL parameters to match VM profile
  // ============================================================
  
  const patchGetParameter = (proto, isWebGL2) => {
    const origGetParameter = proto.getParameter;
    const WebGLRC = window.WebGLRenderingContext;
    const WebGL2RC = window.WebGL2RenderingContext;
    
    const getParameterProxy = createNativeProxy(origGetParameter, (target, thisArg, args) => {
      // Validate 'this' is a proper WebGL context
      if (WebGLRC && !(thisArg instanceof WebGLRC) && 
          !(WebGL2RC && thisArg instanceof WebGL2RC)) {
        throw new TypeError("Failed to execute 'getParameter' on 'WebGLRenderingContext': Illegal invocation");
      }
      
      const pname = args[0];
      
      // Handle UNMASKED queries (from debug extension)
      if (pname === 0x9245 || pname === 0x9246) {
        return (pname === 0x9245) ? __webglConfig.unmaskedVendor : __webglConfig.unmaskedRenderer;
      }
      
      switch (pname) {
        case 0x1F00: return __webglConfig.vendor;           // VENDOR
        case 0x1F01: return __webglConfig.renderer;         // RENDERER
        case 0x1F02: return isWebGL2 ? __webglConfig.version2 : __webglConfig.version; // VERSION
        case 0x8B8C: return isWebGL2 ? __webglConfig.shadingLanguageV2 : __webglConfig.shadingLanguage; // SHADING_LANGUAGE_VERSION
        case 0x0D33: return __webglConfig.maxTextureSize;   // MAX_TEXTURE_SIZE
        case 0x851C: return __webglConfig.maxCubeMapTextureSize; // MAX_CUBE_MAP_TEXTURE_SIZE
        case 0x84E8: return __webglConfig.maxRenderBufferSize;   // MAX_RENDERBUFFER_SIZE
        case 0x8869: return __webglConfig.maxVertexAttribs;      // MAX_VERTEX_ATTRIBS
        case 0x8DFB: return __webglConfig.maxVertexUniformVectors; // MAX_VERTEX_UNIFORM_VECTORS
        case 0x8B4C: return __webglConfig.maxVertexTextureUnits;   // MAX_VERTEX_TEXTURE_IMAGE_UNITS
        case 0x8DFC: return __webglConfig.maxVaryingVectors;       // MAX_VARYING_VECTORS
        case 0x8DFD: return __webglConfig.maxFragmentUniformVectors; // MAX_FRAGMENT_UNIFORM_VECTORS
        case 0x8872: return __webglConfig.maxTextureUnits;         // MAX_TEXTURE_IMAGE_UNITS
        case 0x8B4D: return __webglConfig.maxCombinedTextureUnits; // MAX_COMBINED_TEXTURE_IMAGE_UNITS
        case 0x0D3A: return new Int32Array(__webglConfig.maxViewportDims); // MAX_VIEWPORT_DIMS
        case 0x846E: return new Float32Array(__webglConfig.aliasedLineWidthRange); // ALIASED_LINE_WIDTH_RANGE
        case 0x846D: return new Float32Array(__webglConfig.aliasedPointSizeRange); // ALIASED_POINT_SIZE_RANGE
        case 0x8D57: return __webglConfig.maxSamples;       // MAX_SAMPLES
        case 0x80A9: return __webglConfig.samples;          // SAMPLES
        case 0x80A8: return __webglConfig.sampleBuffers;    // SAMPLE_BUFFERS
        
        // WebGL2-specific parameters
        case 0x8073: return __webglConfig.max3DTextureSize;        // MAX_3D_TEXTURE_SIZE
        case 0x88FF: return __webglConfig.maxArrayTextureLayers;   // MAX_ARRAY_TEXTURE_LAYERS
        case 0x8CDF: return __webglConfig.maxColorAttachments;     // MAX_COLOR_ATTACHMENTS
        case 0x8824: return __webglConfig.maxDrawBuffers;          // MAX_DRAW_BUFFERS
        case 0x8A2F: return __webglConfig.maxUniformBufferBindings; // MAX_UNIFORM_BUFFER_BINDINGS
        case 0x8A30: return __webglConfig.maxUniformBlockSize;     // MAX_UNIFORM_BLOCK_SIZE
        case 0x8A2E: return __webglConfig.maxCombinedUniformBlocks; // MAX_COMBINED_UNIFORM_BLOCKS
      }
      
      return origGetParameter.apply(thisArg, args);
    }, 'getParameter');
    
    Object.defineProperty(proto, 'getParameter', {
      value: getParameterProxy, writable: true, configurable: true
    });
  };
  
  // Patch WebGLRenderingContext
  if (typeof WebGLRenderingContext !== 'undefined') {
    patchGetParameter(WebGLRenderingContext.prototype, false);
  }
  
  // Patch WebGL2RenderingContext
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patchGetParameter(WebGL2RenderingContext.prototype, true);
  }
)JS";
    
    return ss.str();
}

std::string WebGLSpoof::GenerateShaderPrecisionHook(const Config& config) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // getShaderPrecisionFormat HOOK
  // Uses per-context seed to vary precision values for unique fingerprint
  // ============================================================

  // Get WebGL seed from window (set by owl_virtual_machine.cc)
  const getWebGLSeedForPrecision = () => {
    return window[_seedSym] || 0n;
  };

  // Apply small deterministic variation to precision based on seed
  // The variation must be small and realistic (±1 at most)
  const varyPrecision = (prec, seed, offset) => {
    if (!seed) return prec;

    // Extract bits for this offset
    const bits = Number((seed >> BigInt(offset)) & 0xFFn);

    // Vary only the rangeMin/rangeMax slightly (common variance in real hardware)
    // Some GPUs report 127/127/23, others 126/127/23, etc.
    const rangeMin = prec[0];
    const rangeMax = prec[1];
    const precision = prec[2];

    // Decide variation based on seed bits
    // Only vary medium/low precision to keep it realistic
    // High precision (127/127/23) is consistent across most desktop GPUs
    let variedMin = rangeMin;
    let variedMax = rangeMax;

    // For medium precision types, small variance is realistic
    if (rangeMin < 127) {
      variedMin = rangeMin + ((bits % 3) - 1); // -1, 0, or +1
      variedMin = Math.max(1, Math.min(127, variedMin));
    }
    if (rangeMax < 127) {
      variedMax = rangeMax + (((bits >> 2) % 3) - 1);
      variedMax = Math.max(1, Math.min(127, variedMax));
    }

    return [variedMin, variedMax, precision];
  };

  const patchShaderPrecision = (proto) => {
    const origGetShaderPrecisionFormat = proto.getShaderPrecisionFormat;
    const WebGLRC = window.WebGLRenderingContext;
    const WebGL2RC = window.WebGL2RenderingContext;

    const HIGH_FLOAT = 0x8DF2;
    const MEDIUM_FLOAT = 0x8DF1;
    const LOW_FLOAT = 0x8DF0;
    const HIGH_INT = 0x8DF5;
    const MEDIUM_INT = 0x8DF4;
    const LOW_INT = 0x8DF3;
    const VERTEX_SHADER = 0x8B31;
    const FRAGMENT_SHADER = 0x8B30;

    const getShaderPrecisionFormatProxy = createNativeProxy(origGetShaderPrecisionFormat, (target, thisArg, args) => {
      if (WebGLRC && !(thisArg instanceof WebGLRC) &&
          !(WebGL2RC && thisArg instanceof WebGL2RC)) {
        throw new TypeError("Failed to execute 'getShaderPrecisionFormat' on 'WebGLRenderingContext': Illegal invocation");
      }

      const [shaderType, precisionType] = args;
      let prec;
      let seedOffset = 0;

      if (shaderType === VERTEX_SHADER) {
        if (precisionType === HIGH_FLOAT) { prec = __webglConfig.vertexHighFloat; seedOffset = 0; }
        else if (precisionType === MEDIUM_FLOAT) { prec = __webglConfig.vertexMediumFloat; seedOffset = 8; }
        else if (precisionType === LOW_FLOAT) { prec = __webglConfig.vertexLowFloat; seedOffset = 16; }
        else if (precisionType === HIGH_INT) { prec = __webglConfig.vertexHighInt; seedOffset = 24; }
        else if (precisionType === MEDIUM_INT) { prec = __webglConfig.vertexMediumInt; seedOffset = 32; }
        else if (precisionType === LOW_INT) { prec = __webglConfig.vertexLowInt; seedOffset = 40; }
        else { prec = __webglConfig.vertexHighFloat; seedOffset = 0; }
      } else {
        if (precisionType === HIGH_FLOAT) { prec = __webglConfig.fragmentHighFloat; seedOffset = 48; }
        else if (precisionType === MEDIUM_FLOAT) { prec = __webglConfig.fragmentMediumFloat; seedOffset = 56; }
        else if (precisionType === LOW_FLOAT) { prec = __webglConfig.fragmentLowFloat; seedOffset = 4; }
        else if (precisionType === HIGH_INT) { prec = __webglConfig.fragmentHighInt; seedOffset = 12; }
        else if (precisionType === MEDIUM_INT) { prec = __webglConfig.fragmentMediumInt; seedOffset = 20; }
        else if (precisionType === LOW_INT) { prec = __webglConfig.fragmentLowInt; seedOffset = 28; }
        else { prec = __webglConfig.fragmentHighFloat; seedOffset = 48; }
      }

      // Apply per-context variation to medium/low precision types
      const seed = getWebGLSeedForPrecision();
      const variedPrec = varyPrecision(prec, seed, seedOffset);

      // Create proper WebGLShaderPrecisionFormat-like object
      const result = (typeof WebGLShaderPrecisionFormat !== 'undefined')
        ? Object.create(WebGLShaderPrecisionFormat.prototype)
        : {};

      Object.defineProperties(result, {
        rangeMin: { value: variedPrec[0], writable: false },
        rangeMax: { value: variedPrec[1], writable: false },
        precision: { value: variedPrec[2], writable: false }
      });

      return result;
    }, 'getShaderPrecisionFormat');

    Object.defineProperty(proto, 'getShaderPrecisionFormat', {
      value: getShaderPrecisionFormatProxy, writable: true, configurable: true
    });
  };

  if (typeof WebGLRenderingContext !== 'undefined') {
    patchShaderPrecision(WebGLRenderingContext.prototype);
  }
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patchShaderPrecision(WebGL2RenderingContext.prototype);
  }
)JS";

    return ss.str();
}

std::string WebGLSpoof::GenerateExtensionsHook(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // getSupportedExtensions / getExtension HOOKS
  // ============================================================
  
  // Cache for extension objects per context
  const _extensionCache = new WeakMap();
  
  const patchExtensions = (proto, isWebGL2) => {
    const origGetSupportedExtensions = proto.getSupportedExtensions;
    const origGetExtension = proto.getExtension;
    
    const extensions = isWebGL2 ? __webglConfig.extensions2 : __webglConfig.extensions;
    
    const getSupportedExtensionsProxy = createNativeProxy(origGetSupportedExtensions, (target, thisArg, args) => {
      return [...extensions];
    }, 'getSupportedExtensions');
    
    const getExtensionProxy = createNativeProxy(origGetExtension, (target, thisArg, args) => {
      const name = args[0];
      
      // Check if extension is in our supported list
      if (!extensions.includes(name)) {
        return null;
      }
      
      // Get or create cached extension object
      let cache = _extensionCache.get(thisArg);
      if (!cache) {
        cache = {};
        _extensionCache.set(thisArg, cache);
      }
      
      if (cache[name]) {
        return cache[name];
      }
      
      // Get real extension and cache it
      const ext = origGetExtension.call(thisArg, name);
      if (ext) {
        cache[name] = ext;
      }
      return ext;
    }, 'getExtension');
    
    Object.defineProperty(proto, 'getSupportedExtensions', {
      value: getSupportedExtensionsProxy, writable: true, configurable: true
    });
    Object.defineProperty(proto, 'getExtension', {
      value: getExtensionProxy, writable: true, configurable: true
    });
  };
  
  if (typeof WebGLRenderingContext !== 'undefined') {
    patchExtensions(WebGLRenderingContext.prototype, false);
  }
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patchExtensions(WebGL2RenderingContext.prototype, true);
  }
)JS";
    
    return ss.str();
}

std::string WebGLSpoof::GenerateContextAttributesHook(const Config& config) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // getContextAttributes HOOK
  // Uses per-context seed to vary attributes for unique fingerprint
  // ============================================================

  // Get WebGL seed from window (set by owl_virtual_machine.cc)
  const getWebGLSeed = () => {
    return window[_seedSym] || 0n;
  };

  // Deterministic boolean based on seed
  const seedToBool = (seed, offset) => {
    const mixed = Number((seed >> BigInt(offset)) & 0xFFn);
    return (mixed % 2) === 0;
  };

  const patchContextAttributes = (proto) => {
    const origGetContextAttributes = proto.getContextAttributes;

    const getContextAttributesProxy = createNativeProxy(origGetContextAttributes, (target, thisArg, args) => {
      const orig = origGetContextAttributes.call(thisArg);
      if (!orig) return orig;

      // Use per-context seed to vary some attributes
      const seed = getWebGLSeed();

      // Vary desynchronized based on seed (this affects the hash)
      // Most browsers default to false, but some contexts can have true
      const variedDesync = seed ? seedToBool(seed, 8) : __webglConfig.contextAttributes.desynchronized;

      // powerPreference can vary too - use seed to select
      const powerPrefs = ['default', 'high-performance', 'low-power'];
      const powerIdx = seed ? Number((seed >> 16n) & 0xFFn) % 3 : 0;
      const variedPower = seed ? powerPrefs[powerIdx] : __webglConfig.contextAttributes.powerPreference;

      return {
        ...orig,
        antialias: __webglConfig.contextAttributes.antialias,
        desynchronized: variedDesync,
        powerPreference: variedPower
      };
    }, 'getContextAttributes');

    Object.defineProperty(proto, 'getContextAttributes', {
      value: getContextAttributesProxy, writable: true, configurable: true
    });
  };

  if (typeof WebGLRenderingContext !== 'undefined') {
    patchContextAttributes(WebGLRenderingContext.prototype);
  }
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patchContextAttributes(WebGL2RenderingContext.prototype);
  }
)JS";

    return ss.str();
}

std::string WebGLSpoof::GenerateReadPixelsHook() {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // readPixels HOOK (Noise Injection)
  // Context-aware seed lookup via canvas.ownerDocument.defaultView
  // ============================================================
  
  const patchReadPixels = (proto) => {
    const origReadPixels = proto.readPixels;
    
    const getSeedFromContext = (glCtx) => {
      try {
        const canvas = glCtx.canvas;
        const win = canvas?.ownerDocument?.defaultView || window;
        return win[_seedSym] || 0n;
      } catch (e) {
        return window[_seedSym] || 0n;
      }
    };
    
    const applyWebGLNoise = (pixels, seed) => {
      if (!seed || seed === 0n) return;
      
      let state = Number(seed & 0xFFFFFFFFn) || 0x12345678;
      const nextRand = () => {
        state ^= state << 13;
        state ^= state >>> 17;
        state ^= state << 5;
        return (state >>> 0) / 0xFFFFFFFF;
      };
      
      for (let i = 0; i < 10; i++) nextRand();
      
      const len = pixels.length;
      for (let i = 0; i < len; i += 4) {
        const alpha = pixels[i + 3];
        if (alpha === 0) continue;
        
        const noise = (nextRand() < 0.15) ? (nextRand() < 0.5 ? -1 : 1) : 0;
        if (noise !== 0) {
          pixels[i] = Math.max(0, Math.min(255, pixels[i] + noise));
          pixels[i+1] = Math.max(0, Math.min(255, pixels[i+1] + noise));
          pixels[i+2] = Math.max(0, Math.min(255, pixels[i+2] + noise));
        }
      }
    };
    
    const readPixelsProxy = createNativeProxy(origReadPixels, (target, thisArg, args) => {
      const result = origReadPixels.apply(thisArg, args);
      
      try {
        const pixels = args[6]; // pixels buffer is 7th argument
        if (pixels && pixels.length) {
          const seed = getSeedFromContext(thisArg);
          applyWebGLNoise(pixels, seed);
        }
      } catch (e) {}
      
      return result;
    }, 'readPixels');
    
    Object.defineProperty(proto, 'readPixels', {
      value: readPixelsProxy, writable: true, configurable: true
    });
  };
  
  if (typeof WebGLRenderingContext !== 'undefined') {
    patchReadPixels(WebGLRenderingContext.prototype);
  }
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patchReadPixels(WebGL2RenderingContext.prototype);
  }
)JS";
    
    return ss.str();
}

std::string WebGLSpoof::GenerateGetContextHook(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // HTMLCanvasElement.getContext HOOK
  // Track contexts and wrap instance methods for toString masking
  // ============================================================
  
  const _origGetContext = HTMLCanvasElement.prototype.getContext;
  const _2DCanvases = new WeakSet();
  
  const getContextProxy = createNativeProxy(_origGetContext, (target, thisArg, args) => {
    const contextType = args[0];
    const result = _origGetContext.apply(thisArg, args);
    
    // Track canvases that have 2D context
    if (contextType === '2d') {
      _2DCanvases.add(thisArg);
    }
    
    // For WebGL contexts, wrap methods for toString masking
    if (result && (contextType === 'webgl' || contextType === 'webgl2' || contextType === 'experimental-webgl')) {
      // Mark this canvas as having a WebGL context
      try {
        thisArg._hasWebGL = true;
      } catch (e) {}
    }
    
    return result;
  }, 'getContext');
  
  Object.defineProperty(HTMLCanvasElement.prototype, 'getContext', {
    value: getContextProxy, writable: true, configurable: true
  });
)JS";
    
    return ss.str();
}

std::string WebGLSpoof::EscapeJS(const std::string& str) {
    std::stringstream ss;
    for (char c : str) {
        switch (c) {
            case '\\': ss << "\\\\"; break;
            case '"': ss << "\\\""; break;
            case '\'': ss << "\\'"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << c; break;
        }
    }
    return ss.str();
}

std::string WebGLSpoof::VectorToJSArray(const std::vector<std::string>& vec) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "\"" << EscapeJS(vec[i]) << "\"";
    }
    ss << "]";
    return ss.str();
}

} // namespace spoofs
} // namespace owl
