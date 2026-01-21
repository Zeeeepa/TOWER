/**
 * Drag & Drop Test Challenges
 * Tests slider puzzles, drawing, and drag interactions
 *
 * Debug mode: Add ?debug=true to URL to show coordinates on elements
 */

class DragDropChallenges {
  constructor() {
    this.results = {
      slider: false,
      puzzle: false,
      drawing: false,
      reorder: false
    };

    // Check for debug mode
    // Supports: ?debug=true, hash containing 'debug', window.DEBUG_MODE=true
    const urlParams = new URLSearchParams(window.location.search);
    const hash = window.location.hash || '';
    this.debugMode = urlParams.get('debug') === 'true' ||
                     hash.toLowerCase().includes('debug') ||
                     window.DEBUG_MODE === true;

    this.eventLog = document.getElementById('eventLog');
    this.init();

    if (this.debugMode) {
      this.initDebugOverlays();
    }
  }

  // Create coordinate overlay for an element
  createCoordOverlay(element, label = '') {
    const rect = element.getBoundingClientRect();
    const centerX = Math.round(rect.left + rect.width / 2);
    const centerY = Math.round(rect.top + rect.height / 2);

    const overlay = document.createElement('div');
    overlay.className = 'debug-coord-overlay';
    overlay.innerHTML = `
      <div class="debug-label">${label}</div>
      <div class="debug-coords">(${centerX}, ${centerY})</div>
      <div class="debug-size">${Math.round(rect.width)}x${Math.round(rect.height)}</div>
    `;
    overlay.style.cssText = `
      position: absolute;
      left: ${rect.left}px;
      top: ${rect.top}px;
      width: ${rect.width}px;
      height: ${rect.height}px;
      border: 2px solid red;
      background: rgba(255, 0, 0, 0.1);
      pointer-events: none;
      z-index: 9999;
      font-family: monospace;
      font-size: 10px;
      color: red;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      text-align: center;
    `;
    document.body.appendChild(overlay);
    return overlay;
  }

  // Initialize debug overlays for all interactive elements
  initDebugOverlays() {
    this.log('DEBUG MODE: Showing coordinates on elements');

    // Add debug styles
    const style = document.createElement('style');
    style.textContent = `
      .debug-coord-overlay {
        font-weight: bold;
        text-shadow: 0 0 2px white, 0 0 2px white;
      }
      .debug-label {
        background: red;
        color: white;
        padding: 1px 4px;
        font-size: 9px;
        margin-bottom: 2px;
      }
      .debug-coords {
        font-size: 11px;
        background: rgba(255,255,255,0.9);
        padding: 1px 3px;
      }
      .debug-size {
        font-size: 9px;
        color: #666;
      }
    `;
    document.head.appendChild(style);

    // Wait a bit for layout to settle, then create overlays
    setTimeout(() => {
      // Slider thumb
      this.createCoordOverlay(document.getElementById('sliderThumb'), 'SLIDER START');

      // Slider end position (calculate where thumb should end)
      const sliderTrack = document.querySelector('.slider-track');
      const trackRect = sliderTrack.getBoundingClientRect();
      const endMarker = document.createElement('div');
      endMarker.style.cssText = `
        position: absolute;
        left: ${trackRect.right - 30}px;
        top: ${trackRect.top}px;
        width: 20px;
        height: ${trackRect.height}px;
        border: 2px dashed green;
        background: rgba(0, 255, 0, 0.1);
        pointer-events: none;
        z-index: 9998;
      `;
      document.body.appendChild(endMarker);
      const endLabel = document.createElement('div');
      endLabel.innerHTML = `SLIDER END<br>(${Math.round(trackRect.right - 20)}, ${Math.round(trackRect.top + trackRect.height / 2)})`;
      endLabel.style.cssText = `
        position: absolute;
        left: ${trackRect.right - 60}px;
        top: ${trackRect.top - 35}px;
        font-family: monospace;
        font-size: 10px;
        color: green;
        font-weight: bold;
        text-align: center;
        background: rgba(255,255,255,0.9);
        padding: 2px 4px;
        z-index: 9999;
      `;
      document.body.appendChild(endLabel);

      // Puzzle piece
      this.createCoordOverlay(document.getElementById('puzzlePiece'), 'PUZZLE PIECE');

      // Puzzle cutout
      this.createCoordOverlay(document.getElementById('puzzleCutout'), 'PUZZLE TARGET');

      // Drawing canvas with dots
      const canvas = document.getElementById('drawingCanvas');
      const canvasRect = canvas.getBoundingClientRect();
      this.createCoordOverlay(canvas, 'CANVAS');

      // Mark each dot on the canvas
      const dots = [
        { x: 50, y: 125, label: 'DOT 1' },
        { x: 150, y: 50, label: 'DOT 2' },
        { x: 250, y: 125, label: 'DOT 3' },
        { x: 350, y: 50, label: 'DOT 4' },
        { x: 350, y: 200, label: 'DOT 5' },
        { x: 50, y: 200, label: 'DOT 6' }
      ];

      dots.forEach(dot => {
        const absX = Math.round(canvasRect.left + dot.x);
        const absY = Math.round(canvasRect.top + dot.y);
        const marker = document.createElement('div');
        marker.innerHTML = `${dot.label}<br>(${absX}, ${absY})`;
        marker.style.cssText = `
          position: absolute;
          left: ${absX - 25}px;
          top: ${absY - 30}px;
          font-family: monospace;
          font-size: 9px;
          color: blue;
          font-weight: bold;
          text-align: center;
          background: rgba(255,255,255,0.9);
          padding: 1px 3px;
          border: 1px solid blue;
          z-index: 9999;
          pointer-events: none;
        `;
        document.body.appendChild(marker);
      });

      // Reorder items
      const reorderItems = document.querySelectorAll('.reorder-item');
      reorderItems.forEach((item, i) => {
        this.createCoordOverlay(item, `ITEM ${item.dataset.value}`);
      });

      // Verify button
      this.createCoordOverlay(document.getElementById('verifyDrawing'), 'VERIFY BTN');

      this.log('DEBUG: All coordinate overlays created');
    }, 500);
  }

