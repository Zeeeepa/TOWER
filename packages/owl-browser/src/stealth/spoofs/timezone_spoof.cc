#include "stealth/spoofs/timezone_spoof.h"
#include "util/logger.h"
#include <sstream>

namespace owl {
namespace spoofs {

TimezoneSpoof::Config TimezoneSpoof::Config::FromVM(const VirtualMachine& vm) {
    Config config;
    config.iana_name = vm.timezone.iana_name;
    config.offset_minutes = vm.timezone.offset_minutes;
    config.has_dst = vm.timezone.has_dst;
    return config;
}

TimezoneSpoof::Config TimezoneSpoof::Config::FromIANA(const std::string& iana_name) {
    Config config;
    config.iana_name = iana_name;
    // Offset will be calculated dynamically in JavaScript
    config.offset_minutes = 0;
    config.has_dst = true;
    return config;
}

bool TimezoneSpoof::Inject(CefRefPtr<CefFrame> frame, const Config& config) {
    if (!frame) {
        LOG_DEBUG("TimezoneSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(config);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("TimezoneSpoof", "Injected timezone spoofs for: " + config.iana_name);
    return true;
}

bool TimezoneSpoof::Inject(CefRefPtr<CefFrame> frame, const std::string& iana_name) {
    if (!frame) {
        LOG_DEBUG("TimezoneSpoof", "Inject: null frame");
        return false;
    }
    
    std::string script = GenerateScript(iana_name);
    frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    LOG_DEBUG("TimezoneSpoof", "Injected timezone spoofs for: " + iana_name);
    return true;
}

std::string TimezoneSpoof::GenerateScript(const Config& config) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('timezone')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";

    // Configuration
    ss << "  // Timezone configuration\n";
    ss << "  const __tzConfig = {\n";
    ss << "    name: \"" << EscapeJS(config.iana_name) << "\",\n";
    ss << "    offset: " << config.offset_minutes << "\n";
    ss << "  };\n\n";
    
    ss << GenerateDateHooks();
    ss << GenerateIntlHooks();
    
    ss << "})();\n";
    
    return ss.str();
}

std::string TimezoneSpoof::GenerateScript(const std::string& iana_name) {
    std::stringstream ss;
    
    ss << "(function() {\n";
    ss << "  'use strict';\n";
    ss << "  const _owl = Symbol.for('owl');\n\n";
    
    // Guard check
    ss << "  // Guard: Skip if already patched\n";
    ss << "  if (!window[_owl]?.checkGuard('timezone')) return;\n\n";
    
    // Get createNativeProxy from utilities
    ss << "  const createNativeProxy = window[_owl]?.createNativeProxy;\n";
    ss << "  if (!createNativeProxy) return;\n\n";

    // Dynamic offset calculation
    ss << "  // Calculate timezone offset dynamically\n";
    ss << "  const targetTimezone = \"" << EscapeJS(iana_name) << "\";\n";
    ss << R"JS(
  const getTimezoneOffset = (tz) => {
    try {
      const now = new Date();
      const utcTime = now.getTime() + (now.getTimezoneOffset() * 60000);
      
      // Format in target timezone to get the offset
      const targetFormatter = new Intl.DateTimeFormat('en-US', {
        timeZone: tz,
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false
      });
      
      const utcFormatter = new Intl.DateTimeFormat('en-US', {
        timeZone: 'UTC',
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false
      });
      
      const targetParts = targetFormatter.formatToParts(now);
      const utcParts = utcFormatter.formatToParts(now);
      
      const getPartValue = (parts, type) => parseInt(parts.find(p => p.type === type)?.value || '0');
      
      const targetDate = new Date(Date.UTC(
        getPartValue(targetParts, 'year'),
        getPartValue(targetParts, 'month') - 1,
        getPartValue(targetParts, 'day'),
        getPartValue(targetParts, 'hour'),
        getPartValue(targetParts, 'minute'),
        getPartValue(targetParts, 'second')
      ));
      
      const utcDate = new Date(Date.UTC(
        getPartValue(utcParts, 'year'),
        getPartValue(utcParts, 'month') - 1,
        getPartValue(utcParts, 'day'),
        getPartValue(utcParts, 'hour'),
        getPartValue(utcParts, 'minute'),
        getPartValue(utcParts, 'second')
      ));
      
      return Math.round((utcDate - targetDate) / 60000);
    } catch (e) {
      return 0; // Fallback to UTC
    }
  };
  
  const __tzConfig = {
    name: targetTimezone,
    offset: getTimezoneOffset(targetTimezone)
  };
)JS";
    
    ss << GenerateDateHooks();
    ss << GenerateIntlHooks();
    
    ss << "})();\n";
    
    return ss.str();
}

std::string TimezoneSpoof::GenerateDateHooks() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // DATE METHOD HOOKS
  // ============================================================

