/**
 * Sign In Form Handler
 * Handles form submission, validation, and CAPTCHA integration
 */

document.addEventListener('DOMContentLoaded', () => {
    const form = document.getElementById('signinForm');
    const usernameInput = document.getElementById('username');
    const passwordInput = document.getElementById('password');
    const togglePasswordBtn = document.getElementById('togglePassword');
    const submitBtn = document.getElementById('submitBtn');
    const messageBox = document.getElementById('messageBox');

    // Toggle password visibility
    togglePasswordBtn.addEventListener('click', () => {
        const type = passwordInput.type === 'password' ? 'text' : 'password';
        passwordInput.type = type;

        // Update icon (optional - you could change the SVG here)
        console.log(`Password visibility: ${type === 'text' ? 'visible' : 'hidden'}`);
    });

    // Form submission
    form.addEventListener('submit', async (e) => {
        e.preventDefault();

        console.log('📝 Form submission started...');

        // Clear previous messages
        hideMessage();

        // Validate CAPTCHA first
        if (!captchaManager.validate()) {
            console.log('❌ Form submission blocked: CAPTCHA validation failed');
            return;
        }

        // Get form data
        const formData = {
            username: usernameInput.value.trim(),
            password: passwordInput.value,
            rememberMe: document.getElementById('rememberMe').checked
        };

        // Basic validation
        if (!formData.username) {
            showMessage('Please enter your username or email', 'error');
            usernameInput.focus();
            return;
        }

        if (!formData.password) {
            showMessage('Please enter your password', 'error');
            passwordInput.focus();
            return;
        }

        // Simulate sign-in process
        console.log('🔐 Authenticating user...');
        console.log('Username:', formData.username);
        console.log('Remember me:', formData.rememberMe);

        // Disable submit button during processing
        submitBtn.disabled = true;
        submitBtn.querySelector('span').textContent = 'Signing in...';

        // Simulate API call
        setTimeout(() => {
            // For demo purposes, accept any credentials
            console.log('✅ Sign-in successful!');

            showMessage('Sign-in successful! Redirecting...', 'success');

            // Reset form
            setTimeout(() => {
                form.reset();
                captchaManager.reset();
                submitBtn.disabled = false;
                submitBtn.querySelector('span').textContent = 'Sign In';

                // In real app, redirect to dashboard
                console.log('→ Would redirect to dashboard here');
            }, 2000);

        }, 1500);
    });

    // Helper functions
    function showMessage(message, type) {
        messageBox.textContent = message;
        messageBox.className = `message-box ${type}`;
    }

    function hideMessage() {
        messageBox.textContent = '';
        messageBox.className = 'message-box';
    }

    // Form field focus effects
    const inputs = form.querySelectorAll('input[type="text"], input[type="password"]');
    inputs.forEach(input => {
        input.addEventListener('focus', () => {
            input.parentElement.style.transform = 'scale(1.01)';
            input.parentElement.style.transition = 'transform 0.2s ease';
        });

        input.addEventListener('blur', () => {
            input.parentElement.style.transform = 'scale(1)';
        });
    });

    console.log('✓ Sign-in form initialized');
});
