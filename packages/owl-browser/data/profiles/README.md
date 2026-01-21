# VM Profiles Database

This directory contains browser fingerprint profiles used for virtual machine emulation. The profiles are stored in an encrypted SQLite database (SQLCipher) and compiled from SQL template files.

## Files

| File | Description |
|------|-------------|
| `schema.sql` | Database schema definition |
| `profiles.sql` | Profile data (uses templates) |
| `vm_profiles.db` | Compiled encrypted database (generated) |

## Changing Browser Version

The browser version is templated using `{{BROWSER_VERSION}}` and `{{BROWSER_VERSION_FULL}}` placeholders. To change the version:

### Option 1: Command Line Argument

```bash
# Set browser version to Chrome 143
npm run build:profiles -- --browser-version 143

# Or with full version string
npm run build:profiles -- --browser-version 143.0.6312.0
```

### Option 2: Modify Default in schema.sql

Edit the config table insert in `schema.sql`:

```sql
INSERT OR REPLACE INTO config (key, value, description) VALUES
    ('browser_version', '143', 'Chrome browser version'),
    ('browser_version_full', '143.0.0.0', 'Full browser version string'),
    ('schema_version', '1', 'Database schema version');
```

Then rebuild:

```bash
npm run build:profiles
```

## Adding New Unique Profiles

### Step 1: Choose a Unique ID

Profile IDs follow the format: `{os}-{gpu}-{browser}{version}`

Examples:
- `win10-intel-uhd620-chrome142`
- `ubuntu2204-nvidia-rtx3070-chrome142`
- `macos14-m2-chrome142`

### Step 2: Copy an Existing Profile Block

Open `profiles.sql` and copy an existing profile that's similar to your target device. For example, to add a Windows 11 laptop with Intel Arc GPU:

```sql
-- Windows 11 + Intel Arc A770 + Chrome
INSERT INTO vm_profiles (
    id, name, description,
    os_name, os_version, os_platform, os_oscpu, os_app_version, os_max_touch_points,
    browser_name, browser_version, browser_vendor, browser_user_agent,
    cpu_hardware_concurrency, cpu_device_memory, cpu_architecture,
    gpu_vendor, gpu_renderer, gpu_unmasked_vendor, gpu_unmasked_renderer,
    -- ... other fields
    gpu_renderer_hash_seed,
    audio_hash_seed, canvas_hash_seed,
    -- ... remaining fields
) VALUES (
    'win11-intel-arc-a770-chrome{{BROWSER_VERSION}}',
    'Windows 11 - Intel Arc A770 - Chrome {{BROWSER_VERSION}}',
    'Desktop with Intel Arc discrete GPU',
    'Windows', '11.0', 'Win32', '',
    '5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/{{BROWSER_VERSION_FULL}} Safari/537.36',
    0,
    'Chrome', '{{BROWSER_VERSION_FULL}}', 'Google Inc.',
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/{{BROWSER_VERSION_FULL}} Safari/537.36',
    -- ... values
);
```

### Step 3: Generate Unique Hash Seeds

Each profile **must** have unique hash seeds for fingerprint differentiation:
- `gpu_renderer_hash_seed` - 64-bit hex string for WebGL rendering
- `audio_hash_seed` - 64-bit hex string for AudioContext fingerprint
- `canvas_hash_seed` - 64-bit hex string for canvas fingerprint

Generate random 64-bit hex values using:

```bash
# Python
python3 -c "import secrets; print(secrets.token_hex(8).upper())"

# OpenSSL
openssl rand -hex 8 | tr '[:lower:]' '[:upper:]'
```

### Step 4: Update Profile Fields

Configure all required fields based on real device characteristics. **Use realistic values** - avoid unrealistic configurations that would stand out as suspicious (e.g., a laptop with 128GB RAM or a budget PC with 32 cores).

#### System Resource Guidelines

**IMPORTANT**: Use realistic system resources that match the device type:

| Device Type | CPU Cores | Device Memory | Typical GPU |
|-------------|-----------|---------------|-------------|
| Budget laptop | 4-8 | 4-8 GB | Intel UHD, AMD Vega 8 |
| Business laptop | 8-12 | 8-16 GB | Intel Iris Xe, Intel UHD |
| Gaming laptop | 8-16 | 16-32 GB | NVIDIA RTX 3060-4070, AMD RX 6600-7800 |
| Desktop workstation | 8-24 | 16-64 GB | NVIDIA RTX 3070-4090, AMD RX 6700-7900 |
| Mac (Apple Silicon) | 8-24 | 8-32 GB | Apple M1/M2/M3/M4 |

