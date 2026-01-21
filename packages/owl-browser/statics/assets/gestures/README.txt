Gesture Images for Virtual Camera
==================================

This directory should contain gesture images for reCAPTCHA hand gesture challenges.
The images should be named according to their gesture type.

Required Images:
----------------
- thumbs_up.png       - Thumbs up gesture
- thumbs_down.png     - Thumbs down gesture
- peace_sign.png      - Peace sign / V sign / Victory gesture
- open_palm.png       - Open palm / Stop gesture
- closed_fist.png     - Closed fist gesture
- pointing_up.png     - Finger pointing up
- pointing_left.png   - Finger pointing left
- pointing_right.png  - Finger pointing right
- ok_sign.png         - OK sign (thumb and index finger circle)
- wave.png            - Waving hand
- rock_on.png         - Rock on / Devil horns gesture
- call_me.png         - Phone/call me gesture
- pinch.png           - Pinching fingers together

Image Requirements:
------------------
- Format: PNG or JPEG
- Recommended size: 640x480 pixels minimum
- Clear, well-lit images of the hand gesture
- Neutral background preferred (solid color)
- Hand should be clearly visible and centered

Usage:
------
The VirtualCameraManager will load these images and use them as camera input
when a reCAPTCHA hand gesture challenge is detected. The system:
1. Detects the gesture challenge text (e.g., "Show thumbs up")
2. Parses the required gesture type
3. Loads the corresponding image
4. Feeds it to the virtual camera stream
