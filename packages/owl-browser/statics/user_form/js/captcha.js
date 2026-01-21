// CAPTCHA Manager
class CaptchaManager {
  constructor() {
    this.verified = false;
    this.challengeActive = false;
    this.selectedImages = new Set();
    this.correctImages = new Set();
    this.targetObject = '';
    this.metadata = null;
    this.currentImagePaths = [];  // Store current grid image paths

    // Bot detection tracking
    this.botDetection = {
      challengeStartTime: 0,
      firstClickTime: 0,
      clickTimestamps: [],
      clickOrder: [],
      mouseMovements: 0,
      mouseMovementTimestamps: [],
      lastMouseX: 0,
      lastMouseY: 0,
      hoverEvents: 0,
      totalMouseDistance: 0
    };

    // Challenge configurations - map to image categories
    this.challenges = [
      { name: 'cars', category: 'cars' },
      { name: 'buses', category: 'buses' },
      { name: 'traffic lights', category: 'traffic_lights' },
      { name: 'fire hydrants', category: 'fire_hydrants' },
      { name: 'boats', category: 'boats' },
      { name: 'airplanes', category: 'airplanes' },
      { name: 'crosswalks', category: 'crosswalks' },
      { name: 'stairs', category: 'stairs' }
    ];

    this.currentChallenge = null;
    this.init();
  }

  async init() {
    // Load metadata
    await this.loadMetadata();

    // CAPTCHA checkbox - use click event instead of change to control behavior
    const checkbox = document.getElementById('captchaCheck');
    checkbox.addEventListener('click', (e) => this.handleCheckboxClick(e));

    // Challenge buttons
    const skipBtn = document.getElementById('skipBtn');
    const verifyBtn = document.getElementById('verifyBtn');

    skipBtn.addEventListener('click', () => this.skipChallenge());
    verifyBtn.addEventListener('click', () => this.verifyChallenge());

    // Generate image grid
    this.generateImageGrid();
  }

  async loadMetadata() {
    try {
      const response = await fetch('images/captcha/metadata.json');
      this.metadata = await response.json();
      console.log('CAPTCHA metadata loaded from file:', this.metadata);
    } catch (error) {
      console.warn('Failed to fetch metadata, using inline fallback:', error);
      // Fallback for file:// protocol (CORS issues)
      this.metadata = {
        "categories": {
          "cars": ["cars_00.jpg","cars_01.jpg","cars_02.jpg","cars_03.jpg","cars_04.jpg","cars_05.jpg","cars_06.jpg","cars_07.jpg","cars_08.jpg","cars_09.jpg","cars_10.jpg","cars_11.jpg"],
          "buses": ["buses_00.jpg","buses_01.jpg","buses_02.jpg","buses_03.jpg","buses_04.jpg","buses_05.jpg","buses_06.jpg","buses_07.jpg","buses_08.jpg","buses_09.jpg","buses_10.jpg","buses_11.jpg"],
          "traffic_lights": ["traffic_lights_00.jpg","traffic_lights_01.jpg","traffic_lights_02.jpg","traffic_lights_03.jpg","traffic_lights_04.jpg","traffic_lights_05.jpg","traffic_lights_06.jpg","traffic_lights_07.jpg","traffic_lights_08.jpg","traffic_lights_09.jpg","traffic_lights_10.jpg","traffic_lights_11.jpg"],
          "fire_hydrants": ["fire_hydrants_00.jpg","fire_hydrants_01.jpg","fire_hydrants_02.jpg","fire_hydrants_03.jpg","fire_hydrants_04.jpg","fire_hydrants_05.jpg","fire_hydrants_06.jpg","fire_hydrants_07.jpg","fire_hydrants_08.jpg","fire_hydrants_09.jpg","fire_hydrants_10.jpg","fire_hydrants_11.jpg"],
          "boats": ["boats_00.jpg","boats_01.jpg","boats_02.jpg","boats_03.jpg","boats_04.jpg","boats_05.jpg","boats_06.jpg","boats_07.jpg","boats_08.jpg","boats_09.jpg","boats_10.jpg","boats_11.jpg"],
          "airplanes": ["airplanes_00.jpg","airplanes_01.jpg","airplanes_02.jpg","airplanes_03.jpg","airplanes_04.jpg","airplanes_05.jpg","airplanes_06.jpg","airplanes_07.jpg","airplanes_08.jpg","airplanes_09.jpg","airplanes_10.jpg","airplanes_11.jpg"],
          "crosswalks": ["crosswalks_00.jpg","crosswalks_01.jpg","crosswalks_02.jpg","crosswalks_03.jpg","crosswalks_04.jpg","crosswalks_05.jpg","crosswalks_06.jpg","crosswalks_07.jpg","crosswalks_08.jpg","crosswalks_09.jpg","crosswalks_10.jpg","crosswalks_11.jpg"],
          "stairs": ["stairs_00.jpg","stairs_01.jpg","stairs_02.jpg","stairs_03.jpg","stairs_04.jpg","stairs_05.jpg","stairs_06.jpg","stairs_07.jpg","stairs_08.jpg","stairs_09.jpg","stairs_10.jpg","stairs_11.jpg"]
        },
        "backgrounds": ["bg_00.jpg","bg_01.jpg","bg_02.jpg","bg_03.jpg","bg_04.jpg","bg_05.jpg","bg_06.jpg","bg_07.jpg","bg_08.jpg","bg_09.jpg","bg_10.jpg","bg_11.jpg","bg_12.jpg","bg_13.jpg","bg_14.jpg","bg_15.jpg","bg_16.jpg","bg_17.jpg","bg_18.jpg","bg_19.jpg","bg_20.jpg","bg_21.jpg","bg_22.jpg","bg_23.jpg","bg_24.jpg"]
      };
      console.log('CAPTCHA metadata loaded (inline):', this.metadata);
    }
  }