  log(message) {
    const timestamp = new Date().toLocaleTimeString();
    const entry = `[${timestamp}] ${message}\n`;
    this.eventLog.textContent += entry;
    this.eventLog.scrollTop = this.eventLog.scrollHeight;
    console.log(message);
  }

  updateResults() {
    document.getElementById('resultSlider').textContent = this.results.slider ? 'Passed' : 'Pending';
    document.getElementById('resultSlider').className = 'result-value ' + (this.results.slider ? 'success' : 'pending');

    document.getElementById('resultPuzzle').textContent = this.results.puzzle ? 'Passed' : 'Pending';
    document.getElementById('resultPuzzle').className = 'result-value ' + (this.results.puzzle ? 'success' : 'pending');

    document.getElementById('resultDrawing').textContent = this.results.drawing ? 'Passed' : 'Pending';
    document.getElementById('resultDrawing').className = 'result-value ' + (this.results.drawing ? 'success' : 'pending');

    document.getElementById('resultReorder').textContent = this.results.reorder ? 'Passed' : 'Pending';
    document.getElementById('resultReorder').className = 'result-value ' + (this.results.reorder ? 'success' : 'pending');
  }

  init() {
    this.initSlider();
    this.initPuzzle();
    this.initDrawing();
    this.initReorder();
    this.log('All challenges initialized');
  }