**Bad examples** (avoid these):
- Laptop with 64GB RAM and 4 CPU cores
- Budget Intel UHD 620 with 32GB RAM
- RTX 4090 with only 8GB device memory

#### OS Properties
| Field | Description | Example |
|-------|-------------|---------|
| `os_name` | Operating system name | `Windows`, `Linux`, `macOS` |
| `os_version` | OS version | `10.0`, `22.04`, `14.0` |
| `os_platform` | Platform identifier | `Win32`, `Linux x86_64`, `MacIntel` |
| `os_max_touch_points` | Touch screen support | `0` (none), `10` (touch) |

#### GPU Properties

The GPU renderer string format varies by platform:

**Windows (ANGLE):**
```
gpu_vendor: 'Google Inc. (NVIDIA)'
gpu_renderer: 'ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 Direct3D11 vs_5_0 ps_5_0, D3D11)'
gpu_unmasked_vendor: 'Google Inc. (NVIDIA)'
gpu_unmasked_renderer: 'ANGLE (NVIDIA, NVIDIA GeForce RTX 4070 Direct3D11 vs_5_0 ps_5_0, D3D11)'
```

**Linux (Mesa/Native):**
```
gpu_vendor: 'Google Inc. (NVIDIA Corporation)'
gpu_renderer: 'ANGLE (NVIDIA Corporation, NVIDIA GeForce RTX 4070/PCIe/SSE2, OpenGL 4.5)'
gpu_unmasked_vendor: 'NVIDIA Corporation'
gpu_unmasked_renderer: 'NVIDIA GeForce RTX 4070/PCIe/SSE2'
```

**macOS (Apple Silicon):**
```
gpu_vendor: 'Apple Inc.'
gpu_renderer: 'Apple M4'
gpu_unmasked_vendor: 'Apple Inc.'
gpu_unmasked_renderer: 'Apple M4'
```

| Field | Description | Notes |
|-------|-------------|-------|
| `gpu_vendor` | WebGL vendor | `Google Inc. (NVIDIA)`, `Google Inc. (Intel)`, `Apple Inc.` |
| `gpu_renderer` | WebGL renderer | ANGLE string for Windows/Linux, native for macOS |
| `gpu_unmasked_vendor` | Unmasked vendor | Same as vendor on Windows, real manufacturer on Linux |
| `gpu_unmasked_renderer` | Unmasked renderer | Full ANGLE string on Windows, GPU model on Linux/macOS |
| `gpu_max_texture_size` | Max texture size | `16384` (integrated), `32768` (discrete) |
| `gpu_aliased_point_size_max` | Max point size | `1024.0` (Intel), `2047.0` (NVIDIA), `8192.0` (AMD) |
| `gpu_max_samples` | Max MSAA samples | `4-8` (integrated), `16-32` (discrete) |

#### Screen Properties
| Field | Description | Common Values |
|-------|-------------|---------------|
| `screen_width` | Screen width in pixels | `1920`, `2560`, `3840` |
| `screen_height` | Screen height in pixels | `1080`, `1440`, `2160` |
| `screen_device_pixel_ratio` | DPI scaling | `1.0`, `1.25`, `2.0` |

### Step 5: Build the Database

```bash
npm run build:profiles
```

### Step 6: Verify the Profile

Check that your profile was added:

```bash
# List all profile IDs
sqlcipher data/profiles/vm_profiles.db <<< "
PRAGMA key = 'YOUR_PASSWORD';
SELECT id FROM vm_profiles ORDER BY id;
"
```

## Profile Categories

### Windows 10/11 Profiles
- Intel integrated (UHD 620, Iris Xe, Core Ultra Arc)
- Intel discrete (Arc A770)
- NVIDIA discrete (GTX 1070, GTX 1660 Ti, RTX 3060, RTX 4060, RTX 4070, RTX 4080, RTX 4090)
- AMD discrete (RX 6700 XT, RX 7800 XT, RX 7900 XT)
- AMD integrated (Vega 8)