  handleCheckboxClick(e) {
    const checkbox = document.getElementById('captchaCheck');

    // If already verified, do nothing
    if (this.verified) {
      return;
    }

    // If challenge is active, ignore checkbox clicks - user must complete the challenge
    if (this.challengeActive) {
      e.preventDefault();
      return;
    }

    // Prevent default checkbox behavior - we'll control it manually
    e.preventDefault();

    // Prevent double-clicks by disabling immediately
    if (checkbox.disabled) {
      return;
    }

    // Simulate "thinking" delay
    checkbox.disabled = true;

    setTimeout(() => {
      // Always show challenge - no auto-verify for testing purposes
      // This ensures the CAPTCHA solving logic is always tested
      this.showChallenge();
      checkbox.disabled = false;
    }, 500);
  }

  showChallenge() {
    const challenge = document.getElementById('captchaChallenge');
    const checkbox = document.getElementById('captchaCheck');

    // IMPORTANT: Uncheck the box when showing challenge
    checkbox.checked = false;

    challenge.classList.add('active');
    this.challengeActive = true;

    // Select random challenge
    this.currentChallenge = this.challenges[Math.floor(Math.random() * this.challenges.length)];
    this.targetObject = this.currentChallenge.name;

    // Update challenge text
    document.getElementById('challengeTarget').textContent = this.targetObject;

    // Reset selections
    this.selectedImages.clear();
    this.updateGridSelections();

    // Reset bot detection for new challenge
    this.resetBotDetection();

    // Start tracking mouse movements
    this.startMouseTracking();

    // Generate new images with real photos
    this.generateImages();
  }

  resetBotDetection() {
    this.botDetection = {
      challengeStartTime: Date.now(),
      firstClickTime: 0,
      clickTimestamps: [],
      clickOrder: [],
      mouseMovements: 0,
      mouseMovementTimestamps: [],
      lastMouseX: 0,
      lastMouseY: 0,
      hoverEvents: 0,
      totalMouseDistance: 0
    };
  }

  startMouseTracking() {
    const challenge = document.getElementById('captchaChallenge');

    // Remove old listeners if any
    if (this.mouseMoveHandler) {
      challenge.removeEventListener('mousemove', this.mouseMoveHandler);
    }
    if (this.mouseEnterHandler) {
      challenge.removeEventListener('mouseenter', this.mouseEnterHandler);
    }

    // Track mouse movements
    this.mouseMoveHandler = (e) => {
      if (!this.challengeActive) return;

      const dx = e.clientX - this.botDetection.lastMouseX;
      const dy = e.clientY - this.botDetection.lastMouseY;
      const distance = Math.sqrt(dx * dx + dy * dy);

      if (distance > 5) { // Filter out tiny movements
        this.botDetection.mouseMovements++;
        this.botDetection.mouseMovementTimestamps.push(Date.now());
        this.botDetection.totalMouseDistance += distance;
      }

      this.botDetection.lastMouseX = e.clientX;
      this.botDetection.lastMouseY = e.clientY;
    };

    this.mouseEnterHandler = () => {
      if (!this.challengeActive) return;
      this.botDetection.hoverEvents++;
    };

    challenge.addEventListener('mousemove', this.mouseMoveHandler);
    challenge.addEventListener('mouseenter', this.mouseEnterHandler);

    // Track hover on grid items
    const gridItems = document.querySelectorAll('.grid-item');
    gridItems.forEach((item) => {
      item.addEventListener('mouseenter', () => {
        if (this.challengeActive) {
          this.botDetection.hoverEvents++;
        }
      });
    });
  }