  // ============================================
  // Challenge 1: Slider Puzzle
  // ============================================
  initSlider() {
    const container = document.getElementById('sliderContainer');
    const track = container.querySelector('.slider-track');
    const thumb = document.getElementById('sliderThumb');
    const fill = document.getElementById('sliderFill');
    const text = document.getElementById('sliderText');
    const status = document.getElementById('sliderStatus');

    let isDragging = false;
    let startX = 0;
    let thumbLeft = 3;
    const maxLeft = track.offsetWidth - thumb.offsetWidth - 6;
    const successThreshold = maxLeft * 0.95;

    const updatePosition = (clientX) => {
      const rect = track.getBoundingClientRect();
      let newLeft = clientX - rect.left - thumb.offsetWidth / 2;
      newLeft = Math.max(3, Math.min(newLeft, maxLeft));

      thumb.style.left = newLeft + 'px';
      const percentage = (newLeft - 3) / maxLeft * 100;
      fill.style.width = percentage + '%';

      return newLeft;
    };

    const checkSuccess = (finalLeft) => {
      if (finalLeft >= successThreshold) {
        this.results.slider = true;
        thumb.classList.add('success');
        thumb.innerHTML = '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3"><path d="M5 12l5 5L20 7"/></svg>';
        text.textContent = 'Verified!';
        status.textContent = 'Slider verification successful!';
        status.className = 'slider-status success';
        fill.style.background = 'var(--success-color)';
        this.log('SLIDER: Verification successful');
        this.updateResults();
      } else {
        // Reset on failure
        thumb.style.left = '3px';
        fill.style.width = '0%';
        status.textContent = 'Slide further to verify';
        status.className = 'slider-status error';
        this.log('SLIDER: Incomplete slide, reset');
      }
    };

    // Mouse events
    thumb.addEventListener('mousedown', (e) => {
      if (this.results.slider) return;
      isDragging = true;
      startX = e.clientX;
      thumb.style.cursor = 'grabbing';
      this.log('SLIDER: Drag started');
      e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
      if (!isDragging || this.results.slider) return;
      thumbLeft = updatePosition(e.clientX);
    });

    document.addEventListener('mouseup', (e) => {
      if (!isDragging) return;
      isDragging = false;
      thumb.style.cursor = 'grab';
      this.log(`SLIDER: Drag ended at position ${Math.round(thumbLeft)}`);
      checkSuccess(thumbLeft);
    });

    // Touch events
    thumb.addEventListener('touchstart', (e) => {
      if (this.results.slider) return;
      isDragging = true;
      startX = e.touches[0].clientX;
      this.log('SLIDER: Touch drag started');
    }, { passive: true });

    document.addEventListener('touchmove', (e) => {
      if (!isDragging || this.results.slider) return;
      thumbLeft = updatePosition(e.touches[0].clientX);
    }, { passive: true });

    document.addEventListener('touchend', () => {
      if (!isDragging) return;
      isDragging = false;
      this.log(`SLIDER: Touch drag ended at position ${Math.round(thumbLeft)}`);
      checkSuccess(thumbLeft);
    });
  }

  // ============================================
  // Challenge 2: Puzzle Piece
  // ============================================
  initPuzzle() {
    const container = document.getElementById('puzzleContainer');
    const piece = document.getElementById('puzzlePiece');
    const cutout = document.getElementById('puzzleCutout');
    const status = document.getElementById('puzzleStatus');

    let isDragging = false;
    let startX = 0, startY = 0;
    let pieceX = 20, pieceY = 80;

    // Get cutout position (target)
    const cutoutRect = cutout.getBoundingClientRect();
    const containerRect = container.getBoundingClientRect();
    const targetX = cutoutRect.left - containerRect.left + container.scrollLeft;
    const targetY = cutoutRect.top - containerRect.top + container.scrollTop - 30; // Adjust for header

    const updatePosition = (clientX, clientY) => {
      const rect = container.getBoundingClientRect();
      pieceX = clientX - rect.left - 25;
      pieceY = clientY - rect.top - 25;

      // Constrain within container
      pieceX = Math.max(0, Math.min(pieceX, rect.width - 50));
      pieceY = Math.max(0, Math.min(pieceY, rect.height - 50));

      piece.style.left = pieceX + 'px';
      piece.style.top = pieceY + 'px';
    };

    const checkSuccess = () => {
      // Calculate if piece is close enough to cutout
      const pieceRect = piece.getBoundingClientRect();
      const cutoutRect = cutout.getBoundingClientRect();

      const dx = Math.abs(pieceRect.left - cutoutRect.left);
      const dy = Math.abs(pieceRect.top - cutoutRect.top);
      const distance = Math.sqrt(dx * dx + dy * dy);

      this.log(`PUZZLE: Distance to target: ${Math.round(distance)}px`);

      if (distance < 30) {
        this.results.puzzle = true;
        piece.classList.add('correct');
        piece.style.left = (cutoutRect.left - container.getBoundingClientRect().left) + 'px';
        piece.style.top = (cutoutRect.top - container.getBoundingClientRect().top) + 'px';
        status.textContent = 'Puzzle solved!';
        status.className = 'puzzle-status success';
        this.log('PUZZLE: Solved successfully');
        this.updateResults();
      } else {
        status.textContent = 'Try placing the piece in the outlined area';
        this.log('PUZZLE: Not in correct position');
      }
    };

    // Mouse events
    piece.addEventListener('mousedown', (e) => {
      if (this.results.puzzle) return;
      isDragging = true;
      startX = e.clientX - pieceX;
      startY = e.clientY - pieceY;
      piece.style.cursor = 'grabbing';
      this.log('PUZZLE: Drag started');
      e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
      if (!isDragging || this.results.puzzle) return;
      updatePosition(e.clientX, e.clientY);
    });

    document.addEventListener('mouseup', () => {
      if (!isDragging) return;
      isDragging = false;
      piece.style.cursor = 'grab';
      this.log(`PUZZLE: Drag ended at (${Math.round(pieceX)}, ${Math.round(pieceY)})`);
      checkSuccess();
    });

    // Touch events
    piece.addEventListener('touchstart', (e) => {
      if (this.results.puzzle) return;
      isDragging = true;
      startX = e.touches[0].clientX - pieceX;
      startY = e.touches[0].clientY - pieceY;
      this.log('PUZZLE: Touch drag started');
    }, { passive: true });

    document.addEventListener('touchmove', (e) => {
      if (!isDragging || this.results.puzzle) return;
      updatePosition(e.touches[0].clientX, e.touches[0].clientY);
    }, { passive: true });

    document.addEventListener('touchend', () => {
      if (!isDragging) return;
      isDragging = false;
      this.log(`PUZZLE: Touch drag ended at (${Math.round(pieceX)}, ${Math.round(pieceY)})`);
      checkSuccess();
    });
  }

