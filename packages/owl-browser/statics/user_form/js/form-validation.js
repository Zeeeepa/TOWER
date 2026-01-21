// Form Validation
class FormValidator {
  constructor(formId) {
    this.form = document.getElementById(formId);
    this.fields = {
      firstName: {
        element: document.getElementById('firstName'),
        validators: ['required', 'minLength:2', 'maxLength:50', 'alpha']
      },
      lastName: {
        element: document.getElementById('lastName'),
        validators: ['required', 'minLength:2', 'maxLength:50', 'alpha']
      },
      email: {
        element: document.getElementById('email'),
        validators: ['required', 'email']
      },
      phone: {
        element: document.getElementById('phone'),
        validators: ['phone']
      },
      username: {
        element: document.getElementById('username'),
        validators: ['required', 'minLength:4', 'maxLength:20', 'alphanumeric']
      },
      password: {
        element: document.getElementById('password'),
        validators: ['required', 'minLength:8', 'password']
      },
      confirmPassword: {
        element: document.getElementById('confirmPassword'),
        validators: ['required', 'match:password']
      },
      country: {
        element: document.getElementById('country'),
        validators: ['required']
      },
      terms: {
        element: document.getElementById('terms'),
        validators: ['checked']
      }
    };

    this.init();
  }

  init() {
    // Add event listeners
    Object.keys(this.fields).forEach(fieldName => {
      const field = this.fields[fieldName].element;

      field.addEventListener('blur', () => this.validateField(fieldName));
      field.addEventListener('input', () => {
        if (field.classList.contains('error')) {
          this.validateField(fieldName);
        }
      });
    });

    // Password strength indicator
    const passwordField = document.getElementById('password');
    passwordField.addEventListener('input', () => this.updatePasswordStrength());

    // Password toggle
    const togglePassword = document.querySelector('.toggle-password');
    togglePassword.addEventListener('click', () => this.togglePasswordVisibility());

    // Form submission
    this.form.addEventListener('submit', (e) => this.handleSubmit(e));

    // Phone number formatting
    const phoneField = document.getElementById('phone');
    phoneField.addEventListener('input', (e) => this.formatPhoneNumber(e));
  }

  validateField(fieldName) {
    const field = this.fields[fieldName];
    const value = field.element.value.trim();
    const errorElement = document.getElementById(`${fieldName}-error`);

    let isValid = true;
    let errorMessage = '';

    for (const validator of field.validators) {
      const [validatorName, param] = validator.split(':');
      const result = this.validators[validatorName](value, param, fieldName);

      if (!result.valid) {
        isValid = false;
        errorMessage = result.message;
        break;
      }
    }

    if (isValid) {
      field.element.classList.remove('error');
      field.element.classList.add('success');
      errorElement.textContent = '';
    } else {
      field.element.classList.remove('success');
      field.element.classList.add('error');
      errorElement.textContent = errorMessage;
    }

    return isValid;
  }