  // Store original Intl.DateTimeFormat for internal use
  const _OrigDateTimeFormat = Intl.DateTimeFormat;

  // Helper: Check if thisArg is a valid Date object
  // CRITICAL: Must handle null/undefined correctly for CreepJS "null conversion" test
  const isValidDate = (d) => d !== null && d !== undefined && d instanceof Date && !isNaN(d.getTime());

  // getTimezoneOffset
  const _origGetTimezoneOffset = Date.prototype.getTimezoneOffset;
  const getTimezoneOffsetProxy = createNativeProxy(_origGetTimezoneOffset, (target, thisArg, args) => {
    // Early validation - let native function throw correct error for invalid this
    if (thisArg === null || thisArg === undefined || !(thisArg instanceof Date)) {
      return _origGetTimezoneOffset.call(thisArg);
    }
    return __tzConfig.offset;
  }, 'getTimezoneOffset');

  Object.defineProperty(Date.prototype, 'getTimezoneOffset', {
    value: getTimezoneOffsetProxy, writable: true, configurable: true
  });

  // toString - includes timezone name
  const _origToString = Date.prototype.toString;
  const toStringProxy = createNativeProxy(_origToString, (target, thisArg, args) => {
    // Early validation - let native function throw correct error for invalid this
    if (thisArg === null || thisArg === undefined || !(thisArg instanceof Date)) {
      return _origToString.call(thisArg);
    }
    try {
      // Format: "Wed Dec 25 2024 10:30:00 GMT-0500 (Eastern Standard Time)"
      const dayFormatter = new _OrigDateTimeFormat('en-US', {
        timeZone: __tzConfig.name,
        weekday: 'short'
      });
      const monthFormatter = new _OrigDateTimeFormat('en-US', {
        timeZone: __tzConfig.name,
        month: 'short'
      });
      const dateFormatter = new _OrigDateTimeFormat('en-US', {
        timeZone: __tzConfig.name,
        day: '2-digit', year: 'numeric',
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false
      });

      const day = dayFormatter.format(thisArg);
      const month = monthFormatter.format(thisArg);
      const dateParts = dateFormatter.formatToParts(thisArg);

      const getValue = (type) => dateParts.find(p => p.type === type)?.value || '';

      // Calculate GMT offset string
      const offset = __tzConfig.offset;
      const sign = offset <= 0 ? '+' : '-';
      const absOffset = Math.abs(offset);
      const hours = String(Math.floor(absOffset / 60)).padStart(2, '0');
      const mins = String(absOffset % 60).padStart(2, '0');
      const gmtString = `GMT${sign}${hours}${mins}`;

      // Get long timezone name
      const longFormatter = new _OrigDateTimeFormat('en-US', {
        timeZone: __tzConfig.name,
        timeZoneName: 'long'
      });
      const longParts = longFormatter.formatToParts(thisArg);
      const tzName = longParts.find(p => p.type === 'timeZoneName')?.value || __tzConfig.name;

      return `${day} ${month} ${getValue('day')} ${getValue('year')} ${getValue('hour')}:${getValue('minute')}:${getValue('second')} ${gmtString} (${tzName})`;
    } catch (e) {
      return _origToString.call(thisArg);
    }
  }, 'toString');

  Object.defineProperty(Date.prototype, 'toString', {
    value: toStringProxy, writable: true, configurable: true
  });