  // ============================================
  // Challenge 3: Drawing Canvas
  // ============================================
  initDrawing() {
    const canvas = document.getElementById('drawingCanvas');
    const ctx = canvas.getContext('2d');
    const clearBtn = document.getElementById('clearCanvas');
    const verifyBtn = document.getElementById('verifyDrawing');
    const status = document.getElementById('canvasStatus');

    let isDrawing = false;
    let points = [];
    let lastX = 0, lastY = 0;

    // Draw target dots
    const dots = [
      { x: 50, y: 125 },
      { x: 150, y: 50 },
      { x: 250, y: 125 },
      { x: 350, y: 50 },
      { x: 350, y: 200 },
      { x: 50, y: 200 }
    ];

    const drawDots = () => {
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      // Draw connecting lines (faint)
      ctx.strokeStyle = '#e0e0e0';
      ctx.lineWidth = 1;
      ctx.setLineDash([5, 5]);
      ctx.beginPath();
      dots.forEach((dot, i) => {
        if (i === 0) ctx.moveTo(dot.x, dot.y);
        else ctx.lineTo(dot.x, dot.y);
      });
      ctx.closePath();
      ctx.stroke();
      ctx.setLineDash([]);

      // Draw dots
      dots.forEach((dot, i) => {
        ctx.beginPath();
        ctx.arc(dot.x, dot.y, 10, 0, Math.PI * 2);
        ctx.fillStyle = '#4F46E5';
        ctx.fill();
        ctx.fillStyle = '#fff';
        ctx.font = 'bold 12px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(i + 1, dot.x, dot.y);
      });
    };

    drawDots();

    const getPos = (e) => {
      const rect = canvas.getBoundingClientRect();
      const x = (e.clientX || e.touches?.[0]?.clientX) - rect.left;
      const y = (e.clientY || e.touches?.[0]?.clientY) - rect.top;
      return { x, y };
    };

    const startDrawing = (e) => {
      if (this.results.drawing) return;
      isDrawing = true;
      const pos = getPos(e);
      lastX = pos.x;
      lastY = pos.y;
      points = [{ x: pos.x, y: pos.y }];
      this.log(`DRAWING: Started at (${Math.round(pos.x)}, ${Math.round(pos.y)})`);
    };

    const draw = (e) => {
      if (!isDrawing || this.results.drawing) return;
      const pos = getPos(e);

      ctx.beginPath();
      ctx.strokeStyle = '#4F46E5';
      ctx.lineWidth = 3;
      ctx.lineCap = 'round';
      ctx.lineJoin = 'round';
      ctx.moveTo(lastX, lastY);
      ctx.lineTo(pos.x, pos.y);
      ctx.stroke();

      lastX = pos.x;
      lastY = pos.y;
      points.push({ x: pos.x, y: pos.y });
    };

    const stopDrawing = () => {
      if (!isDrawing) return;
      isDrawing = false;
      this.log(`DRAWING: Ended with ${points.length} points`);
    };

    // Mouse events
    canvas.addEventListener('mousedown', startDrawing);
    canvas.addEventListener('mousemove', draw);
    canvas.addEventListener('mouseup', stopDrawing);
    canvas.addEventListener('mouseleave', stopDrawing);

    // Touch events
    canvas.addEventListener('touchstart', (e) => {
      e.preventDefault();
      startDrawing(e);
    });
    canvas.addEventListener('touchmove', (e) => {
      e.preventDefault();
      draw(e);
    });
    canvas.addEventListener('touchend', stopDrawing);

    // Clear button
    clearBtn.addEventListener('click', () => {
      if (this.results.drawing) return;
      points = [];
      drawDots();
      status.textContent = '';
      this.log('DRAWING: Canvas cleared');
    });

    // Verify button
    verifyBtn.addEventListener('click', () => {
      if (this.results.drawing) return;

      // Check if user drew near the dots
      let dotsHit = 0;
      dots.forEach((dot, i) => {
        const hit = points.some(p => {
          const dx = p.x - dot.x;
          const dy = p.y - dot.y;
          return Math.sqrt(dx * dx + dy * dy) < 25;
        });
        if (hit) dotsHit++;
      });

      this.log(`DRAWING: Verified - ${dotsHit}/${dots.length} dots connected`);

      if (dotsHit >= 4) {
        this.results.drawing = true;
        status.textContent = 'Drawing verified!';
        status.className = 'canvas-status success';
        this.log('DRAWING: Verification successful');
        this.updateResults();
      } else {
        status.textContent = `Connect more dots (${dotsHit}/${dots.length} connected)`;
        status.className = 'canvas-status';
      }
    });
  }