  validators = {
    required: (value) => ({
      valid: value !== '',
      message: 'This field is required'
    }),

    email: (value) => {
      const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
      return {
        valid: !value || emailRegex.test(value),
        message: 'Please enter a valid email address'
      };
    },

    phone: (value) => {
      if (!value) return { valid: true };
      const phoneRegex = /^[\d\s\-\+\(\)]+$/;
      return {
        valid: phoneRegex.test(value) && value.replace(/\D/g, '').length >= 10,
        message: 'Please enter a valid phone number'
      };
    },

    minLength: (value, length) => ({
      valid: !value || value.length >= parseInt(length),
      message: `Must be at least ${length} characters`
    }),

    maxLength: (value, length) => ({
      valid: !value || value.length <= parseInt(length),
      message: `Must be no more than ${length} characters`
    }),

    alpha: (value) => {
      const alphaRegex = /^[a-zA-Z\s\-']+$/;
      return {
        valid: !value || alphaRegex.test(value),
        message: 'Only letters, spaces, hyphens, and apostrophes allowed'
      };
    },

    alphanumeric: (value) => {
      const alphanumericRegex = /^[a-zA-Z0-9]+$/;
      return {
        valid: !value || alphanumericRegex.test(value),
        message: 'Only letters and numbers allowed'
      };
    },

    password: (value) => {
      const hasUpperCase = /[A-Z]/.test(value);
      const hasLowerCase = /[a-z]/.test(value);
      const hasNumber = /[0-9]/.test(value);
      const hasSpecialChar = /[!@#$%^&*(),.?":{}|<>]/.test(value);

      const isValid = hasUpperCase && hasLowerCase && hasNumber && hasSpecialChar;

      return {
        valid: !value || isValid,
        message: 'Password must contain uppercase, lowercase, number, and special character'
      };
    },

    match: (value, fieldName) => {
      const matchField = document.getElementById(fieldName);
      return {
        valid: !value || value === matchField.value,
        message: 'Passwords do not match'
      };
    },

    checked: (value, _, fieldName) => {
      const field = this.fields[fieldName].element;
      return {
        valid: field.checked,
        message: 'You must agree to the terms and conditions'
      };
    }
  };

  updatePasswordStrength() {
    const password = document.getElementById('password').value;
    const strengthFill = document.getElementById('strengthFill');
    const strengthText = document.getElementById('strengthText');

    let strength = 0;
    let strengthClass = '';
    let strengthLabel = '';

    if (password.length === 0) {
      strengthFill.className = 'strength-fill';
      strengthText.textContent = 'Password strength';
      return;
    }

    // Calculate strength
    if (password.length >= 8) strength++;
    if (/[a-z]/.test(password) && /[A-Z]/.test(password)) strength++;
    if (/[0-9]/.test(password)) strength++;
    if (/[!@#$%^&*(),.?":{}|<>]/.test(password)) strength++;

    if (strength <= 2) {
      strengthClass = 'weak';
      strengthLabel = 'Weak';
    } else if (strength === 3) {
      strengthClass = 'medium';
      strengthLabel = 'Medium';
    } else {
      strengthClass = 'strong';
      strengthLabel = 'Strong';
    }

    strengthFill.className = `strength-fill ${strengthClass}`;
    strengthText.textContent = `Password strength: ${strengthLabel}`;
  }

  togglePasswordVisibility() {
    const passwordField = document.getElementById('password');
    const type = passwordField.type === 'password' ? 'text' : 'password';
    passwordField.type = type;
  }

  formatPhoneNumber(e) {
    let value = e.target.value.replace(/\D/g, '');

    if (value.length > 0) {
      if (value.length <= 3) {
        value = `(${value}`;
      } else if (value.length <= 6) {
        value = `(${value.slice(0, 3)}) ${value.slice(3)}`;
      } else {
        value = `(${value.slice(0, 3)}) ${value.slice(3, 6)}-${value.slice(6, 10)}`;
      }
    }

    e.target.value = value;
  }

  validateAllFields() {
    let isValid = true;

    Object.keys(this.fields).forEach(fieldName => {
      if (!this.validateField(fieldName)) {
        isValid = false;
      }
    });

    return isValid;
  }

  handleSubmit(e) {
    e.preventDefault();

    // Validate all fields
    const isFormValid = this.validateAllFields();

    // Check CAPTCHA
    const captchaVerified = window.captchaManager && window.captchaManager.isVerified();

    if (!captchaVerified) {
      const captchaError = document.getElementById('captcha-error');
      captchaError.textContent = 'Please complete the CAPTCHA verification';
      return;
    }

    if (isFormValid && captchaVerified) {
      // Show success modal
      this.showSuccessModal();
    } else {
      // Scroll to first error
      const firstError = this.form.querySelector('.error');
      if (firstError) {
        firstError.scrollIntoView({ behavior: 'smooth', block: 'center' });
        firstError.focus();
      }
    }
  }

  showSuccessModal() {
    const modal = document.getElementById('successModal');
    modal.classList.add('active');
  }
}

// Close modal function
function closeModal() {
  const modal = document.getElementById('successModal');
  modal.classList.remove('active');

  // Reset form
  document.getElementById('registrationForm').reset();

  // Reset all field states
  document.querySelectorAll('.form-input, .form-select').forEach(field => {
    field.classList.remove('error', 'success');
  });

  // Reset password strength
  document.getElementById('strengthFill').className = 'strength-fill';
  document.getElementById('strengthText').textContent = 'Password strength';

  // Reset CAPTCHA
  if (window.captchaManager) {
    window.captchaManager.reset();
  }
}

// Initialize form validation when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
  new FormValidator('registrationForm');
});
