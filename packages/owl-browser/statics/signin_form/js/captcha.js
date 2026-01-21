/**
 * Text-based CAPTCHA Manager
 * Loads random CAPTCHA images and validates user input
 * Filename = correct answer for validation
 */

class CaptchaManager {
    constructor() {
        this.metadata = null;
        this.currentCaptcha = null;
        this.captchaImage = document.getElementById('captchaImage');
        this.captchaInput = document.getElementById('captchaInput');
        this.captchaError = document.getElementById('captchaError');
        this.refreshBtn = document.getElementById('refreshCaptcha');

        this.init();
    }

    async init() {
        console.log('🔒 Initializing CAPTCHA system...');

        // Load metadata
        await this.loadMetadata();

        // Load first CAPTCHA
        this.loadRandomCaptcha();

        // Setup event listeners
        this.setupEventListeners();

        console.log('✓ CAPTCHA system initialized');
    }

    async loadMetadata() {
        try {
            const response = await fetch('images/captcha/metadata.json');
            this.metadata = await response.json();
            console.log(`✓ Loaded ${this.metadata.total_count} CAPTCHA images`);
        } catch (error) {
            console.error('Failed to load CAPTCHA metadata:', error);

            // Fallback: if metadata fails, we can still load by trying filenames
            // This is a simple fallback - in production you'd want better error handling
            this.metadata = {
                total_count: 100,
                captchas: [] // Will be empty, but loadRandomCaptcha will handle it
            };
        }
    }

    loadRandomCaptcha() {
        if (!this.metadata || this.metadata.total_count === 0) {
            console.error('No CAPTCHA metadata available');
            return;
        }

        // Get random CAPTCHA from metadata
        const randomIndex = Math.floor(Math.random() * this.metadata.captchas.length);
        this.currentCaptcha = this.metadata.captchas[randomIndex];

        // Load image (filename = the answer)
        const imagePath = `images/captcha/${this.currentCaptcha}.png`;
        this.captchaImage.src = imagePath;

        // Clear input and error
        this.captchaInput.value = '';
        this.captchaError.textContent = '';
        this.captchaInput.classList.remove('error');

        console.log(`📸 Loaded new CAPTCHA (answer in filename)`);
    }

    setupEventListeners() {
        // Refresh button
        this.refreshBtn.addEventListener('click', () => {
            console.log('🔄 Refreshing CAPTCHA...');
            this.loadRandomCaptcha();
        });

        // Input validation on blur
        this.captchaInput.addEventListener('blur', () => {
            this.clearError();
        });

        // Auto-uppercase input
        this.captchaInput.addEventListener('input', (e) => {
            e.target.value = e.target.value.toUpperCase();
        });
    }

    validate() {
        const userInput = this.captchaInput.value.trim().toUpperCase();

        if (!userInput) {
            this.showError('Please enter the CAPTCHA characters');
            return false;
        }

        if (userInput !== this.currentCaptcha) {
            console.log(`❌ CAPTCHA incorrect: "${userInput}" !== "${this.currentCaptcha}"`);
            this.showError('CAPTCHA is incorrect. Please try again.');
            this.loadRandomCaptcha(); // Load new CAPTCHA on failure
            return false;
        }

        console.log('✅ CAPTCHA validated successfully');
        this.clearError();
        return true;
    }

    showError(message) {
        this.captchaError.textContent = message;
        this.captchaInput.classList.add('error');
    }

    clearError() {
        this.captchaError.textContent = '';
        this.captchaInput.classList.remove('error');
    }

    reset() {
        this.loadRandomCaptcha();
    }
}

// Initialize CAPTCHA when DOM is ready
let captchaManager;
document.addEventListener('DOMContentLoaded', () => {
    captchaManager = new CaptchaManager();
});