  generateImageGrid() {
    const grid = document.getElementById('imageGrid');
    grid.innerHTML = '';

    for (let i = 0; i < 9; i++) {
      const item = document.createElement('div');
      item.className = 'grid-item';
      item.dataset.index = i;
      item.addEventListener('click', () => this.toggleImage(i));

      grid.appendChild(item);
    }
  }

  generateImages() {
    if (!this.metadata) {
      console.error('Metadata not loaded yet');
      return;
    }

    const gridItems = document.querySelectorAll('.grid-item');

    // Randomly decide which 4-6 squares will contain the target object (more visible)
    const numTargets = Math.floor(Math.random() * 3) + 4;  // 4-6 targets (increased)
    const targetIndices = new Set();

    while (targetIndices.size < numTargets) {
      targetIndices.add(Math.floor(Math.random() * 9));
    }

    this.correctImages = targetIndices;

    console.log(`Challenge: "${this.targetObject}" - Correct squares:`, Array.from(targetIndices).sort());

    // Get available images for current category
    const category = this.currentChallenge.category;
    const categoryImages = this.metadata.categories[category] || [];
    const backgroundImages = this.metadata.backgrounds || [];

    if (categoryImages.length === 0 || backgroundImages.length === 0) {
      console.error('No images available for category:', category);
      return;
    }

    // Shuffle arrays for random selection
    const shuffledCategory = [...categoryImages].sort(() => Math.random() - 0.5);
    const shuffledBackgrounds = [...backgroundImages].sort(() => Math.random() - 0.5);

    this.currentImagePaths = [];

    // Assign images to grid
    let categoryIndex = 0;
    let backgroundIndex = 0;

    gridItems.forEach((item, index) => {
      const img = document.createElement('img');

      if (targetIndices.has(index)) {
        // Use target category image
        const imageName = shuffledCategory[categoryIndex % shuffledCategory.length];
        img.src = `images/captcha/${category}/${imageName}`;
        categoryIndex++;
      } else {
        // Use background image
        const imageName = shuffledBackgrounds[backgroundIndex % shuffledBackgrounds.length];
        img.src = `images/captcha/backgrounds/${imageName}`;
        backgroundIndex++;
      }

      img.alt = `Image ${index + 1}`;

      // Handle image load errors
      img.onerror = () => {
        console.error('Failed to load image:', img.src);
        // Fallback to placeholder
        img.src = 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200"><rect width="200" height="200" fill="%23ddd"/><text x="100" y="100" text-anchor="middle" fill="%23999">Error</text></svg>';
      };

      item.innerHTML = '';
      item.appendChild(img);

      this.currentImagePaths.push(img.src);
    });
  }

  toggleImage(index) {
    if (!this.challengeActive) return;

    const now = Date.now();

    if (this.selectedImages.has(index)) {
      this.selectedImages.delete(index);
      // Remove from click order and timestamps
      const orderIndex = this.botDetection.clickOrder.indexOf(index);
      if (orderIndex > -1) {
        this.botDetection.clickOrder.splice(orderIndex, 1);
        this.botDetection.clickTimestamps.splice(orderIndex, 1);
      }
    } else {
      this.selectedImages.add(index);

      // Track first click time
      if (this.botDetection.firstClickTime === 0) {
        this.botDetection.firstClickTime = now;
      }

      // Track click timing and order
      this.botDetection.clickTimestamps.push(now);
      this.botDetection.clickOrder.push(index);
    }

    this.updateGridSelections();
  }

  updateGridSelections() {
    const gridItems = document.querySelectorAll('.grid-item');

    gridItems.forEach((item, index) => {
      if (this.selectedImages.has(index)) {
        item.classList.add('selected');
      } else {
        item.classList.remove('selected');
      }
    });
  }

  skipChallenge() {
    // Generate new challenge
    this.showChallenge();
  }