  // ============================================
  // Challenge 4: Drag Reorder
  // ============================================
  initReorder() {
    const container = document.getElementById('reorderContainer');
    const status = document.getElementById('reorderStatus');
    const items = container.querySelectorAll('.reorder-item');

    let draggedItem = null;

    const checkOrder = () => {
      const currentItems = container.querySelectorAll('.reorder-item');
      const values = Array.from(currentItems).map(item => parseInt(item.dataset.value));
      const isCorrect = values.every((val, i) => val === i + 1);

      this.log(`REORDER: Current order: [${values.join(', ')}]`);

      if (isCorrect) {
        this.results.reorder = true;
        currentItems.forEach(item => item.classList.add('correct'));
        status.textContent = 'Items ordered correctly!';
        status.className = 'reorder-status success';
        this.log('REORDER: Verification successful');
        this.updateResults();
      }

      return isCorrect;
    };

    items.forEach(item => {
      item.addEventListener('dragstart', (e) => {
        if (this.results.reorder) {
          e.preventDefault();
          return;
        }
        draggedItem = item;
        item.classList.add('dragging');
        this.log(`REORDER: Started dragging item ${item.dataset.value}`);
      });

      item.addEventListener('dragend', () => {
        item.classList.remove('dragging');
        draggedItem = null;
        this.log('REORDER: Drag ended');
        checkOrder();
      });

      item.addEventListener('dragover', (e) => {
        e.preventDefault();
        if (this.results.reorder) return;

        if (draggedItem && draggedItem !== item) {
          const rect = item.getBoundingClientRect();
          const midX = rect.left + rect.width / 2;

          if (e.clientX < midX) {
            container.insertBefore(draggedItem, item);
          } else {
            container.insertBefore(draggedItem, item.nextSibling);
          }
        }
      });

      // Also support mouse drag (for programmatic testing)
      let isDragging = false;
      let startX = 0;

      item.addEventListener('mousedown', (e) => {
        if (this.results.reorder || e.target.getAttribute('draggable') === 'true') return;
        isDragging = true;
        startX = e.clientX;
        draggedItem = item;
        item.classList.add('dragging');
        this.log(`REORDER: Mouse drag started on item ${item.dataset.value}`);
      });
    });

    // Handle drops in gaps
    container.addEventListener('dragover', (e) => {
      e.preventDefault();
    });

    // Expose for programmatic testing
    window.reorderItems = (order) => {
      this.log(`REORDER: Programmatic reorder to [${order.join(', ')}]`);
      const items = Array.from(container.querySelectorAll('.reorder-item'));
      order.forEach((val, i) => {
        const item = items.find(it => parseInt(it.dataset.value) === val);
        if (item) container.appendChild(item);
      });
      checkOrder();
    };
  }
}

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
  window.challenges = new DragDropChallenges();
});
