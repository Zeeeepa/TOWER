-- VM Profiles Database Schema
-- This file defines the structure for browser fingerprint profiles
--
-- IMPORTANT: This database is encrypted using SQLCipher
-- The encryption key is derived from the browser binary hash

PRAGMA encoding = "UTF-8";

-- Main VM Profiles table
CREATE TABLE IF NOT EXISTS vm_profiles (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT,

    -- OS Properties
    os_name TEXT NOT NULL,           -- Windows, Linux, macOS
    os_version TEXT NOT NULL,
    os_platform TEXT NOT NULL,       -- Win32, Linux x86_64, MacIntel
    os_oscpu TEXT DEFAULT '',
    os_app_version TEXT NOT NULL,
    os_max_touch_points INTEGER DEFAULT 0,

    -- Browser Properties
    browser_name TEXT NOT NULL,
    browser_version TEXT NOT NULL,   -- Will be replaced by {{BROWSER_VERSION}}
    browser_vendor TEXT NOT NULL,
    browser_user_agent TEXT NOT NULL,
    browser_app_name TEXT DEFAULT 'Netscape',
    browser_app_code_name TEXT DEFAULT 'Mozilla',
    browser_product TEXT DEFAULT 'Gecko',
    browser_product_sub TEXT DEFAULT '20030107',
    browser_build_id TEXT DEFAULT '',
    browser_webdriver INTEGER DEFAULT 0,
    browser_pdf_viewer_enabled INTEGER DEFAULT 1,
    browser_cookies_enabled INTEGER DEFAULT 1,
    browser_java_enabled INTEGER DEFAULT 0,

    -- CPU Properties
    cpu_hardware_concurrency INTEGER DEFAULT 8,
    cpu_device_memory INTEGER DEFAULT 8,
    cpu_architecture TEXT DEFAULT 'x86_64',

    -- GPU Properties
    gpu_vendor TEXT NOT NULL,
    gpu_renderer TEXT NOT NULL,
    gpu_unmasked_vendor TEXT NOT NULL,
    gpu_unmasked_renderer TEXT NOT NULL,
    gpu_webgl_version TEXT DEFAULT 'WebGL 1.0 (OpenGL ES 2.0 Chromium)',
    gpu_webgl2_version TEXT DEFAULT 'WebGL 2.0 (OpenGL ES 3.0 Chromium)',
    gpu_shading_language TEXT DEFAULT 'WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)',
    gpu_max_texture_size INTEGER DEFAULT 16384,
    gpu_max_cube_map_texture_size INTEGER DEFAULT 16384,
    gpu_max_render_buffer_size INTEGER DEFAULT 16384,
    gpu_max_vertex_attribs INTEGER DEFAULT 16,
    gpu_max_vertex_uniform_vectors INTEGER DEFAULT 4096,
    gpu_max_vertex_texture_units INTEGER DEFAULT 16,
    gpu_max_varying_vectors INTEGER DEFAULT 30,
    gpu_max_fragment_uniform_vectors INTEGER DEFAULT 1024,
    gpu_max_texture_units INTEGER DEFAULT 16,
    gpu_max_combined_texture_units INTEGER DEFAULT 64,
    gpu_max_viewport_dims_w INTEGER DEFAULT 16384,
    gpu_max_viewport_dims_h INTEGER DEFAULT 16384,
    gpu_aliased_line_width_min REAL DEFAULT 1.0,
    gpu_aliased_line_width_max REAL DEFAULT 1.0,
    gpu_aliased_point_size_min REAL DEFAULT 1.0,
    gpu_aliased_point_size_max REAL DEFAULT 1024.0,
    gpu_max_samples INTEGER DEFAULT 16,
    gpu_max_anisotropy REAL DEFAULT 16.0,
    gpu_renderer_hash_seed TEXT NOT NULL,     -- 64-bit hex string

    -- Shader Precision (stored as JSON)
    gpu_shader_precision TEXT DEFAULT '{"vh":[127,127,23],"vm":[127,127,23],"vl":[127,127,23],"fh":[127,127,23],"fm":[127,127,23],"fl":[127,127,23]}',

    -- WebGL Extensions (comma-separated)
    gpu_webgl_extensions TEXT,

    -- Screen Properties
    screen_width INTEGER DEFAULT 1920,
    screen_height INTEGER DEFAULT 1080,
    screen_avail_width INTEGER DEFAULT 1920,
    screen_avail_height INTEGER DEFAULT 1040,
    screen_color_depth INTEGER DEFAULT 24,
    screen_pixel_depth INTEGER DEFAULT 24,
    screen_device_pixel_ratio REAL DEFAULT 1.0,
    screen_orientation_type TEXT DEFAULT 'landscape-primary',
    screen_orientation_angle INTEGER DEFAULT 0,

    -- Audio Properties
    audio_sample_rate INTEGER DEFAULT 48000,
    audio_max_channel_count INTEGER DEFAULT 2,
    audio_number_of_inputs INTEGER DEFAULT 0,
    audio_number_of_outputs INTEGER DEFAULT 1,
    audio_channel_count INTEGER DEFAULT 2,
    audio_channel_count_mode TEXT DEFAULT 'explicit',
    audio_channel_interpretation TEXT DEFAULT 'speakers',
    audio_base_latency REAL DEFAULT 0.01,
    audio_output_latency REAL DEFAULT 0.02,
    audio_hash_seed TEXT NOT NULL,           -- 64-bit hex string

    -- Canvas Properties
    canvas_hash_seed TEXT NOT NULL,          -- 64-bit hex string
    canvas_apply_noise INTEGER DEFAULT 0,
    canvas_noise_intensity REAL DEFAULT 0.0,

    -- Fonts (comma-separated)
    fonts_installed TEXT,
    fonts_default_serif TEXT DEFAULT 'Times New Roman',
    fonts_default_sans_serif TEXT DEFAULT 'Arial',
    fonts_default_monospace TEXT DEFAULT 'Consolas',

    -- Timezone (default - will be overridden by GeoIP)
    timezone_iana_name TEXT DEFAULT 'America/New_York',
    timezone_offset_minutes INTEGER DEFAULT 300,
    timezone_has_dst INTEGER DEFAULT 1,

    -- Language
    language_primary TEXT DEFAULT 'en-US',
    language_list TEXT DEFAULT 'en-US,en',   -- comma-separated

    -- Network
    network_connection_type TEXT DEFAULT 'wifi',
    network_downlink REAL DEFAULT 10.0,
    network_rtt INTEGER DEFAULT 50,
    network_effective_type TEXT DEFAULT '4g',
    network_save_data INTEGER DEFAULT 0,

    -- Media (comma-separated codec strings)
    media_audio_codecs TEXT,
    media_video_codecs TEXT,
    media_has_microphone INTEGER DEFAULT 1,
    media_has_camera INTEGER DEFAULT 1,
    media_has_speakers INTEGER DEFAULT 1,

    -- Permissions
    permissions_geolocation TEXT DEFAULT 'prompt',
    permissions_notifications TEXT DEFAULT 'prompt',
    permissions_camera TEXT DEFAULT 'prompt',
    permissions_microphone TEXT DEFAULT 'prompt',
    permissions_midi TEXT DEFAULT 'prompt',
    permissions_clipboard_read TEXT DEFAULT 'prompt',
    permissions_clipboard_write TEXT DEFAULT 'granted',

    -- Client Hints
    client_hints_enabled INTEGER DEFAULT 1,
    client_hints_sec_ch_ua TEXT,
    client_hints_sec_ch_ua_platform TEXT,
    client_hints_sec_ch_ua_mobile TEXT DEFAULT '?0',
    client_hints_sec_ch_ua_full_version TEXT,
    client_hints_sec_ch_ua_arch TEXT,
    client_hints_sec_ch_ua_bitness TEXT,
    client_hints_sec_ch_ua_model TEXT DEFAULT '',

    -- Storage
    storage_quota INTEGER DEFAULT 2147483648,
    storage_usage INTEGER DEFAULT 0,

    -- Battery
    battery_enabled INTEGER DEFAULT 0,
    battery_level REAL DEFAULT 1.0,
    battery_charging INTEGER DEFAULT 1,
    battery_charging_time REAL DEFAULT 0,
    battery_discharging_time REAL DEFAULT -1,  -- -1 = infinity

    -- Metadata
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Index for fast OS/Browser lookups
CREATE INDEX IF NOT EXISTS idx_vm_os ON vm_profiles(os_name);
CREATE INDEX IF NOT EXISTS idx_vm_browser ON vm_profiles(browser_name);
CREATE INDEX IF NOT EXISTS idx_vm_os_browser ON vm_profiles(os_name, browser_name);

-- Configuration table for browser version and other settings
CREATE TABLE IF NOT EXISTS config (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    description TEXT
);

-- Insert default configuration
INSERT OR REPLACE INTO config (key, value, description) VALUES
    ('browser_version', '143', 'Chrome browser version (e.g., 143 for Chrome 143)'),
    ('browser_version_full', '143.0.0.0', 'Full browser version string'),
    ('schema_version', '1', 'Database schema version');
