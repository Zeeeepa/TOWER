#include "stealth/spoofs/audio_spoof.h"
#include "util/logger.h"
#include <sstream>
#include <iomanip>

namespace owl {
namespace spoofs {

AudioSpoof::Config AudioSpoof::Config::FromVM(const VirtualMachine& vm) {
    Config config;
    config.sample_rate = vm.audio.sample_rate;
    config.base_latency = vm.audio.base_latency;
    config.output_latency = vm.audio.output_latency;
    config.max_channel_count = vm.audio.max_channel_count;
    config.number_of_inputs = vm.audio.number_of_inputs;
    config.number_of_outputs = vm.audio.number_of_outputs;
    config.channel_count = vm.audio.channel_count;
    config.channel_count_mode = vm.audio.channel_count_mode;
    config.channel_interpretation = vm.audio.channel_interpretation;
    config.seed = vm.audio.audio_hash_seed;
    return config;
}

bool AudioSpoof::Inject(CefRefPtr<CefFrame> frame, const Config& config) {
    if (!frame) {
        LOG_DEBUG("AudioSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(config);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("AudioSpoof", "Injected audio spoofs into frame");
    return true;
}

std::string AudioSpoof::GenerateScript(const Config& config) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('audio')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";
    
    // Configuration
    ss << "  // Audio configuration\n";
    ss << "  const __audioConfig = {\n";
    ss << "    sampleRate: " << config.sample_rate << ",\n";
    ss << "    baseLatency: " << config.base_latency << ",\n";
    ss << "    outputLatency: " << config.output_latency << ",\n";
    ss << "    maxChannelCount: " << config.max_channel_count << ",\n";
    ss << "    numberOfInputs: " << config.number_of_inputs << ",\n";
    ss << "    numberOfOutputs: " << config.number_of_outputs << ",\n";
    ss << "    channelCount: " << config.channel_count << ",\n";
    ss << "    channelCountMode: \"" << config.channel_count_mode << "\",\n";
    ss << "    channelInterpretation: \"" << config.channel_interpretation << "\"\n";
    ss << "  };\n\n";
    
    ss << GenerateAudioContextHooks(config);
    ss << GenerateOfflineAudioContextHooks(config);
    ss << GenerateDestinationHooks(config);
    ss << GenerateAudioFingerprintHooks(config);

    ss << "})();\n";
    
    return ss.str();
}

std::string AudioSpoof::GenerateAudioContextHooks(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // AudioContext HOOKS
  // ============================================================
  
  if (typeof AudioContext !== 'undefined') {
    const AudioContextProto = AudioContext.prototype;
    const BaseAudioContextProto = (typeof BaseAudioContext !== 'undefined') ? BaseAudioContext.prototype : null;
    
    // Create property descriptors
    const sampleRateDescriptor = {
      get: function() { return __audioConfig.sampleRate; },
      configurable: true,
      enumerable: true
    };
    
    const baseLatencyDescriptor = {
      get: function() { return __audioConfig.baseLatency; },
      configurable: true,
      enumerable: true
    };
    
    const outputLatencyDescriptor = {
      get: function() { return __audioConfig.outputLatency; },
      configurable: true,
      enumerable: true
    };
    
    // Register getters for toString masking
    window[_owl].registerNative(sampleRateDescriptor.get, 'function get sampleRate() { [native code] }');
    window[_owl].registerNative(baseLatencyDescriptor.get, 'function get baseLatency() { [native code] }');
    window[_owl].registerNative(outputLatencyDescriptor.get, 'function get outputLatency() { [native code] }');
    
    // Apply to AudioContext.prototype
    try {
      Object.defineProperty(AudioContextProto, 'sampleRate', sampleRateDescriptor);
    } catch (e) {}
    try {
      Object.defineProperty(AudioContextProto, 'baseLatency', baseLatencyDescriptor);
    } catch (e) {}
    try {
      Object.defineProperty(AudioContextProto, 'outputLatency', outputLatencyDescriptor);
    } catch (e) {}
    
    // Apply to BaseAudioContext.prototype if exists
    if (BaseAudioContextProto) {
      try {
        Object.defineProperty(BaseAudioContextProto, 'sampleRate', sampleRateDescriptor);
      } catch (e) {}
      try {
        Object.defineProperty(BaseAudioContextProto, 'baseLatency', baseLatencyDescriptor);
      } catch (e) {}
      try {
        Object.defineProperty(BaseAudioContextProto, 'outputLatency', outputLatencyDescriptor);
      } catch (e) {}
    }
    
    // Wrap AudioContext constructor
    const OrigAudioContext = AudioContext;
    window.AudioContext = function AudioContext(options) {
      const ctx = new OrigAudioContext(options);
      
      // Ensure destination has correct properties
      try {
        Object.defineProperty(ctx.destination, 'maxChannelCount', {
          get: () => __audioConfig.maxChannelCount,
          configurable: true
        });
        Object.defineProperty(ctx.destination, 'numberOfInputs', {
          get: () => __audioConfig.numberOfInputs,
          configurable: true
        });
        Object.defineProperty(ctx.destination, 'numberOfOutputs', {
          get: () => __audioConfig.numberOfOutputs,
          configurable: true
        });
        Object.defineProperty(ctx.destination, 'channelCount', {
          get: () => __audioConfig.channelCount,
          configurable: true
        });
        Object.defineProperty(ctx.destination, 'channelCountMode', {
          get: () => __audioConfig.channelCountMode,
          configurable: true
        });
        Object.defineProperty(ctx.destination, 'channelInterpretation', {
          get: () => __audioConfig.channelInterpretation,
          configurable: true
        });
      } catch (e) {}
      
      return ctx;
    };
    
    window.AudioContext.prototype = AudioContextProto;
    Object.defineProperty(window.AudioContext, 'name', { value: 'AudioContext' });
    window[_owl].registerNative(window.AudioContext, 'function AudioContext() { [native code] }');
    
    try {
      Object.setPrototypeOf(window.AudioContext, OrigAudioContext);
    } catch (e) {}
    
    // Intercept Object.getOwnPropertyDescriptor for audio properties
    const origGetOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
    Object.getOwnPropertyDescriptor = function(obj, prop) {
      if ((obj === AudioContextProto || obj === BaseAudioContextProto) &&
          ['sampleRate', 'baseLatency', 'outputLatency'].includes(prop)) {
        const desc = origGetOwnPropertyDescriptor.call(Object, obj, prop);
        if (desc && desc.get) {
          window[_owl].registerNative(desc.get, `function get ${prop}() { [native code] }`);
        }
        return desc;
      }
      return origGetOwnPropertyDescriptor.call(Object, obj, prop);
    };
    window[_owl].registerNative(Object.getOwnPropertyDescriptor, 'function getOwnPropertyDescriptor() { [native code] }');
  }
)JS";
    
    return ss.str();
}

std::string AudioSpoof::GenerateOfflineAudioContextHooks(const Config& config) {
    std::stringstream ss;
    
    ss << R"JS(
  // ============================================================
  // OfflineAudioContext HOOKS
  // Normalize sample rate for consistent fingerprints
  // ============================================================
  
  if (typeof OfflineAudioContext !== 'undefined') {
    const OfflineAudioContextProto = OfflineAudioContext.prototype;
    
    // Apply sampleRate descriptor
    try {
      Object.defineProperty(OfflineAudioContextProto, 'sampleRate', {
        get: function() { return __audioConfig.sampleRate; },
        configurable: true,
        enumerable: true
      });
    } catch (e) {}
    
    // Wrap OfflineAudioContext constructor to normalize sample rate
    const OrigOfflineAudioContext = OfflineAudioContext;
    window.OfflineAudioContext = function OfflineAudioContext(options) {
      let ctx;
      if (typeof options === 'object') {
        // Options object format
        ctx = new OrigOfflineAudioContext({
          ...options,
          sampleRate: __audioConfig.sampleRate
        });
      } else {
        // Legacy format: channels, length, sampleRate
        ctx = new OrigOfflineAudioContext(arguments[0], arguments[1], __audioConfig.sampleRate);
      }
      return ctx;
    };
    
    window.OfflineAudioContext.prototype = OfflineAudioContextProto;
    Object.defineProperty(window.OfflineAudioContext, 'name', { value: 'OfflineAudioContext' });
    window[_owl].registerNative(window.OfflineAudioContext, 'function OfflineAudioContext() { [native code] }');
  }
)JS";
    