  verifyChallenge() {
    const captchaError = document.getElementById('captcha-error');

    // Check if selected images match correct images
    if (this.selectedImages.size === 0) {
      captchaError.textContent = 'Please select at least one image';
      return;
    }

    const isCorrect = this.checkSelection();

    if (isCorrect) {
      // Check for bot behavior
      const botScore = this.analyzeBotBehavior();

      if (botScore.isBot) {
        // Bot detected - show another challenge
        console.warn('🤖 BOT DETECTED! Showing another challenge...');
        console.log('Bot detection signature:', botScore);

        captchaError.textContent = 'Please try again.';

        setTimeout(() => {
          this.showChallenge();
          captchaError.textContent = '';
        }, 1500);
      } else {
        // Human verified
        console.log('✅ Human behavior detected');
        console.log('Bot detection signature:', botScore);
        this.markAsVerified();
        captchaError.textContent = '';
      }
    } else {
      // Failed - show new challenge
      captchaError.textContent = 'Incorrect selection. Please try again.';

      setTimeout(() => {
        this.showChallenge();
        captchaError.textContent = '';
      }, 1500);
    }
  }

  analyzeBotBehavior() {
    const bd = this.botDetection;
    const now = Date.now();

    // Calculate metrics
    const timeToFirstClick = bd.firstClickTime - bd.challengeStartTime;
    const totalTime = now - bd.challengeStartTime;
    const numClicks = bd.clickTimestamps.length;

    // Calculate click intervals
    const clickIntervals = [];
    for (let i = 1; i < bd.clickTimestamps.length; i++) {
      clickIntervals.push(bd.clickTimestamps[i] - bd.clickTimestamps[i - 1]);
    }

    const avgClickInterval = clickIntervals.length > 0
      ? clickIntervals.reduce((a, b) => a + b, 0) / clickIntervals.length
      : 0;

    // Check for variance in click timing (humans vary, bots are consistent)
    const clickVariance = clickIntervals.length > 1
      ? this.calculateVariance(clickIntervals)
      : 0;

    // Check if clicks are in linear order (0,1,2,3 or 8,7,6,5)
    const isLinearOrder = this.isLinearPattern(bd.clickOrder);

    // Check if clicks are in grid order (left-to-right, top-to-bottom)
    const isGridOrder = this.isGridPattern(bd.clickOrder);

    // Bot score calculation
    let botScore = 0;
    const reasons = [];

    // 1. Too fast to first click (< 500ms is suspicious)
    if (timeToFirstClick < 500) {
      botScore += 30;
      reasons.push('Too fast to first click');
    }

    // 2. Average click interval too fast (< 100ms is bot-like)
    if (avgClickInterval < 100 && numClicks > 1) {
      botScore += 40;
      reasons.push('Clicks too rapid');
    }

    // 3. Too consistent timing (low variance = bot)
    if (clickVariance < 50 && numClicks > 2) {
      botScore += 25;
      reasons.push('Too consistent timing');
    }

    // 4. Linear selection pattern
    if (isLinearOrder) {
      botScore += 35;
      reasons.push('Linear selection pattern');
    }

    // 5. Grid pattern (left-to-right, top-to-bottom)
    if (isGridOrder) {
      botScore += 30;
      reasons.push('Grid selection pattern');
    }

    // 6. No mouse movement (automation)
    if (bd.mouseMovements < 3) {
      botScore += 50;
      reasons.push('No mouse movement');
    }

    // 7. Very little mouse movement for the time spent
    const mouseMovementRate = bd.mouseMovements / (totalTime / 1000); // movements per second
    if (mouseMovementRate < 2 && totalTime > 2000) {
      botScore += 20;
      reasons.push('Low mouse movement rate');
    }

    // 8. No hover events (bots don't hover)
    if (bd.hoverEvents < numClicks) {
      botScore += 15;
      reasons.push('No hover before clicks');
    }

    // 9. Total time too short (< 1.5 seconds for multiple clicks)
    if (totalTime < 1500 && numClicks >= 3) {
      botScore += 30;
      reasons.push('Completed too quickly');
    }

    const signature = {
      isBot: botScore >= 100, // Threshold for bot detection
      botScore: botScore,
      confidence: Math.min(100, botScore),
      reasons: reasons,
      metrics: {
        timeToFirstClick: timeToFirstClick,
        totalTime: totalTime,
        numClicks: numClicks,
        avgClickInterval: avgClickInterval.toFixed(2),
        clickVariance: clickVariance.toFixed(2),
        mouseMovements: bd.mouseMovements,
        mouseDistance: bd.totalMouseDistance.toFixed(2),
        hoverEvents: bd.hoverEvents,
        clickOrder: bd.clickOrder,
        isLinearOrder: isLinearOrder,
        isGridOrder: isGridOrder
      }
    };

    return signature;
  }