### Linux (Ubuntu 22.04/24.04/25.04) Profiles
- Intel integrated (UHD 620, UHD 770)
- Intel discrete (Arc A750)
- NVIDIA discrete (RTX 3070, RTX 3080 Ti, RTX 4070 Ti)
- AMD discrete (RX 6600, RX 7800 XT)
- AMD integrated (Radeon 780M)

### macOS 13/14/26 Profiles
- Apple Silicon (M1, M2, M2 Ultra, M3, M3 Pro, M3 Ultra, M4, M4 Pro, M4 Max)
- Intel (Iris Pro)

## Database Schema Reference

See `schema.sql` for the complete field list. Key field categories:

- **OS Properties** - Operating system identification
- **Browser Properties** - Browser identification and capabilities
- **CPU Properties** - Core count, memory, architecture
- **GPU Properties** - WebGL parameters and limits
- **Screen Properties** - Resolution, color depth, DPI
- **Audio Properties** - AudioContext fingerprint parameters
- **Canvas Properties** - Canvas fingerprint noise settings
- **Fonts** - Available font list
- **Network** - Connection type and speed
- **Battery** - Battery status (laptops)
- **Client Hints** - Sec-CH-UA headers

## Build Prerequisites

1. **SQLCipher CLI**:
   ```bash
   # macOS
   brew install sqlcipher

   # Ubuntu/Debian
   sudo apt install sqlcipher
   ```

2. **Environment Variable** in `.env`:
   ```bash
   OWL_VM_PROFILE_DB_PASS="your-secure-password-here"
   ```

   Generate a secure password:
   ```bash
   python3 -c "import secrets; print(secrets.token_hex(32))"
   ```

## Security Notes

- The database is encrypted using SQLCipher with:
  - 4096-byte page size
  - 256,000 PBKDF2 iterations
  - HMAC-SHA512 authentication
  - SHA512-based KDF

- The encryption key is obfuscated in the C++ header file `include/stealth/owl_vm_db_key.h`

- Do not commit the `.env` file or share the database password

## Existing Profile IDs

When adding new profiles, ensure the ID is unique. Here are the existing profile IDs:

### Windows Profiles
| ID | Description |
|----|-------------|
| `win10-intel-uhd620-chrome` | Windows 10 + Intel UHD 620 |
| `win10-nvidia-gtx1660ti-chrome` | Windows 10 + NVIDIA GTX 1660 Ti |
| `win10-nvidia-gtx1070-chrome` | Windows 10 + NVIDIA GTX 1070 |
| `win10-nvidia-rtx2080-chrome` | Windows 10 + NVIDIA RTX 2080 |
| `win10-nvidia-rtx3060-chrome` | Windows 10 + NVIDIA RTX 3060 |
| `win10-nvidia-rtx3080-chrome` | Windows 10 + NVIDIA RTX 3080 |
| `win10-amd-vega8-chrome` | Windows 10 + AMD Vega 8 |
| `win10-intel-iris-xe-chrome` | Windows 10 + Intel Iris Xe |
| `win11-amd-rx6700xt-chrome` | Windows 11 + AMD RX 6700 XT |
| `win11-intel-iris-xe-chrome` | Windows 11 + Intel Iris Xe (Touch) |
| `win11-nvidia-rtx4070-chrome` | Windows 11 + NVIDIA RTX 4070 |
| `win11-24h2-intel-arc-a770-chrome` | Windows 11 24H2 + Intel Arc A770 |
| `win11-24h2-intel-core-ultra-chrome` | Windows 11 24H2 + Intel Core Ultra |
| `win11-24h2-intel-b580-chrome` | Windows 11 24H2 + Intel Arc B580 |
| `win11-24h2-nvidia-rtx3090-chrome` | Windows 11 24H2 + NVIDIA RTX 3090 |
| `win11-24h2-nvidia-rtx4060-chrome` | Windows 11 24H2 + NVIDIA RTX 4060 |
| `win11-24h2-nvidia-rtx4070super-chrome` | Windows 11 24H2 + NVIDIA RTX 4070 Super |
| `win11-24h2-nvidia-rtx4080-chrome` | Windows 11 24H2 + NVIDIA RTX 4080 |
| `win11-24h2-nvidia-rtx4090-chrome` | Windows 11 24H2 + NVIDIA RTX 4090 |
| `win11-24h2-nvidia-rtx5080-chrome` | Windows 11 24H2 + NVIDIA RTX 5080 |
| `win11-24h2-nvidia-rtx5090-chrome` | Windows 11 24H2 + NVIDIA RTX 5090 |
| `win11-24h2-amd-rx6800xt-chrome` | Windows 11 24H2 + AMD RX 6800 XT |
| `win11-24h2-amd-rx7800xt-chrome` | Windows 11 24H2 + AMD RX 7800 XT |
| `win11-24h2-amd-rx7900xt-chrome` | Windows 11 24H2 + AMD RX 7900 XT |
| `win11-24h2-amd-rx9070xt-chrome` | Windows 11 24H2 + AMD RX 9070 XT |

