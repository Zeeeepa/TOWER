#include "stealth/owl_font_spoofer.h"
#include "stealth/owl_virtual_machine.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace owl {

std::string FontSpoofer::EscapeJS(const std::string& str) {
    std::string result;
    result.reserve(str.length() * 2);
    for (char c : str) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\'': result += "\\'"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

std::vector<std::string> FontSpoofer::GetMacOSExclusiveFonts() {
    // IMPORTANT: This list should ONLY contain fonts that are EXCLUSIVE to macOS
    // and should NEVER appear on Windows/Linux. Fonts that exist on both platforms
    // (like monaco, gill sans, baskerville, etc.) should NOT be in this list
    // because they need to be FAKED as installed on Windows/Linux profiles.
    return {
        // Apple system fonts - CRITICAL for fontPreferences detection
        "-apple-system", "blinkmacsystemfont", "apple system",
        // San Francisco family (Apple exclusive)
        "sf pro", "sf pro display", "sf pro text", "sf pro rounded",
        "sf compact", "sf compact display", "sf compact text",
        "sf mono", "sf arabic", "sf hebrew",
        "san francisco", ".sf", "new york",
        // macOS-exclusive fonts (NOT on Windows)
        "helvetica neue", "helvetica light",
        "lucida grande", "geneva",
        "big caslon", "cochin", "skia", "herculanum",
        // Apple-specific emoji and symbols
        "apple color emoji", "apple symbols", "apple sd gothic",
        "applegothic", "applemyungjo", "avenir", "avenir next",
        "phosphate", "signpainter",
        // Asian fonts specific to macOS
        "pingfang", "pingfang sc", "pingfang tc", "pingfang hk",
        "songti", "songti sc", "songti tc", "stheiti",
        "hiragino", "hiragino sans", "hiragino sans gb", "hiragino mincho",
        "heiti", "heiti sc", "heiti tc",
        "kaiti sc", "kaiti tc",
        // Additional macOS-exclusive fonts
        "iowan", "iowan old style", "athelas", "charter", "seravek", "superclarendon",
        "noteworthy", "trattatello",
        // Apple-specific fonts
        "apple chancery", "apple braille", "academy engraved",
        "al bayan", "al nile", "al tarikh",
        "ayuthaya", "baghdad", "bangla mn", "bangla sangam",
        "beirut", "bodoni ornaments", "bradley hand",
        "chalkduster", "damascus", "farah", "decotype naskh",
        "devanagari sangam", "din alternate", "din condensed",
        "diwan kufi", "diwan thuluth", "euphemia ucas", "farisi",
        "galvji", "geeza pro", "gujarati sangam",
        "gurmukhi mn", "gurmukhi sangam", "inaimathi",
        "itf devanagari", "kailasa", "kannada mn", "kannada sangam",
        "kefa", "khmer mn", "khmer sangam", "kohinoor bangla",
        "kohinoor devanagari", "kohinoor gujarati", "kohinoor telugu",
        "kokonor", "krungthep", "kufistandardgk", "lao mn", "lao sangam",
        "luminari", "malayalam mn", "malayalam sangam", "marion",
        "mishafi", "mshtakan", "mukta mahee", "muna", "myanmar mn",
        "myanmar sangam", "nadeem", "new peninim",
        "oriya mn", "oriya sangam", "party let", "plantagenet cherokee",
        "raanana", "sana", "sathu", "savoye let", "silom",
        "sinhala mn", "sinhala sangam",
        "stsong", "sukhumvit set", "tamil mn", "tamil sangam",
        "telugu mn", "telugu sangam", "thonburi", "waseem",
        // PT fonts (bundled with macOS)
        "pt mono", "pt sans", "pt serif"
        // REMOVED: monaco, gill sans, baskerville, copperplate, didot, futura,
        // papyrus, optima, hoefler text, menlo, marker felt, chalkboard,
        // zapfino, snell roundhand, american typewriter, brush script, nanum gothic
        // These fonts exist on Windows too and should be FAKED as installed, not blocked
    };
}

std::vector<std::string> FontSpoofer::GetTargetOSFonts(const std::string& target_os) {
    if (target_os == "Windows") {
        return {
            "Arial", "Arial Black", "Calibri", "Calibri Light", "Cambria", "Cambria Math",
            "Candara", "Comic Sans MS", "Consolas", "Constantia", "Corbel", "Courier New",
            "Ebrima", "Franklin Gothic Medium", "Gabriola", "Gadugi", "Georgia",
            "Impact", "Ink Free", "Javanese Text", "Leelawadee UI", "Lucida Console",
            "Lucida Sans Unicode", "Malgun Gothic", "Marlett", "Microsoft Himalaya",
            "Microsoft JhengHei", "Microsoft New Tai Lue", "Microsoft PhagsPa",
            "Microsoft Sans Serif", "Microsoft Tai Le", "Microsoft YaHei",
            "Microsoft Yi Baiti", "MingLiU-ExtB", "Mongolian Baiti", "MS Gothic",
            "MS PGothic", "MS UI Gothic", "MV Boli", "Myanmar Text", "Nirmala UI",
            "Palatino Linotype", "Segoe MDL2 Assets", "Segoe Print", "Segoe Script",
            "Segoe UI", "Segoe UI Emoji", "Segoe UI Historic", "Segoe UI Symbol",
            "SimSun", "Sitka", "Sylfaen", "Symbol", "Tahoma", "Times New Roman",
            "Trebuchet MS", "Verdana", "Webdings", "Wingdings", "Yu Gothic"
        };
    } else if (target_os == "Linux") {
        return {
            "DejaVu Sans", "DejaVu Sans Mono", "DejaVu Serif",
            "Liberation Sans", "Liberation Serif", "Liberation Mono",
            "Ubuntu", "Ubuntu Mono", "Ubuntu Condensed",
            "Noto Sans", "Noto Serif", "Noto Mono", "Noto Color Emoji",
            "Droid Sans", "Droid Sans Mono", "Droid Serif",
            "FreeSans", "FreeMono", "FreeSerif",
            "Cantarell", "Nimbus Sans", "Nimbus Mono", "Nimbus Roman",
            "Bitstream Vera Sans", "Bitstream Vera Sans Mono", "Bitstream Vera Serif",
            "Arial", "Times New Roman", "Courier New",
            "Georgia", "Verdana", "Trebuchet MS"
        };
    } else {
        // macOS fonts
        return {
            "SF Pro", "SF Pro Display", "SF Pro Text", "SF Mono", "New York",
            "Helvetica Neue", "Helvetica", "Arial", "Arial Unicode MS",
            "Menlo", "Monaco", "Courier New", "Courier",
            "Times New Roman", "Times", "Georgia",
            "Lucida Grande", "Geneva", "Verdana", "Tahoma",
            "Gill Sans", "Futura", "Optima", "Baskerville", "Didot",
            "Apple Color Emoji", "Apple Symbols",
            "Avenir", "Avenir Next", "Hoefler Text", "Palatino"
        };
    }
}

std::string FontSpoofer::GenerateScript(const VirtualMachine& vm) {
    // Default: no per-context variation (fonts_seed = 0)
    return GenerateScript(vm, 0);
}

std::string FontSpoofer::GenerateScript(const VirtualMachine& vm, uint64_t fonts_seed) {
    Config config;
    config.target_os = vm.os.name;
    config.spoofed_dpr = vm.screen.device_pixel_ratio;
    config.block_mac_fonts = (vm.os.name != "macOS");
    config.normalize_measurements = true;  // Enabled: Fixes 2x font size on Retina displays
    config.allowed_fonts = vm.fonts.installed;
    config.fonts_seed = fonts_seed;  // Per-context font variation seed

    // If no fonts specified in profile, use defaults for target OS
    if (config.allowed_fonts.empty()) {
        config.allowed_fonts = GetTargetOSFonts(config.target_os);
    }

    return GenerateScript(config);
}

std::string FontSpoofer::GenerateScript(const Config& config) {
    std::stringstream ss;

    ss << "\n// ============================================================\n";
    ss << "// FontSpoofer - Complete font fingerprint protection\n";
    ss << "// Target OS: " << config.target_os << "\n";
    ss << "// Block macOS fonts: " << (config.block_mac_fonts ? "YES" : "NO") << "\n";
    ss << "// ============================================================\n\n";

    // Generate components
    ss << GenerateFontLists(config);
    ss << GenerateMeasureTextHook(config);
    ss << GenerateDOMMeasurementHooks(config);
    ss << GenerateFontAPIHooks(config);
    ss << GenerateCSSStyleHooks(config);
    ss << GenerateComputedStyleHook(config);
    ss << GenerateSetAttributeHook(config);

    return ss.str();
}

std::string FontSpoofer::GenerateFontLists(const Config& config) {
    std::stringstream ss;

    // CRITICAL: Detect actual RENDERING DPR through measurement
    // CEF headless reports window.devicePixelRatio = 1, but renders at host DPR!
    // We must measure to detect the actual scale factor being applied to text.
    ss << R"(
// ============================================================
// CRITICAL: Detect actual rendering DPR through measurement
// CEF headless mode issue: devicePixelRatio = 1 but renders at host DPR
// Solution: Measure actual text width and compare to expected
// ============================================================
(function() {
    'use strict';
    const _owl = Symbol.for('owl');
    if (!window[_owl]) {
        Object.defineProperty(window, _owl, {
            value: { font: {}, webgl: {}, camera: {} },
            writable: false,
            enumerable: false,
            configurable: false
        });
    }
    // Ensure sub-objects exist even if window[_owl] was created elsewhere
    if (!window[_owl].font) window[_owl].font = {};
    if (!window[_owl].webgl) window[_owl].webgl = {};
    if (!window[_owl].camera) window[_owl].camera = {};

    // Detect actual rendering DPR by measuring text
    // This works even when window.devicePixelRatio is spoofed or wrong
    if (typeof window[_owl].font.actualDPR === 'undefined') {
        try {
            // Create an off-screen canvas for measurement
            const canvas = document.createElement('canvas');
            canvas.width = 500;
            canvas.height = 100;
            const ctx = canvas.getContext('2d');

            // Measure a reference text with known font
            // Expected width at DPR=1 for "mmmmmmmmmmlli" at 72px monospace is ~468px
            // At DPR=2, it would measure ~936px (before any correction)
            ctx.font = '72px monospace';
            const testText = 'mmmmmmmmmmlli';
            const measuredWidth = ctx.measureText(testText).width;

            // Reference values determined empirically:
            // - DPR 1.0: ~468px (466-470)
            // - DPR 1.5: ~702px (700-704)
            // - DPR 2.0: ~936px (934-938)
            // - DPR 2.5: ~1170px (1168-1172)
            // - DPR 3.0: ~1404px (1402-1406)
            // Calculate DPR as ratio to reference (468px at DPR=1)
            const REFERENCE_WIDTH = 468;
            let detectedDPR = measuredWidth / REFERENCE_WIDTH;

            // Round to common DPR values (1, 1.25, 1.5, 2, 2.5, 3)
            const commonDPRs = [1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3];
            let closestDPR = 1;
            let minDiff = Math.abs(detectedDPR - 1);
            for (const dpr of commonDPRs) {
                const diff = Math.abs(detectedDPR - dpr);
                if (diff < minDiff) {
                    minDiff = diff;
                    closestDPR = dpr;
                }
            }

            // If reported DPR matches detected, use reported (more accurate)
            // Otherwise use detected DPR (CEF headless bug workaround)
            const reportedDPR = window.devicePixelRatio || 1;
            if (Math.abs(reportedDPR - closestDPR) < 0.1) {
                window[_owl].font.actualDPR = reportedDPR;
            } else {
                window[_owl].font.actualDPR = closestDPR;
                // console.log('[FontSpoofer] DPR mismatch: reported=' + reportedDPR + ', detected=' + closestDPR + ', measured=' + measuredWidth);
            }
        } catch (e) {
            // Fallback to reported DPR if measurement fails
            window[_owl].font.actualDPR = window.devicePixelRatio || 1;
        }
    }
})();
)";

    // Generate allowed fonts set
    ss << "// Allowed fonts for target OS\n";
    ss << "const __fontAllowedFonts = new Set([\n";
    for (size_t i = 0; i < config.allowed_fonts.size(); ++i) {
        ss << "  '" << EscapeJS(config.allowed_fonts[i]) << "'";
        if (i < config.allowed_fonts.size() - 1) ss << ",";
        ss << "\n";
    }
    ss << "]);\n\n";

    // Generate macOS exclusive fonts list
    if (config.block_mac_fonts) {
        auto mac_fonts = GetMacOSExclusiveFonts();
        ss << "// macOS-exclusive fonts to block\n";
        ss << "const __fontMacOSExclusive = [\n";
        for (size_t i = 0; i < mac_fonts.size(); ++i) {
            ss << "  '" << EscapeJS(mac_fonts[i]) << "'";
            if (i < mac_fonts.size() - 1) ss << ",";
            ss << "\n";
        }
        ss << "];\n\n";
    }

    // Font name normalization map (macOS font names -> Windows equivalents)
    ss << "// Font name normalization (macOS -> Windows)\n";
    ss << "const __fontNameMap = {\n";
    ss << "  'times': 'Times New Roman',\n";
    ss << "  'helvetica': 'Arial',\n";
    ss << "  'courier': 'Courier New',\n";
    ss << "  'palatino': 'Palatino Linotype'\n";
    ss << "};\n\n";

    // Generate config object with actual DPR from JavaScript
    // CRITICAL: CEF in headless/off-screen mode returns measurements in DEVICE pixels
    // We need to normalize to CSS pixels by dividing by actualDPR
    ss << "// Font spoofing configuration\n";
    ss << "const __fontConfig = {\n";
    ss << "  targetOS: '" << EscapeJS(config.target_os) << "',\n";
    ss << "  actualDPR: (window[Symbol.for('owl')] && window[Symbol.for('owl')].font.actualDPR) || window.devicePixelRatio || 1,\n";
    ss << "  spoofedDPR: " << config.spoofed_dpr << ",\n";
    ss << "  blockMacFonts: " << (config.block_mac_fonts ? "true" : "false") << ",\n";
    ss << "  normalizeMeasurements: " << (config.normalize_measurements ? "true" : "false") << ",\n";
    // CRITICAL FIX: CEF returns DEVICE pixels, not CSS pixels on Retina displays!
    // We must divide by actualDPR to normalize to CSS pixels.
    // Evidence: fontPreferences values were exactly 2x real Chrome on 2x Retina display.
    ss << "  get dprScale() { return 1 / this.actualDPR; },\n";
    // Per-context font variation seed - use 0x prefix for BigInt in JS
    // Read from Symbol-keyed property if not provided directly
    ss << "  fontsSeed: " << (config.fonts_seed != 0 ?
        ("0x" + ([&]() {
            std::stringstream hex;
            hex << std::hex << config.fonts_seed;
            return hex.str();
        })() + "n") :
        "(typeof __vmFontsSeed !== 'undefined' ? __vmFontsSeed : "
        "(typeof self !== 'undefined' && self[Symbol.for('__owl_fonts_seed__')] ? "
        "self[Symbol.for('__owl_fonts_seed__')] : 0n))") << "\n";
    ss << "};\n\n";

    // CRITICAL: Generate __fakeInstalledFonts as a GLOBAL Set
    // This is the SINGLE SOURCE OF TRUTH for which fonts appear installed
    // Used by: measureText, offsetWidth, document.fonts.check(), etc.
    ss << R"(
// ============================================================
// FAKE INSTALLED FONTS - Single source of truth for font detection
// All font detection hooks must use this set for consistency
// ============================================================
const __windowsFonts = [
    // Core Windows fonts
    'arial', 'arial black', 'arial narrow', 'arial unicode ms',
    'calibri', 'calibri light', 'cambria', 'cambria math', 'candara',
    'century', 'century gothic', 'comic sans ms', 'consolas', 'constantia', 'corbel',
    'courier', 'courier new',
    // Serif fonts
    'georgia', 'garamond', 'book antiqua', 'bookman old style',
    'palatino', 'palatino linotype', 'times', 'times new roman',
    // Sans-serif fonts
    'franklin gothic medium', 'gill sans', 'gill sans mt',
    'helvetica', 'impact', 'lucida sans', 'lucida sans unicode',
    'microsoft sans serif', 'segoe ui', 'segoe ui light', 'segoe ui semibold',
    'segoe print', 'segoe script', 'tahoma', 'trebuchet ms', 'verdana',
    // Monospace fonts
    'lucida console', 'monaco', 'andale mono',
    // Decorative fonts
    'brush script mt', 'copperplate gothic', 'copperplate gothic bold',
    'papyrus', 'rockwell', 'rockwell extra bold',
    // Symbol fonts
    'symbol', 'webdings', 'wingdings', 'wingdings 2', 'wingdings 3', 'marlett',
    // CJK fonts
    'simhei', 'simsun', 'simsun-extb', 'nsimsun', 'fangsong', 'kaiti',
    'microsoft yahei', 'microsoft yahei ui', 'microsoft jhenghei',
    'mingliu', 'mingliu-extb', 'pmingliu', 'ms gothic', 'ms pgothic',
    'ms ui gothic', 'ms mincho', 'meiryo', 'yu gothic', 'malgun gothic',
    'gulim', 'dotum', 'batang',
    // International fonts
    'arabic typesetting', 'simplified arabic', 'traditional arabic',
    'david', 'miriam', 'browallia new', 'cordia new', 'angsana new',
    'kartika', 'latha', 'mangal', 'raavi', 'shruti', 'tunga', 'vrinda',
    'nirmala ui', 'leelawadee ui',
    // Office fonts
    'ebrima', 'gabriola', 'gadugi', 'javanese text', 'mv boli',
    'sylfaen', 'sitka', 'ink free'
];

const __macOSFonts = [
    // San Francisco family (system fonts)
    'sf pro', 'sf pro display', 'sf pro text', 'sf pro rounded',
    'sf mono', 'sf compact', 'sf compact display', 'sf compact text',
    // New York family
    'new york', 'new york small', 'new york medium', 'new york large',
    // Classic macOS fonts
    'helvetica', 'helvetica neue', 'helvetica light',
    'lucida grande', 'geneva', 'monaco', 'menlo',
    // Serif fonts
    'times', 'times new roman', 'georgia', 'palatino',
    'baskerville', 'big caslon', 'cochin', 'didot', 'hoefler text',
    'iowan old style', 'athelas', 'charter', 'superclarendon',
    // Sans-serif fonts
    'arial', 'arial black', 'arial narrow', 'verdana', 'tahoma', 'trebuchet ms',
    'gill sans', 'futura', 'optima', 'avenir', 'avenir next', 'avenir next condensed',
    'seravek',
    // Monospace fonts
    'courier', 'courier new', 'andale mono',
    // Decorative fonts
    'american typewriter', 'brush script mt', 'chalkboard', 'chalkboard se',
    'comic sans ms', 'copperplate', 'impact', 'marker felt', 'noteworthy',
    'papyrus', 'phosphate', 'rockwell', 'signpainter', 'skia', 'snell roundhand',
    'trattatello', 'zapfino',
    // Symbol fonts
    'apple symbols', 'symbol', 'webdings', 'wingdings', 'wingdings 2', 'wingdings 3',
    // CJK fonts
    'pingfang sc', 'pingfang tc', 'pingfang hk',
    'hiragino sans', 'hiragino sans gb', 'hiragino mincho pron',
    'heiti sc', 'heiti tc', 'songti sc', 'songti tc', 'kaiti sc', 'kaiti tc',
    'apple sd gothic neo', 'nanum gothic', 'apple myungjo',
    // Arabic/Hebrew
    'geeza pro', 'baghdad', 'nadeem', 'muna',
    'arial hebrew', 'corsiva hebrew', 'new peninim mt',
    // Other international
    'kohinoor devanagari', 'devanagari mt', 'devanagari sangam mn',
    'tamil sangam mn', 'kannada sangam mn', 'telugu sangam mn',
    'gujarati sangam mn', 'gurmukhi mn', 'oriya sangam mn',
    'sinhala sangam mn', 'malayalam sangam mn', 'bengali sangam mn',
    'khmer sangam mn', 'lao sangam mn', 'myanmar sangam mn', 'thai sangam mn',
    // Emoji
    'apple color emoji'
];

const __linuxFonts = [
    // DejaVu family (very common on all Linux distros)
    'dejavu sans', 'dejavu sans mono', 'dejavu serif',
    'dejavu sans condensed', 'dejavu serif condensed',
    'dejavu sans bold', 'dejavu serif bold', 'dejavu sans book',
    // Liberation family (metric-compatible with MS fonts - often pre-installed)
    'liberation sans', 'liberation serif', 'liberation mono',
    'liberation sans narrow',
    // Ubuntu fonts (Ubuntu/derivatives)
    'ubuntu', 'ubuntu mono', 'ubuntu condensed', 'ubuntu light',
    'ubuntu medium', 'ubuntu bold', 'ubuntu italic',
    // Noto family (Google fonts - very comprehensive, common on modern Linux)
    'noto sans', 'noto serif', 'noto mono', 'noto color emoji',
    'noto sans cjk sc', 'noto sans cjk tc', 'noto sans cjk jp', 'noto sans cjk kr',
    'noto serif cjk sc', 'noto serif cjk tc', 'noto serif cjk jp',
    'noto sans arabic', 'noto sans hebrew', 'noto sans thai',
    'noto sans devanagari', 'noto sans bengali', 'noto sans tamil',
    // Droid family (Android/older Linux)
    'droid sans', 'droid sans mono', 'droid serif',
    'droid sans fallback', 'droid arabic naskh',
    // Free fonts (GNU FreeFont)
    'freesans', 'freemono', 'freeserif',
    'free sans', 'free mono', 'free serif',
    // Nimbus family (URW fonts - Ghostscript compatible)
    'nimbus sans', 'nimbus sans l', 'nimbus mono', 'nimbus mono l',
    'nimbus roman', 'nimbus roman no9 l',
    // Bitstream fonts (classic Linux fonts)
    'bitstream vera sans', 'bitstream vera sans mono', 'bitstream vera serif',
    'bitstream charter',
    // GNOME/GTK fonts
    'cantarell', 'cantarell light', 'cantarell bold',
    // Fira fonts (Mozilla - common on Fedora)
    'fira sans', 'fira mono', 'fira code',
    // Common cross-platform fonts (often installed via packages)
    'arial', 'times new roman', 'courier new', 'georgia', 'verdana',
    'trebuchet ms', 'impact', 'comic sans ms', 'helvetica',
    'arial black', 'arial narrow', 'arial unicode ms',
    'palatino', 'palatino linotype', 'book antiqua',
    'century gothic', 'century', 'gill sans', 'gill sans mt',
    'lucida sans', 'lucida console', 'lucida grande',
    'tahoma', 'andale mono', 'monaco',
    // Source family (Adobe - very popular on Linux)
    'source sans pro', 'source serif pro', 'source code pro',
    'source sans 3', 'source serif 4',
    // Roboto (Android/Chrome OS - common on Linux)
    'roboto', 'roboto mono', 'roboto condensed', 'roboto slab',
    'roboto light', 'roboto medium', 'roboto bold',
    // Open Sans (very popular web font)
    'open sans', 'open sans condensed', 'open sans light',
    // Other popular web fonts often installed
    'lato', 'montserrat', 'oswald', 'raleway', 'poppins',
    'pt sans', 'pt serif', 'pt mono',
    'ibm plex sans', 'ibm plex serif', 'ibm plex mono',
    'inter', 'nunito', 'quicksand', 'work sans',
    // CJK fonts (Chinese/Japanese/Korean)
    'wenquanyi micro hei', 'wenquanyi zen hei', 'wqy microhei', 'wqy zenhei',
    'ar pl uming cn', 'ar pl ukai cn', 'ar pl uming tw',
    'takao gothic', 'takao mincho', 'ipa gothic', 'ipa mincho',
    'ipagothic', 'ipamincho', 'ipaexgothic', 'ipaexmincho',
    'un batang', 'un dotum', 'baekmuk gulim', 'baekmuk batang',
    'nanum gothic', 'nanum myeongjo', 'nanum barun gothic',
    // Symbol/Dingbat fonts
    'symbola', 'dingbats', 'zapf dingbats',
    // International fonts (common on Linux with language packs)
    'lohit devanagari', 'lohit tamil', 'lohit bengali', 'lohit gujarati',
    'lohit kannada', 'lohit malayalam', 'lohit punjabi', 'lohit telugu',
    'kacst', 'kacst one', 'kacst book', 'kacst letter',
    'freearabic', 'freefarsi', 'padauk', 'khmer os',
    // Monospace/coding fonts
    'inconsolata', 'hack', 'jetbrains mono', 'cascadia code',
    'terminus', 'anonymous pro', 'fantasque sans mono'
];

// ============================================================
// PER-CONTEXT FONT VARIATION
// Uses fonts_seed to deterministically select which fonts appear installed
// This prevents all contexts with same OS from having identical font lists
// ============================================================

// Simple seeded PRNG (mulberry32) for deterministic font selection
function __fontSeedRandom(seed) {
    // Convert BigInt seed to 32-bit number for mulberry32
    let s = Number(seed & 0xFFFFFFFFn);
    return function() {
        s |= 0; s = s + 0x6D2B79F5 | 0;
        let t = Math.imul(s ^ s >>> 15, 1 | s);
        t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
        return ((t ^ t >>> 14) >>> 0) / 4294967296;
    };
}

// Deterministically filter fonts based on seed
// - Always keeps "core" fonts that must be present for realism
// - Randomly includes/excludes "optional" fonts based on seed
// - Target: 70-90% of fonts remain (realistic variation)
function __filterFontsWithSeed(allFonts, seed) {
    if (seed === 0n) {
        // No seed: return all fonts (fallback behavior)
        return allFonts;
    }

    const rng = __fontSeedRandom(seed);

    // Core fonts that MUST always be present (never filtered out)
    // These are essential for basic web rendering
    const coreFonts = new Set([
        // Cross-platform core
        'arial', 'times new roman', 'courier new', 'georgia', 'verdana',
        'tahoma', 'trebuchet ms', 'impact', 'comic sans ms',
        // System fonts by OS
        'segoe ui', 'calibri', 'consolas',  // Windows
        'sf pro', 'helvetica neue', 'menlo',  // macOS
        'dejavu sans', 'liberation sans', 'ubuntu',  // Linux
        // Monospace essentials
        'monaco', 'lucida console',
        // Emoji
        'segoe ui emoji', 'apple color emoji', 'noto color emoji'
    ]);

    const result = [];
    for (const font of allFonts) {
        const isCore = coreFonts.has(font);
        if (isCore) {
            // Always include core fonts
            result.push(font);
        } else {
            // Probabilistically include optional fonts (75% chance)
            // This creates variation while keeping most fonts
            if (rng() < 0.75) {
                result.push(font);
            }
        }
    }

    return result;
}

// Select base fonts for target OS, then apply per-context variation
const __baseFonts = (
    __fontConfig.targetOS === 'Windows' ? __windowsFonts :
    __fontConfig.targetOS === 'macOS' ? __macOSFonts :
    __linuxFonts
);

// Apply per-context font variation using fonts_seed
const __fakeInstalledFonts = new Set(
    __filterFontsWithSeed(__baseFonts, __fontConfig.fontsSeed)
);
)";

    return ss.str();
}