  isLinearPattern(order) {
    if (order.length < 3) return false;

    // Check ascending (0,1,2,3...)
    let isAscending = true;
    for (let i = 1; i < order.length; i++) {
      if (order[i] !== order[i - 1] + 1) {
        isAscending = false;
        break;
      }
    }

    // Check descending (8,7,6,5...)
    let isDescending = true;
    for (let i = 1; i < order.length; i++) {
      if (order[i] !== order[i - 1] - 1) {
        isDescending = false;
        break;
      }
    }

    return isAscending || isDescending;
  }

  isGridPattern(order) {
    if (order.length < 3) return false;

    // Grid is 3x3: [0,1,2] [3,4,5] [6,7,8]
    // Check if mostly left-to-right, top-to-bottom
    let correctOrderCount = 0;

    for (let i = 1; i < order.length; i++) {
      if (order[i] > order[i - 1]) {
        correctOrderCount++;
      }
    }

    // If > 70% follow left-to-right pattern
    return (correctOrderCount / (order.length - 1)) > 0.7;
  }

  calculateVariance(arr) {
    if (arr.length === 0) return 0;
    const mean = arr.reduce((a, b) => a + b, 0) / arr.length;
    const squareDiffs = arr.map(value => Math.pow(value - mean, 2));
    return squareDiffs.reduce((a, b) => a + b, 0) / arr.length;
  }

  checkSelection() {
    // More forgiving validation - like real CAPTCHAs
    const selectedArray = Array.from(this.selectedImages).sort();
    const correctArray = Array.from(this.correctImages).sort();

    console.log('User selected:', selectedArray);
    console.log('Correct answer:', correctArray);

    // Count how many correct squares were selected
    let correctSelections = 0;
    let wrongSelections = 0;

    for (const img of this.selectedImages) {
      if (this.correctImages.has(img)) {
        correctSelections++;
      } else {
        wrongSelections++;
      }
    }

    const totalCorrect = this.correctImages.size;
    const accuracy = correctSelections / totalCorrect;

    console.log(`Accuracy: ${correctSelections}/${totalCorrect} correct (${(accuracy * 100).toFixed(0)}%)`);
    console.log(`Wrong selections: ${wrongSelections}`);

    // Pass if:
    // - Selected at least 75% of correct images
    // - AND didn't select more than 2 wrong images
    const passed = accuracy >= 0.75 && wrongSelections <= 2;

    if (passed) {
      console.log('✅ Passed! (Good enough)');
    } else {
      console.log('❌ Failed - need at least 75% accuracy and max 2 wrong selections');
    }

    return passed;
  }

  markAsVerified() {
    this.verified = true;
    this.challengeActive = false;

    const challenge = document.getElementById('captchaChallenge');
    const checkbox = document.getElementById('captchaCheck');
    const captchaError = document.getElementById('captcha-error');

    challenge.classList.remove('active');
    checkbox.checked = true;
    checkbox.disabled = true;
    captchaError.textContent = '';

    // Update checkbox label
    const label = checkbox.nextElementSibling;
    const textSpan = label.querySelector('.captcha-text');
    textSpan.textContent = 'Verified';

    // Add green checkmark animation
    const checkmark = label.querySelector('.checkmark');
    checkmark.style.background = '#10B981';
    checkmark.style.borderColor = '#10B981';
  }

  isVerified() {
    return this.verified;
  }

  reset() {
    this.verified = false;
    this.challengeActive = false;
    this.selectedImages.clear();
    this.correctImages.clear();

    const challenge = document.getElementById('captchaChallenge');
    const checkbox = document.getElementById('captchaCheck');

    challenge.classList.remove('active');
    checkbox.checked = false;
    checkbox.disabled = false;

    // Reset label
    const label = checkbox.nextElementSibling;
    const textSpan = label.querySelector('.captcha-text');
    textSpan.textContent = "I'm not a robot";

    const checkmark = label.querySelector('.checkmark');
    checkmark.style.background = '';
    checkmark.style.borderColor = '';
  }
}

// Initialize CAPTCHA when DOM is loaded
document.addEventListener('DOMContentLoaded', () => {
  window.captchaManager = new CaptchaManager();
});