### Linux Profiles
| ID | Description |
|----|-------------|
| `ubuntu2204-intel-uhd620-chrome` | Ubuntu 22.04 + Intel UHD 620 |
| `ubuntu2204-nvidia-rtx3070-chrome` | Ubuntu 22.04 + NVIDIA RTX 3070 |
| `ubuntu2404-amd-rx6600-chrome` | Ubuntu 24.04 + AMD RX 6600 |
| `ubuntu2404-nvidia-rtx4080-chrome` | Ubuntu 24.04 + NVIDIA RTX 4080 |
| `ubuntu2404-amd-rx7900xtx-chrome` | Ubuntu 24.04 + AMD RX 7900 XTX |
| `ubuntu2504-intel-uhd770-chrome` | Ubuntu 25.04 + Intel UHD 770 |
| `ubuntu2504-intel-arc-a750-chrome` | Ubuntu 25.04 + Intel Arc A750 |
| `ubuntu2504-nvidia-rtx3080ti-chrome` | Ubuntu 25.04 + NVIDIA RTX 3080 Ti |
| `ubuntu2504-nvidia-rtx4070ti-chrome` | Ubuntu 25.04 + NVIDIA RTX 4070 Ti |
| `ubuntu2504-nvidia-rtx4090-chrome` | Ubuntu 25.04 + NVIDIA RTX 4090 |
| `ubuntu2504-nvidia-rtx5070-chrome` | Ubuntu 25.04 + NVIDIA RTX 5070 |
| `ubuntu2504-amd-780m-chrome` | Ubuntu 25.04 + AMD Radeon 780M |
| `ubuntu2504-amd-rx7800xt-chrome` | Ubuntu 25.04 + AMD RX 7800 XT |
| `fedora41-nvidia-rtx4070-chrome` | Fedora 41 + NVIDIA RTX 4070 |
| `fedora41-amd-rx7600-chrome` | Fedora 41 + AMD RX 7600 |
| `debian13-nvidia-rtx3060-chrome` | Debian 13 + NVIDIA RTX 3060 |

### macOS Profiles
| ID | Description |
|----|-------------|
| `macos13-m1-chrome` | macOS 13 + Apple M1 |
| `macos14-m2-chrome` | macOS 14 + Apple M2 |
| `macos14-m3-chrome` | macOS 14 + Apple M3 |
| `macos14-intel-iris-chrome` | macOS 14 + Intel Iris Pro |
| `macos15-m3-chrome` | macOS 15 Sequoia + Apple M3 |
| `macos15-m4-chrome` | macOS 15 Sequoia + Apple M4 |
| `macos26-m4-chrome` | macOS 26 + Apple M4 (MacBook Pro 14") |
| `macos26-m4-air15-chrome` | macOS 26 + Apple M4 (MacBook Air 15") |
| `macos26-m4pro-chrome` | macOS 26 + Apple M4 Pro (MacBook Pro 16") |
| `macos26-m4max-chrome` | macOS 26 + Apple M4 Max (MacBook Pro 16" Max) |
| `macos26-m4ultra-chrome` | macOS 26 + Apple M4 Ultra |
| `macos26-m2ultra-chrome` | macOS 26 + Apple M2 Ultra (Mac Pro) |
| `macos26-m3pro-imac-chrome` | macOS 26 + Apple M3 Pro (iMac 27") |
| `macos26-m3ultra-chrome` | macOS 26 + Apple M3 Ultra (Mac Studio) |

## Validation

Run the validation script to check for duplicate hashes:

```bash
python3 data/profiles/validate_profiles.py
```

This will detect:
- Duplicate `audio_hash_seed` values
- Duplicate `canvas_hash_seed` values
- Duplicate `gpu_renderer_hash_seed` values