std::string FontSpoofer::GenerateMeasureTextHook(const Config& config) {
    std::stringstream ss;

    ss << R"(
// ============================================================
// measureText Hook - CRITICAL for fontPreferences and font detection
// Must return DIFFERENT widths for "installed" fonts vs fallback
// ============================================================
(function() {
    'use strict';

    // Check if font string contains macOS-exclusive fonts
    // FIXED: Use exact font name matching instead of substring matching
    // This prevents false positives like "Monaco" matching "monaco" in Windows fonts
    const containsMacFont = (fontStr) => {
        if (!fontStr || !__fontConfig.blockMacFonts) return false;
        // Extract individual font names from CSS font-family string
        const fonts = fontStr.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        // Check if any extracted font exactly matches a macOS-exclusive font
        return fonts.some(font => __fontMacOSExclusive.includes(font));
    };

    // Parse CSS font string to extract font families
    // Format: "font-style font-variant font-weight font-size/line-height font-family"
    const parseFontFamilies = (fontStr) => {
        if (!fontStr) return [];
        // Find the font-family part (after the last space before a font name)
        // Simple approach: split by comma for font-family list
        const match = fontStr.match(/['"]*([^'"]+)['"]*(?:,|$)/g);
        if (!match) return [];
        return match.map(f => f.replace(/[,'"]/g, '').trim()).filter(f => f);
    };

    // Extract first font family from CSS font string
    const extractFirstFontFamily = (fontStr) => {
        if (!fontStr) return null;
        const parts = fontStr.trim().split(/\s+/);
        let familyStart = 0;
        for (let i = 0; i < parts.length; i++) {
            if (/^\d/.test(parts[i]) || /^[\d.]+(px|pt|em|rem|%|vh|vw)/.test(parts[i])) {
                familyStart = i + 1;
                break;
            }
        }
        if (familyStart < parts.length) {
            const family = parts.slice(familyStart).join(' ');
            const firstFamily = family.split(',')[0].replace(/['"]/g, '').trim();
            return firstFamily;
        }
        return fontStr.split(',')[0].replace(/['"]/g, '').trim();
    };

    // Simple hash function for consistent font-based offsets
    const hashFontName = (name) => {
        let hash = 0;
        for (let i = 0; i < name.length; i++) {
            hash = ((hash << 5) - hash) + name.charCodeAt(i);
            hash = hash & hash;
        }
        return Math.abs(hash);
    };

    // Check if font is in fake installed fonts
    const isFakeInstalledFont = (fontName) => {
        if (!fontName) return false;
        const lower = fontName.toLowerCase().replace(/['"]/g, '').trim();
        return __fakeInstalledFonts.has(lower);
    };

    // Get width offset for a fake installed font (makes it appear different from fallback)
    const getFakeFontWidthOffset = (fontFamily) => {
        if (!fontFamily) return 0;
        const firstFont = extractFirstFontFamily(fontFamily);
        if (!firstFont) return 0;

        // Skip generic fallbacks
        const genericFonts = ['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'system-ui'];
        if (genericFonts.includes(firstFont.toLowerCase())) return 0;

        // If this is a fake installed font, return a consistent offset
        if (isFakeInstalledFont(firstFont)) {
            // Return consistent offset based on font name hash (2.0 to 4.0 pixels)
            // Increased from 0.1-2.0px to ensure it survives DPR scaling
            const hash = hashFontName(firstFont.toLowerCase());
            return 2.0 + (hash % 21) / 10.0;
        }

        return 0;
    };

    // Remove macOS fonts from font-family string, keeping the rest
    const removeMacFonts = (fontStr) => {
        if (!fontStr) return fontStr;

        // Extract font-size/style prefix and font-family part
        const parts = fontStr.split(/\s+/);
        let sizePrefix = '';
        let familyPart = '';

        // Find where font-family starts (typically after size like "72px")
        for (let i = 0; i < parts.length; i++) {
            if (/\d+(px|pt|em|rem|%)?/.test(parts[i])) {
                sizePrefix = parts.slice(0, i + 1).join(' ');
                familyPart = parts.slice(i + 1).join(' ');
                break;
            }
        }

        if (!familyPart) {
            // No size found, treat whole string as font-family
            familyPart = fontStr;
        }

        // Split font families and filter
        const families = familyPart.split(',').map(f => f.trim());
        const filtered = families.filter(f => {
            const lower = f.replace(/['"]/g, '').toLowerCase();
            return !__fontMacOSExclusive.includes(lower);
        });

        // If all fonts were filtered out, use sans-serif as fallback
        const resultFamily = filtered.length > 0 ? filtered.join(', ') : 'sans-serif';

        return sizePrefix ? (sizePrefix + ' ' + resultFamily) : resultFamily;
    };

    // Store original measureText
    const _origMeasureText = CanvasRenderingContext2D.prototype.measureText;

    // Get createNativeProxy from global registry
    const _owl_mt_sym = Symbol.for('owl');
    const createNativeProxy = window[_owl_mt_sym]?.createNativeProxy || ((orig, handler) => new Proxy(orig, { apply: handler }));

    // CRITICAL: Round fontBoundingBox values to integers to avoid "metric noise detected"
    // CEF's font metrics can return fractional values (e.g., 14.4, 3.2) which CreepJS detects
    // This function rounds the values IN-PLACE using Object.defineProperty
    const roundFontMetrics = (metrics) => {
        const propsToRound = ['fontBoundingBoxAscent', 'fontBoundingBoxDescent'];
        for (let i = 0; i < propsToRound.length; i++) {
            const prop = propsToRound[i];
            if (prop in metrics) {
                const val = metrics[prop];
                if (typeof val === 'number' && val % 1 !== 0) {
                    Object.defineProperty(metrics, prop, {
                        value: Math.round(val),
                        writable: false,
                        configurable: true,
                        enumerable: true
                    });
                }
            }
        }
        return metrics;
    };

    // Create a TextMetrics object with modified width
    // Note: fontBoundingBox values are rounded in the adjusted object
    const adjustTextMetrics = (original, widthOffset, scale) => {
        const effectiveScale = scale || 1;
        const adjustedWidth = (original.width + widthOffset) * effectiveScale;

        // Create a new object that mimics TextMetrics with adjusted values
        // Round fontBoundingBox values to integers to pass CreepJS metric noise detection
        const adjusted = {
            width: adjustedWidth,
            actualBoundingBoxLeft: (original.actualBoundingBoxLeft || 0) * effectiveScale,
            actualBoundingBoxRight: ((original.actualBoundingBoxRight || 0) + widthOffset) * effectiveScale,
            fontBoundingBoxAscent: Math.round((original.fontBoundingBoxAscent || 0) * effectiveScale),
            fontBoundingBoxDescent: Math.round((original.fontBoundingBoxDescent || 0) * effectiveScale),
            actualBoundingBoxAscent: (original.actualBoundingBoxAscent || 0) * effectiveScale,
            actualBoundingBoxDescent: (original.actualBoundingBoxDescent || 0) * effectiveScale,
            emHeightAscent: (original.emHeightAscent || 0) * effectiveScale,
            emHeightDescent: (original.emHeightDescent || 0) * effectiveScale,
            hangingBaseline: (original.hangingBaseline || 0) * effectiveScale,
            alphabeticBaseline: (original.alphabeticBaseline || 0) * effectiveScale,
            ideographicBaseline: (original.ideographicBaseline || 0) * effectiveScale
        };
        // Make it look like a real TextMetrics object
        Object.setPrototypeOf(adjusted, TextMetrics.prototype);
        return adjusted;
    };

    // Check if font should be masked (not in fake installed fonts AND not a fallback)
    const shouldMaskFont = (fontStr) => {
        if (!fontStr) return false;
        const firstFont = extractFirstFontFamily(fontStr);
        if (!firstFont) return false;
        const lower = firstFont.toLowerCase();

        // Don't mask generic fallbacks
        const genericFonts = ['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'system-ui'];
        if (genericFonts.includes(lower)) return false;

        // If font is in our fake installed list, don't mask it
        if (isFakeInstalledFont(firstFont)) return false;

        // Otherwise, mask it (font is installed on host but shouldn't appear installed for target OS)
        return true;
    };

    // Get fallback font for masking non-installed fonts
    const getFallbackFont = (fontStr) => {
        if (!fontStr) return 'sans-serif';
        // Extract size and style prefix, replace family with fallback
        const parts = fontStr.trim().split(/\s+/);
        let sizeIndex = -1;
        for (let i = 0; i < parts.length; i++) {
            if (/^\d/.test(parts[i]) || /^[\d.]+(px|pt|em|rem|%|vh|vw)/.test(parts[i])) {
                sizeIndex = i;
                break;
            }
        }
        if (sizeIndex >= 0) {
            return parts.slice(0, sizeIndex + 1).join(' ') + ' sans-serif';
        }
        return fontStr.split(',')[0] + ', sans-serif';
    };

    // Create Proxy-based measureText that passes all introspection tests
    const measureTextProxy = createNativeProxy(_origMeasureText, (target, thisArg, args) => {
        const [text] = args;
        const currentFont = thisArg.font || '';
        const dprScale = (__fontConfig.normalizeMeasurements && __fontConfig.dprScale !== 1) ?
                         __fontConfig.dprScale : 1;

        // Check if current font contains any macOS-exclusive font
        if (containsMacFont(currentFont)) {
            // CRITICAL FIX: Instead of using monospace, remove macOS fonts
            // and use the remaining fallback fonts. This ensures:
            // - fontPreferences.apple uses sans-serif (not monospace)
            // - fontPreferences.apple equals fontPreferences.sans
            const cleanFont = removeMacFonts(currentFont);

            // Save current font
            const savedFont = thisArg.font;

            // Set to cleaned font (macOS fonts removed)
            thisArg.font = cleanFont;
            let result = target.call(thisArg, text);

            // Restore original font
            thisArg.font = savedFont;

            // Apply DPR scaling
            if (dprScale !== 1) {
                result = adjustTextMetrics(result, 0, dprScale);
            }

            // CRITICAL: Always round fontBoundingBox values to avoid metric noise detection
            return roundFontMetrics(result);
        }

        // CRITICAL FIX: Mask fonts that are installed on host but NOT in our fake list
        // This prevents fonts like Menlo (on macOS host) from appearing installed
        // when using a Windows profile
        if (shouldMaskFont(currentFont)) {
            const savedFont = thisArg.font;
            // Use fallback font to get consistent "not installed" measurement
            thisArg.font = getFallbackFont(currentFont);
            let result = target.call(thisArg, text);
            thisArg.font = savedFont;

            // Apply DPR scaling
            if (dprScale !== 1) {
                result = adjustTextMetrics(result, 0, dprScale);
            }
            // CRITICAL: Always round fontBoundingBox values to avoid metric noise detection
            return roundFontMetrics(result);
        }

        // Get the actual measurement
        let result = target.call(thisArg, text);

        // CRITICAL: Add width offset for fake installed fonts
        // This makes font detection think the font is installed
        // because the width is different from the fallback font
        const widthOffset = getFakeFontWidthOffset(currentFont);

        // Apply offset and/or DPR scaling if needed
        if (widthOffset !== 0 || dprScale !== 1) {
            result = adjustTextMetrics(result, widthOffset, dprScale);
        }

        // CRITICAL: Always round fontBoundingBox values to avoid metric noise detection
        return roundFontMetrics(result);
    });

    // Install the Proxy - passes all introspection tests automatically
    Object.defineProperty(CanvasRenderingContext2D.prototype, 'measureText', {
        value: measureTextProxy, writable: true, enumerable: false, configurable: true
    });
})();
)";

    return ss.str();
}

std::string FontSpoofer::GenerateDOMMeasurementHooks(const Config& config) {
    std::stringstream ss;

    ss << R"(
// ============================================================
// DOM Measurement Hooks - offsetWidth/Height, getBoundingClientRect
// WITH DPR SCALING for fontPreferences detection
// WITH FAKE FONT DETECTION for fonts fingerprinting
// Uses global __fakeInstalledFonts defined in GenerateFontLists
// ============================================================
(function() {
    'use strict';

    // Mark FontSpoofer as active using Symbol for iframe detection
    const _owl = Symbol.for('owl');
    if (window[_owl]) {
        // Ensure font object exists (may not if RemoveCDPArtifacts ran first)
        if (!window[_owl].font) {
            window[_owl].font = {};
        }
        window[_owl].font.active = true;
    }

    // Get createNativeProxy from global registry for proper introspection bypass
    const createNativeProxy = window[_owl]?.createNativeProxy || ((orig, handler) => new Proxy(orig, { apply: handler }));

    // DPR scaling factor - CRITICAL for fontPreferences
    const dprScale = __fontConfig.normalizeMeasurements ? __fontConfig.dprScale : 1;

    // CRITICAL: Use native method to detect real HTMLElement vs Object.create(HTMLElement.prototype)
    // Native methods like 'click' have internal slot checking that throws "Illegal invocation"
    // on fake objects. Object.prototype.toString doesn't work because Symbol.toStringTag
    // makes both real and fake return "[object HTMLDivElement]" etc.
    const _origClick = HTMLElement.prototype.click;
    const isRealHTMLElement = (obj) => {
        try {
            // click throws "Illegal invocation" on fake objects (Object.create)
            // but executes successfully (or throws different error) on real elements
            _origClick.call(obj);
            return true;
        } catch (e) {
            // "Illegal invocation" means fake object
            return !e.message.includes('Illegal invocation');
        }
    };

    // NOTE: __fakeInstalledFonts is defined globally in GenerateFontLists
    // This ensures all font detection hooks use the SAME font list

    // Simple hash function for consistent font-based offsets
    const hashFontName = (name) => {
        let hash = 0;
        for (let i = 0; i < name.length; i++) {
            hash = ((hash << 5) - hash) + name.charCodeAt(i);
            hash = hash & hash;
        }
        return Math.abs(hash);
    };

    // Get width offset for a "fake installed" font
    // Returns a consistent offset (1-8px) based on font name hash
    const getFakeFontOffset = (fontFamily) => {
        if (!fontFamily) return 0;
        const fonts = fontFamily.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());

        // Check if first font (before fallback) is a fake installed font
        for (const font of fonts) {
            // Skip generic fallbacks
            if (['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'system-ui'].includes(font)) {
                continue;
            }
            // Check if this is a fake installed font
            if (__fakeInstalledFonts.has(font)) {
                // Return consistent offset based on font name (4-12px)
                // Increased from 1-8px to ensure it survives DPR scaling
                return (hashFontName(font) % 9) + 4;
            }
        }
        return 0;
    };

    // Check if font string contains macOS-exclusive fonts
    // FIXED: Use exact font name matching instead of substring matching
    // This prevents false positives like "Monaco" matching "monaco" in Windows fonts
    const containsMacFont = (fontStr) => {
        if (!fontStr || !__fontConfig.blockMacFonts) return false;
        // Extract individual font names from CSS font-family string
        const fonts = fontStr.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        // Check if any extracted font exactly matches a macOS-exclusive font
        return fonts.some(font => __fontMacOSExclusive.includes(font));
    };

    // Filter out macOS fonts from font-family
    const filterMacFonts = (fontFamily) => {
        if (!fontFamily) return fontFamily;
        const fonts = fontFamily.split(',').map(f => f.trim());
        const filtered = fonts.filter(f => {
            const name = f.replace(/['"]/g, '').toLowerCase();
            return !__fontMacOSExclusive.includes(name);
        });
        return filtered.length > 0 ? filtered.join(', ') : 'sans-serif';
    };

    // Check if font should be masked (not in fake installed fonts AND not a fallback)
    // Used to hide fonts installed on host but not in target OS profile
    const shouldMaskFontDOM = (fontFamily) => {
        if (!fontFamily) return false;
        const fonts = fontFamily.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        const firstFont = fonts[0];
        if (!firstFont) return false;

        // Don't mask generic fallbacks
        const genericFonts = ['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'system-ui'];
        if (genericFonts.includes(firstFont)) return false;

        // If font is in our fake installed list, don't mask
        if (__fakeInstalledFonts.has(firstFont)) return false;

        // Otherwise mask it (installed on host but shouldn't appear for target OS)
        return true;
    };

    // Check if this element is being used for font measurement
    // fingerprint.com uses specific patterns for fontPreferences
    const isFontMeasurement = (el) => {
        try {
            const text = el.textContent || el.innerText || '';
            // Font measurement typically uses short test strings
            if (text.length === 0 || text.length > 100) return false;
            // Check if element has specific font-related styling
            const style = el.style;
            if (style.fontSize || style.fontFamily || style.font) return true;
            // Check parent for font measurement patterns
            const parent = el.parentElement;
            if (parent && (parent.style.fontSize || parent.style.fontFamily)) return true;
            return text.length > 0 && text.length <= 50; // Likely a font test
        } catch (e) {
            return false;
        }
    };

    // Store original getComputedStyle for internal use
    const _origGetCS = window.getComputedStyle;

    // ========== offsetWidth Hook with DPR scaling + FAKE FONTS ==========
    // Using Proxy on the getter to pass all introspection tests
    const _origOffsetWidthDesc = Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'offsetWidth');
    if (_origOffsetWidthDesc && _origOffsetWidthDesc.get) {
        const _origOffsetWidthGet = _origOffsetWidthDesc.get;

        // Create Proxy-based getter that passes introspection tests
        const offsetWidthGetterProxy = createNativeProxy(_origOffsetWidthGet, (target, thisArg, args) => {
            // CRITICAL: Must throw TypeError when accessed on non-HTMLElement
            // This is how native getters behave and what CreepJS tests for
            if (!isRealHTMLElement(thisArg)) {
                throw new TypeError("Illegal invocation");
            }
            let result;
            let fontMasked = false; // Track if font was masked/blocked
            try {
                const style = _origGetCS.call(window, thisArg);
                const fontFamily = style.fontFamily || '';
                const inlineFontFamily = thisArg.style.fontFamily || '';
                const testFont = inlineFontFamily || fontFamily;

                // Block macOS-exclusive fonts
                if (containsMacFont(fontFamily)) {
                    fontMasked = true;
                    const text = thisArg.textContent || thisArg.innerText || '';
                    if (text.length > 0 && text.length <= 100) {
                        const origInline = thisArg.style.fontFamily;
                        thisArg.style.fontFamily = filterMacFonts(fontFamily);
                        result = target.call(thisArg);
                        thisArg.style.fontFamily = origInline;
                    } else {
                        result = target.call(thisArg);
                    }
                }
                // CRITICAL: Mask fonts installed on host but NOT in our fake list
                // This hides fonts like Menlo (macOS) from Windows profiles
                else if (shouldMaskFontDOM(testFont) && isFontMeasurement(thisArg)) {
                    fontMasked = true;
                    const origInline = thisArg.style.fontFamily;
                    thisArg.style.fontFamily = 'sans-serif';
                    result = target.call(thisArg);
                    thisArg.style.fontFamily = origInline;
                }
                else {
                    result = target.call(thisArg);
                }

                // Check if this is a font measurement scenario
                if (isFontMeasurement(thisArg)) {
                    // Apply DPR scaling
                    if (dprScale !== 1) {
                        result = result * dprScale;
                    }

                    // FAKE FONTS: Add offset for "installed" fonts
                    // CRITICAL: Do NOT add offset if we masked/blocked a font
                    if (!fontMasked) {
                        const fakeOffset = getFakeFontOffset(testFont);
                        if (fakeOffset > 0) {
                            result = result - fakeOffset; // Subtract to make it different from fallback
                        }
                    }
                }

                return result;
            } catch (e) {}
            return target.call(thisArg);
        });

        Object.defineProperty(HTMLElement.prototype, 'offsetWidth', {
            get: offsetWidthGetterProxy,
            configurable: true,
            enumerable: true
        });
    }

    // ========== offsetHeight Hook with DPR scaling ==========
    // Using Proxy on the getter to pass all introspection tests
    const _origOffsetHeightDesc = Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'offsetHeight');
    if (_origOffsetHeightDesc && _origOffsetHeightDesc.get) {
        const _origOffsetHeightGet = _origOffsetHeightDesc.get;

        // Create Proxy-based getter that passes introspection tests
        const offsetHeightGetterProxy = createNativeProxy(_origOffsetHeightGet, (target, thisArg, args) => {
            // CRITICAL: Must throw TypeError when accessed on non-HTMLElement
            // This is how native getters behave and what CreepJS tests for
            if (!isRealHTMLElement(thisArg)) {
                throw new TypeError("Illegal invocation");
            }
            let result;
            try {
                const style = _origGetCS.call(window, thisArg);
                const fontFamily = style.fontFamily || '';
                const inlineFontFamily = thisArg.style.fontFamily || '';
                const testFont = inlineFontFamily || fontFamily;

                // Block macOS-exclusive fonts
                if (containsMacFont(fontFamily)) {
                    const text = thisArg.textContent || thisArg.innerText || '';
                    if (text.length > 0 && text.length <= 100) {
                        const origInline = thisArg.style.fontFamily;
                        thisArg.style.fontFamily = filterMacFonts(fontFamily);
                        result = target.call(thisArg);
                        thisArg.style.fontFamily = origInline;
                    } else {
                        result = target.call(thisArg);
                    }
                }
                // CRITICAL: Mask fonts installed on host but NOT in our fake list
                else if (shouldMaskFontDOM(testFont) && isFontMeasurement(thisArg)) {
                    const origInline = thisArg.style.fontFamily;
                    thisArg.style.fontFamily = 'sans-serif';
                    result = target.call(thisArg);
                    thisArg.style.fontFamily = origInline;
                }
                else {
                    result = target.call(thisArg);
                }

                // Apply DPR scaling for font measurements
                if (dprScale !== 1 && isFontMeasurement(thisArg)) {
                    return result * dprScale;
                }
                return result;
            } catch (e) {}
            return target.call(thisArg);
        });

        Object.defineProperty(HTMLElement.prototype, 'offsetHeight', {
            get: offsetHeightGetterProxy,
            configurable: true,
            enumerable: true
        });
    }

    // ========== scrollWidth Hook with DPR scaling ==========
    // Using Proxy on the getter to pass all introspection tests
    const _origScrollWidthDesc = Object.getOwnPropertyDescriptor(Element.prototype, 'scrollWidth');
    if (_origScrollWidthDesc && _origScrollWidthDesc.get) {
        const _origScrollWidthGet = _origScrollWidthDesc.get;

        // Create Proxy-based getter that passes introspection tests
        const scrollWidthGetterProxy = createNativeProxy(_origScrollWidthGet, (target, thisArg, args) => {
            let result;
            try {
                if (thisArg instanceof HTMLElement) {
                    const style = _origGetCS.call(window, thisArg);
                    const fontFamily = style.fontFamily || '';

                    if (containsMacFont(fontFamily)) {
                        const text = thisArg.textContent || thisArg.innerText || '';
                        if (text.length > 0 && text.length <= 100) {
                            const origInline = thisArg.style.fontFamily;
                            thisArg.style.fontFamily = filterMacFonts(fontFamily);
                            result = _origScrollWidthGet.call(thisArg);
                            thisArg.style.fontFamily = origInline;
                        } else {
                            result = _origScrollWidthGet.call(thisArg);
                        }
                    } else {
                        result = _origScrollWidthGet.call(thisArg);
                    }

                    // Apply DPR scaling for font measurements
                    if (dprScale !== 1 && isFontMeasurement(thisArg)) {
                        return result * dprScale;
                    }
                    return result;
                }
            } catch (e) {}
            return _origScrollWidthGet.call(thisArg);
        }, 'get scrollWidth');

        Object.defineProperty(Element.prototype, 'scrollWidth', {
            get: scrollWidthGetterProxy,
            configurable: true,
            enumerable: true
        });
    }

    // ========== clientWidth Hook with DPR scaling ==========
    // Using Proxy on the getter to pass all introspection tests
    const _origClientWidthDesc = Object.getOwnPropertyDescriptor(Element.prototype, 'clientWidth');
    if (_origClientWidthDesc && _origClientWidthDesc.get) {
        const _origClientWidthGet = _origClientWidthDesc.get;

        // Create Proxy-based getter that passes introspection tests
        const clientWidthGetterProxy = createNativeProxy(_origClientWidthGet, (target, thisArg, args) => {
            let result;
            try {
                if (thisArg instanceof HTMLElement) {
                    const style = _origGetCS.call(window, thisArg);
                    const fontFamily = style.fontFamily || '';

                    if (containsMacFont(fontFamily)) {
                        const text = thisArg.textContent || thisArg.innerText || '';
                        if (text.length > 0 && text.length <= 100) {
                            const origInline = thisArg.style.fontFamily;
                            thisArg.style.fontFamily = filterMacFonts(fontFamily);
                            result = _origClientWidthGet.call(thisArg);
                            thisArg.style.fontFamily = origInline;
                        } else {
                            result = _origClientWidthGet.call(thisArg);
                        }
                    } else {
                        result = _origClientWidthGet.call(thisArg);
                    }

                    // Apply DPR scaling for font measurements
                    if (dprScale !== 1 && isFontMeasurement(thisArg)) {
                        return result * dprScale;
                    }
                    return result;
                }
            } catch (e) {}
            return _origClientWidthGet.call(thisArg);
        }, 'get clientWidth');

        Object.defineProperty(Element.prototype, 'clientWidth', {
            get: clientWidthGetterProxy,
            configurable: true,
            enumerable: true
        });
    }

    // ========== getBoundingClientRect Hook with DPR scaling ==========
    const _origGetBCR = Element.prototype.getBoundingClientRect;

    // Helper to check if element is MathML (for fingerprint.com mathML detection)
    const isMathMLElement = (el) => {
        if (!el) return false;
        // Check namespace (MathML has its own namespace)
        if (el.namespaceURI === 'http://www.w3.org/1998/Math/MathML') return true;
        // Check tag name as fallback
        const tag = (el.tagName || '').toLowerCase();
        return tag === 'math' || tag.startsWith('m') && ['mi', 'mo', 'mn', 'ms', 'mtext', 'mrow', 'mfrac', 'msqrt', 'mroot', 'msub', 'msup', 'msubsup', 'munder', 'mover', 'munderover', 'mmultiscripts', 'mtable', 'mtr', 'mtd', 'mspace', 'mphantom', 'mfenced', 'menclose', 'mpadded', 'merror', 'mstyle', 'mglyph', 'maligngroup', 'malignmark', 'mprescripts', 'none'].includes(tag);
    };

    // Helper to create DPR-scaled DOMRect-like object
    const scaleDOMRect = (result, scale) => {
        return {
            x: result.x,
            y: result.y,
            top: result.top,
            bottom: result.bottom,
            left: result.left,
            right: result.right,
            width: result.width * scale,
            height: result.height * scale,
            toJSON: function() {
                return { x: this.x, y: this.y, top: this.top, bottom: this.bottom,
                         left: this.left, right: this.right, width: this.width, height: this.height };
            }
        };
    };

    // Create Proxy-based getBoundingClientRect that passes introspection tests
    const getBCRProxy = createNativeProxy(_origGetBCR, (target, thisArg, args) => {
        try {
            // NOTE: getBoundingClientRect already returns CSS pixels, NOT device pixels.
            // DPR scaling should NOT be applied here - only to offsetWidth/offsetHeight.
            // This was verified: emoji/mathML were correct before DPR fix, broken after.

            // CRITICAL: Handle MathML elements for fingerprint.com mathML detection
            // MathML elements are NOT HTMLElements, so they need separate handling
            if (isMathMLElement(thisArg)) {
                return target.call(thisArg);
            }

            if (thisArg instanceof HTMLElement) {
                const style = _origGetCS.call(window, thisArg);
                const fontFamily = style.fontFamily || '';

                if (containsMacFont(fontFamily)) {
                    const text = thisArg.textContent || thisArg.innerText || '';
                    if (text.length > 0 && text.length <= 100) {
                        const origInline = thisArg.style.fontFamily;
                        thisArg.style.fontFamily = filterMacFonts(fontFamily);
                        const result = target.call(thisArg);
                        thisArg.style.fontFamily = origInline;
                        return result;
                    }
                }
                return target.call(thisArg);
            }

            return target.call(thisArg);
        } catch (e) {}
        return target.call(thisArg);
    });

    // Install Proxy - passes all introspection tests automatically
    Object.defineProperty(Element.prototype, 'getBoundingClientRect', {
        value: getBCRProxy, writable: true, enumerable: false, configurable: true
    });
})();
)";

    return ss.str();
}

std::string FontSpoofer::GenerateFontAPIHooks(const Config& config) {
    std::stringstream ss;

    ss << R"(
// ============================================================
// Font API Hooks - document.fonts, queryLocalFonts
// CRITICAL: Must report fake fonts as available for consistency
// ============================================================
(function() {
    'use strict';

    // Check if font string contains macOS-exclusive fonts
    // FIXED: Use exact font name matching instead of substring matching
    // This prevents false positives like "Monaco" matching "monaco" in Windows fonts
    const containsMacFont = (fontStr) => {
        if (!fontStr || !__fontConfig.blockMacFonts) return false;
        // Extract individual font names from CSS font-family string
        const fonts = fontStr.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        // Check if any extracted font exactly matches a macOS-exclusive font
        return fonts.some(font => __fontMacOSExclusive.includes(font));
    };

    // Parse font family name from CSS font string (e.g., "12px Arial" -> "Arial")
    const extractFontFamily = (fontStr) => {
        if (!fontStr) return null;
        // CSS font format: [style] [variant] [weight] [size/line-height] family
        // Extract the family part (after the size)
        const parts = fontStr.trim().split(/\s+/);
        let familyStart = 0;
        for (let i = 0; i < parts.length; i++) {
            // Size usually contains digits and unit (px, pt, em, etc.)
            if (/^\d/.test(parts[i]) || /^[\d.]+(px|pt|em|rem|%|vh|vw)/.test(parts[i])) {
                familyStart = i + 1;
                // Handle size/line-height format like "12px/1.5"
                if (parts[i].includes('/')) {
                    familyStart = i + 1;
                }
                break;
            }
        }
        if (familyStart < parts.length) {
            // Join remaining parts as font family (may have spaces)
            const family = parts.slice(familyStart).join(' ');
            // Get first font family (before comma)
            const firstFamily = family.split(',')[0].replace(/['"]/g, '').trim();
            return firstFamily;
        }
        // Fallback: treat entire string as font family
        return fontStr.split(',')[0].replace(/['"]/g, '').trim();
    };

    // Check if font is in our fake installed fonts list
    const isFakeInstalledFont = (fontName) => {
        if (!fontName) return false;
        const lower = fontName.toLowerCase();
        // Remove quotes and normalize
        const normalized = lower.replace(/['"]/g, '').trim();
        return __fakeInstalledFonts.has(normalized);
    };

    // ========== document.fonts.check() Hook ==========
    // CRITICAL: This must return TRUE for fonts in __fakeInstalledFonts
    // browserleaks.com uses this as one of the font detection methods
    if (typeof document.fonts !== 'undefined' && document.fonts.check) {
        const _origCheck = document.fonts.check.bind(document.fonts);

        document.fonts.check = function(font, text) {
            // Extract the font family from the font string
            const fontFamily = extractFontFamily(font);

            // If font contains macOS-exclusive font and we're blocking, return false
            if (containsMacFont(font)) {
                return false;
            }

            // CRITICAL: If font is in our fake installed fonts list, return TRUE
            // This makes browserleaks.com think the font is installed
            if (fontFamily && isFakeInstalledFont(fontFamily)) {
                return true;
            }

            // CRITICAL FIX: For fonts NOT in our fake list and NOT generic fallbacks,
            // return FALSE to hide host-installed fonts from target profile
            // This prevents fonts like Menlo (macOS host) from appearing on Windows profile
            const lower = (fontFamily || '').toLowerCase();
            const genericFonts = ['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy', 'system-ui'];
            if (!genericFonts.includes(lower)) {
                // Font is not in fake list and not a generic fallback - hide it
                return false;
            }

            // For generic fallbacks, call the original
            return _origCheck(font, text);
        };

        // Register with global registry for native toString
        const _owl = Symbol.for('owl');
        if (window[_owl] && window[_owl].registerNative) {
            window[_owl].registerNative(document.fonts.check, 'check');
        }
    }

    // ========== document.fonts.load() Hook ==========
    if (typeof document.fonts !== 'undefined' && document.fonts.load) {
        const _origLoad = document.fonts.load.bind(document.fonts);

        document.fonts.load = function(font, text) {
            // Extract font family
            const fontFamily = extractFontFamily(font);

            // If font contains macOS-exclusive font, reject
            if (containsMacFont(font)) {
                return Promise.reject(new DOMException('Font not available', 'NetworkError'));
            }

            // If font is in our fake installed list, resolve successfully
            // (pretend it loaded even though we're using system fallback)
            if (fontFamily && isFakeInstalledFont(fontFamily)) {
                return Promise.resolve([]);
            }

            return _origLoad(font, text);
        };

        // Register with global registry for native toString
        const _owl = Symbol.for('owl');
        if (window[_owl] && window[_owl].registerNative) {
            window[_owl].registerNative(document.fonts.load, 'load');
        }
    }

    // ========== queryLocalFonts() Hook ==========
    // Return a list of fonts matching our target OS profile
    if (typeof window.queryLocalFonts === 'function') {
        const _origQueryLocalFonts = window.queryLocalFonts.bind(window);

        window.queryLocalFonts = async function(options) {
            try {
                // Get actual fonts as a template for structure
                const realFonts = await _origQueryLocalFonts(options);

                // Build fake font list from our __fakeInstalledFonts
                // Use structure from real fonts but with our fake font names
                const fakeFontList = [];
                const fakeInstalledArray = Array.from(__fakeInstalledFonts);

                for (const fontName of fakeInstalledArray) {
                    // Create FontData-like object
                    // Structure: { family: string, fullName: string, postscriptName: string, style: string }
                    fakeFontList.push({
                        family: fontName,
                        fullName: fontName,
                        postscriptName: fontName.replace(/\s+/g, '-'),
                        style: 'Regular'
                    });
                }

                return fakeFontList;
            } catch (e) {
                throw e;
            }
        };

        // Register with global registry for native toString
        const _owl = Symbol.for('owl');
        if (window[_owl] && window[_owl].registerNative) {
            window[_owl].registerNative(window.queryLocalFonts, 'queryLocalFonts');
        }
    }
})();
)";

    return ss.str();
}

std::string FontSpoofer::GenerateCSSStyleHooks(const Config& config) {
    std::stringstream ss;

    ss << R"(
// ============================================================
// CSS Style Property Hooks - fontFamily, font, cssText, setProperty
// ============================================================
(function() {
    'use strict';

    // Check if font string contains macOS-exclusive fonts
    // FIXED: Use exact font name matching instead of substring matching
    // This prevents false positives like "Monaco" matching "monaco" in Windows fonts
    const containsMacFont = (fontStr) => {
        if (!fontStr || !__fontConfig.blockMacFonts) return false;
        // Extract individual font names from CSS font-family string
        const fonts = fontStr.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        // Check if any extracted font exactly matches a macOS-exclusive font
        return fonts.some(font => __fontMacOSExclusive.includes(font));
    };

    // Filter macOS fonts from font-family string
    const filterMacFonts = (fontFamily) => {
        if (!fontFamily) return fontFamily;
        const fonts = fontFamily.split(',').map(f => f.trim());
        const filtered = fonts.filter(f => {
            const name = f.replace(/['"]/g, '').toLowerCase();
            return !__fontMacOSExclusive.includes(name);
        });
        return filtered.length > 0 ? filtered.join(', ') : 'sans-serif';
    };

    const styleProto = CSSStyleDeclaration.prototype;

    // ========== fontFamily Setter Hook ==========
    const _origFontFamilyDesc = Object.getOwnPropertyDescriptor(styleProto, 'fontFamily');
    if (_origFontFamilyDesc && _origFontFamilyDesc.set) {
        const _origFontFamilySet = _origFontFamilyDesc.set;

        Object.defineProperty(styleProto, 'fontFamily', {
            get: _origFontFamilyDesc.get,
            set: function(value) {
                if (value && containsMacFont(value)) {
                    value = filterMacFonts(value);
                }
                return _origFontFamilySet.call(this, value);
            },
            configurable: true,
            enumerable: true
        });
    }

    // ========== font Setter Hook ==========
    const _origFontDesc = Object.getOwnPropertyDescriptor(styleProto, 'font');
    if (_origFontDesc && _origFontDesc.set) {
        const _origFontSet = _origFontDesc.set;

        Object.defineProperty(styleProto, 'font', {
            get: _origFontDesc.get,
            set: function(value) {
                if (value && containsMacFont(value)) {
                    value = filterMacFonts(value);
                }
                return _origFontSet.call(this, value);
            },
            configurable: true,
            enumerable: true
        });
    }

    // ========== cssText Setter Hook ==========
    const _origCssTextDesc = Object.getOwnPropertyDescriptor(styleProto, 'cssText');
    if (_origCssTextDesc && _origCssTextDesc.set) {
        const _origCssTextSet = _origCssTextDesc.set;

        Object.defineProperty(styleProto, 'cssText', {
            get: _origCssTextDesc.get,
            set: function(value) {
                if (value && containsMacFont(value)) {
                    // Replace macOS fonts in the entire cssText
                    value = value.replace(/font-family\s*:\s*([^;]+)/gi, (match, families) => {
                        return 'font-family: ' + filterMacFonts(families);
                    });
                }
                return _origCssTextSet.call(this, value);
            },
            configurable: true,
            enumerable: true
        });
    }

    // ========== setProperty Hook ==========
    // Using Proxy to pass all introspection tests
    const _owl = Symbol.for('owl');
    const createNativeProxy = window[_owl]?.createNativeProxy || ((orig, handler) => new Proxy(orig, { apply: handler }));

    const _origSetProperty = CSSStyleDeclaration.prototype.setProperty;

    const setPropertyProxy = createNativeProxy(_origSetProperty, (target, thisArg, args) => {
        let [prop, value, priority] = args;
        if ((prop === 'font-family' || prop === 'font') && value && containsMacFont(value)) {
            value = filterMacFonts(value);
        }
        return target.call(thisArg, prop, value, priority);
    });

    Object.defineProperty(CSSStyleDeclaration.prototype, 'setProperty', {
        value: setPropertyProxy, writable: true, enumerable: false, configurable: true
    });
})();
)";

    return ss.str();
}

std::string FontSpoofer::GenerateComputedStyleHook(const Config& config) {
    std::stringstream ss;

    ss << R"(
// ============================================================
// getComputedStyle Hook - Returns filtered font-family
// ============================================================
(function() {
    'use strict';

    // Check if font string contains macOS-exclusive fonts
    // FIXED: Use exact font name matching instead of substring matching
    // This prevents false positives like "Monaco" matching "monaco" in Windows fonts
    const containsMacFont = (fontStr) => {
        if (!fontStr || !__fontConfig.blockMacFonts) return false;
        // Extract individual font names from CSS font-family string
        const fonts = fontStr.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        // Check if any extracted font exactly matches a macOS-exclusive font
        return fonts.some(font => __fontMacOSExclusive.includes(font));
    };

    // Filter macOS fonts from font-family string
    const filterMacFonts = (fontFamily) => {
        if (!fontFamily) return fontFamily;
        const fonts = fontFamily.split(',').map(f => f.trim());
        const filtered = fonts.filter(f => {
            const name = f.replace(/['"]/g, '').toLowerCase();
            return !__fontMacOSExclusive.includes(name);
        });
        return filtered.length > 0 ? filtered.join(', ') : 'sans-serif';
    };

    // Normalize font names (macOS names -> Windows names)
    // CRITICAL: Multi-word font names must be quoted in CSS computed style
    const normalizeFontNames = (fontFamily) => {
        if (!fontFamily) return fontFamily;
        let normalized = fontFamily;
        for (const [macFont, winFont] of Object.entries(__fontNameMap)) {
            const regex = new RegExp('\\b' + macFont + '\\b', 'gi');
            // If replacement contains spaces, wrap in quotes (CSS standard)
            const replacement = winFont.includes(' ') ? '"' + winFont + '"' : winFont;
            normalized = normalized.replace(regex, replacement);
        }
        return normalized;
    };

    // Store original in Symbol namespace (invisible to getOwnPropertyNames)
    const _owl = Symbol.for('owl');
    if (window[_owl]) {
        if (!window[_owl].font) window[_owl].font = {};
        if (!window[_owl].font.origGetComputedStyle) {
            window[_owl].font.origGetComputedStyle = window.getComputedStyle;
        }
    }
    const _origGetComputedStyle = (window[_owl] && window[_owl].font) ? window[_owl].font.origGetComputedStyle : window.getComputedStyle;

    window.getComputedStyle = function(element, pseudoElt) {
        const style = _origGetComputedStyle.call(window, element, pseudoElt);

        // Wrap with Proxy to filter font-family and normalize names
        return new Proxy(style, {
            get(target, prop) {
                const value = target[prop];

                // Filter fontFamily property
                if (prop === 'fontFamily' || prop === 'font-family') {
                    if (typeof value === 'string') {
                        let result = value;
                        // First filter macOS fonts (only for non-macOS targets)
                        if (containsMacFont(result)) {
                            result = filterMacFonts(result);
                        }
                        // Normalize font names ONLY for non-macOS targets
                        // macOS uses "Times", Windows uses "Times New Roman"
                        if (__fontConfig.targetOS !== 'macOS') {
                            result = normalizeFontNames(result);
                        }
                        return result;
                    }
                }

                // Wrap getPropertyValue method
                if (prop === 'getPropertyValue') {
                    return function(propName) {
                        const val = target.getPropertyValue(propName);
                        if ((propName === 'font-family' || propName === 'font') &&
                            typeof val === 'string') {
                            let result = val;
                            if (containsMacFont(result)) {
                                result = filterMacFonts(result);
                            }
                            // Normalize font names ONLY for non-macOS targets
                            if (__fontConfig.targetOS !== 'macOS') {
                                result = normalizeFontNames(result);
                            }
                            return result;
                        }
                        return val;
                    };
                }

                // Return functions bound to target
                if (typeof value === 'function') {
                    return value.bind(target);
                }

                return value;
            }
        });
    };

    // Register with global registry for native toString (NO direct toString property!)
    // Note: _owl already defined at line 1465 in this IIFE
    if (window[_owl] && window[_owl].registerNative) {
        window[_owl].registerNative(window.getComputedStyle, 'getComputedStyle');
    }
})();
)";

    return ss.str();
}

std::string FontSpoofer::GenerateSetAttributeHook(const Config& config) {
    std::stringstream ss;

    ss << R"(
// ============================================================
// setAttribute Hook - Intercept style attribute changes
// ============================================================
(function() {
    'use strict';

    // Check if font string contains macOS-exclusive fonts
    // FIXED: Use exact font name matching instead of substring matching
    // This prevents false positives like "Monaco" matching "monaco" in Windows fonts
    const containsMacFont = (fontStr) => {
        if (!fontStr || !__fontConfig.blockMacFonts) return false;
        // Extract individual font names from CSS font-family string
        const fonts = fontStr.toLowerCase().split(',').map(f => f.replace(/['"]/g, '').trim());
        // Check if any extracted font exactly matches a macOS-exclusive font
        return fonts.some(font => __fontMacOSExclusive.includes(font));
    };

    // Filter macOS fonts from font-family string
    const filterMacFonts = (fontFamily) => {
        if (!fontFamily) return fontFamily;
        const fonts = fontFamily.split(',').map(f => f.trim());
        const filtered = fonts.filter(f => {
            const name = f.replace(/['"]/g, '').toLowerCase();
            return !__fontMacOSExclusive.includes(name);
        });
        return filtered.length > 0 ? filtered.join(', ') : 'sans-serif';
    };

    // Using Proxy to pass all introspection tests
    const _owl = Symbol.for('owl');
    const createNativeProxy = window[_owl]?.createNativeProxy || ((orig, handler) => new Proxy(orig, { apply: handler }));

    const _origSetAttribute = Element.prototype.setAttribute;

    const setAttributeProxy = createNativeProxy(_origSetAttribute, (target, thisArg, args) => {
        let [name, value] = args;
        // Intercept style attribute
        if (name.toLowerCase() === 'style' && value && containsMacFont(value)) {
            value = value.replace(/font-family\s*:\s*([^;]+)/gi, (match, families) => {
                return 'font-family: ' + filterMacFonts(families);
            });
            value = value.replace(/font\s*:\s*([^;]+)/gi, (match, fontValue) => {
                if (containsMacFont(fontValue)) {
                    return 'font: ' + filterMacFonts(fontValue);
                }
                return match;
            });
        }
        return target.call(thisArg, name, value);
    });

    Object.defineProperty(Element.prototype, 'setAttribute', {
        value: setAttributeProxy, writable: true, enumerable: false, configurable: true
    });
})();
)";

    return ss.str();
}

} // namespace owl