  // toTimeString
  const _origToTimeString = Date.prototype.toTimeString;
  const toTimeStringProxy = createNativeProxy(_origToTimeString, (target, thisArg, args) => {
    // Early validation - let native function throw correct error for invalid this
    if (thisArg === null || thisArg === undefined || !(thisArg instanceof Date)) {
      return _origToTimeString.call(thisArg);
    }
    try {
      const formatter = new _OrigDateTimeFormat('en-US', {
        timeZone: __tzConfig.name,
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false
      });
      const time = formatter.format(thisArg);

      const offset = __tzConfig.offset;
      const sign = offset <= 0 ? '+' : '-';
      const absOffset = Math.abs(offset);
      const hours = String(Math.floor(absOffset / 60)).padStart(2, '0');
      const mins = String(absOffset % 60).padStart(2, '0');
      const gmtString = `GMT${sign}${hours}${mins}`;

      const longFormatter = new _OrigDateTimeFormat('en-US', {
        timeZone: __tzConfig.name,
        timeZoneName: 'long'
      });
      const longParts = longFormatter.formatToParts(thisArg);
      const tzName = longParts.find(p => p.type === 'timeZoneName')?.value || __tzConfig.name;

      return `${time} ${gmtString} (${tzName})`;
    } catch (e) {
      return _origToTimeString.call(thisArg);
    }
  }, 'toTimeString');

  Object.defineProperty(Date.prototype, 'toTimeString', {
    value: toTimeStringProxy, writable: true, configurable: true
  });

  // toLocaleString family - inject timezone
  const _origToLocaleString = Date.prototype.toLocaleString;
  const toLocaleStringProxy = createNativeProxy(_origToLocaleString, (target, thisArg, args) => {
    // Early validation - let native function throw correct error for invalid this
    if (thisArg === null || thisArg === undefined || !(thisArg instanceof Date)) {
      return _origToLocaleString.call(thisArg);
    }
    const [locales, options] = args;
    const newOptions = { ...options };
    if (!newOptions.timeZone) {
      newOptions.timeZone = __tzConfig.name;
    }
    return _origToLocaleString.call(thisArg, locales, newOptions);
  }, 'toLocaleString');

  Object.defineProperty(Date.prototype, 'toLocaleString', {
    value: toLocaleStringProxy, writable: true, configurable: true
  });
)JS";
    
    return ss.str();
}

std::string TimezoneSpoof::GenerateIntlHooks() {
    std::stringstream ss;

    ss << R"JS(
  // ============================================================
  // Intl.DateTimeFormat HOOKS
  // ============================================================

  // resolvedOptions - return spoofed timezone
  const _origResolvedOptions = _OrigDateTimeFormat.prototype.resolvedOptions;
  const resolvedOptionsProxy = createNativeProxy(_origResolvedOptions, (target, thisArg, args) => {
    // Early validation - let native function throw correct error for invalid this
    if (thisArg === null || thisArg === undefined) {
      return _origResolvedOptions.call(thisArg);
    }
    const options = _origResolvedOptions.call(thisArg);
    options.timeZone = __tzConfig.name;
    return options;
  }, 'resolvedOptions');
  
  Object.defineProperty(_OrigDateTimeFormat.prototype, 'resolvedOptions', {
    value: resolvedOptionsProxy, writable: true, configurable: true
  });
  
  // Wrap DateTimeFormat constructor to inject timezone
  const DateTimeFormatWrapper = function DateTimeFormat(locales, options) {
    const newOptions = options ? { ...options } : {};
    if (!newOptions.timeZone) {
      newOptions.timeZone = __tzConfig.name;
    }
    return new _OrigDateTimeFormat(locales, newOptions);
  };
  
  Object.setPrototypeOf(DateTimeFormatWrapper, _OrigDateTimeFormat);
  DateTimeFormatWrapper.prototype = _OrigDateTimeFormat.prototype;
  DateTimeFormatWrapper.supportedLocalesOf = _OrigDateTimeFormat.supportedLocalesOf;
  
  Object.defineProperty(DateTimeFormatWrapper, 'name', { value: 'DateTimeFormat' });
  window[_owl].registerNative(DateTimeFormatWrapper, 'function DateTimeFormat() { [native code] }');
  
  Intl.DateTimeFormat = DateTimeFormatWrapper;
)JS";
    
    return ss.str();
}

std::string TimezoneSpoof::EscapeJS(const std::string& str) {
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

} // namespace spoofs
} // namespace owl