    return ss.str();
}

std::string AudioSpoof::GenerateDestinationHooks(const Config& config) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // AudioDestinationNode HOOKS
  // ============================================================

  if (typeof AudioDestinationNode !== 'undefined') {
    const AudioDestinationNodeProto = AudioDestinationNode.prototype;

    const destProps = {
      maxChannelCount: __audioConfig.maxChannelCount,
      numberOfInputs: __audioConfig.numberOfInputs,
      numberOfOutputs: __audioConfig.numberOfOutputs,
      channelCount: __audioConfig.channelCount,
      channelCountMode: __audioConfig.channelCountMode,
      channelInterpretation: __audioConfig.channelInterpretation
    };

    for (const [prop, value] of Object.entries(destProps)) {
      try {
        const origDesc = Object.getOwnPropertyDescriptor(AudioDestinationNodeProto, prop);
        if (origDesc && origDesc.get) {
          const proxyGetter = createNativeProxy(origDesc.get, (target, thisArg, args) => {
            return value;
          }, `get ${prop}`);

          Object.defineProperty(AudioDestinationNodeProto, prop, {
            get: proxyGetter,
            configurable: true,
            enumerable: true
          });
        }
      } catch (e) {}
    }
  }
)JS";

    return ss.str();
}

std::string AudioSpoof::GenerateAudioFingerprintHooks(const Config& config) {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // AUDIO FINGERPRINT HOOKS
  // Hook getChannelData to apply deterministic noise to audio output
  // This changes the audio fingerprint hash per context
  // ============================================================

  const _audioSeedSym = Symbol.for('__owl_audio_seed__');

  // Get audio seed from context (set by owl_virtual_machine.cc)
  const getAudioSeed = () => {
    return window[_audioSeedSym] || window[Symbol.for('__owl_canvas_seed__')] || 0n;
  };

  // Deterministic noise generator using xorshift
  const createNoiseGenerator = (seed) => {
    let state = Number(seed & 0xFFFFFFFFn) || 0x12345678;
    return () => {
      state ^= state << 13;
      state ^= state >>> 17;
      state ^= state << 5;
      return ((state >>> 0) / 0xFFFFFFFF) * 2 - 1; // Range: -1 to 1
    };
  };

  // Track which AudioBuffers have been noised (using WeakMap to avoid memory leaks)
  const _noisedBuffers = new WeakMap();

  if (typeof AudioBuffer !== 'undefined') {
    const AudioBufferProto = AudioBuffer.prototype;
    const origGetChannelData = AudioBufferProto.getChannelData;

    const getChannelDataProxy = createNativeProxy(origGetChannelData, (target, thisArg, args) => {
      const data = origGetChannelData.apply(thisArg, args);

      // Only apply noise once per buffer+channel combination
      const channel = args[0] || 0;
      let channelSet = _noisedBuffers.get(thisArg);
      if (!channelSet) {
        channelSet = new Set();
        _noisedBuffers.set(thisArg, channelSet);
      }

      if (channelSet.has(channel)) {
        return data; // Already noised
      }
      channelSet.add(channel);

      // Apply very small deterministic noise to audio samples
      // The noise must be small enough to not affect audio quality
      // but large enough to change the fingerprint hash
      const seed = getAudioSeed();
      if (!seed || seed === 0n) return data;

      const nextRand = createNoiseGenerator(seed + BigInt(channel));

      // Warm up the generator
      for (let i = 0; i < 10; i++) nextRand();

      // Apply tiny noise to a subset of samples
      // fingerprint.com sums samples at specific positions (4500-4999)
      // We apply noise throughout to ensure hash changes
      const len = data.length;
      const noiseIntensity = 0.0000001; // Very small - won't affect audio quality

      for (let i = 0; i < len; i++) {
        // Apply noise to every ~10th sample for efficiency
        if ((i % 10) === 0) {
          data[i] += nextRand() * noiseIntensity;
        }
      }

      return data;
    }, 'getChannelData');

    Object.defineProperty(AudioBufferProto, 'getChannelData', {
      value: getChannelDataProxy, writable: true, configurable: true
    });
  }

  // Also hook copyFromChannel for completeness
  if (typeof AudioBuffer !== 'undefined' && AudioBuffer.prototype.copyFromChannel) {
    const AudioBufferProto = AudioBuffer.prototype;
    const origCopyFromChannel = AudioBufferProto.copyFromChannel;

    const copyFromChannelProxy = createNativeProxy(origCopyFromChannel, (target, thisArg, args) => {
      const result = origCopyFromChannel.apply(thisArg, args);

      const destination = args[0];
      const channel = args[1] || 0;

      // Apply same noise pattern as getChannelData
      const seed = getAudioSeed();
      if (!seed || seed === 0n || !destination) return result;

      const nextRand = createNoiseGenerator(seed + BigInt(channel));
      for (let i = 0; i < 10; i++) nextRand();

      const len = destination.length;
      const noiseIntensity = 0.0000001;

      for (let i = 0; i < len; i++) {
        if ((i % 10) === 0) {
          destination[i] += nextRand() * noiseIntensity;
        }
      }

      return result;
    }, 'copyFromChannel');

    Object.defineProperty(AudioBufferProto, 'copyFromChannel', {
      value: copyFromChannelProxy, writable: true, configurable: true
    });
  }
)JS";

    return ss.str();
}

} // namespace spoofs
} // namespace owl
