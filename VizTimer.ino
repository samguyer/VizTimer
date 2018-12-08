#include <FastLED.h>
#include <Accelerometer.h>
#include <Buttons.h>

// ------------------------------------------------------------
//   Main configuration options

// -- Non-distracting mode
//    Just fade out each dot instead of animating it dropping
const bool NON_DISTRACTING = false;

// -- Arduino pins
#define LED_PIN 6
#define BUTTON_PIN 4
#define X_PIN A2
#define Y_PIN A1
#define Z_PIN A0
#define UNUSED_PIN 9

// -- Which axis is up/down?
#define ORIENT_UP Accelerometer::X_UP
#define ORIENT_DOWN Accelerometer::X_DOWN

// -- Number of LEDs in the strip
#define NUM_LEDS 30
float g_num_leds = (float) NUM_LEDS;

#define MAX_BRIGHTNESS 60

// -- Spectrum for the timer
//  Use 32 (green) to 97 (red)
#define START_HUE 35
#define END_HUE 100

// -- Spectrum for the finale
#define FINALE_TIME 30000
#define FINALE_FADE 5000

// ------------------------------------------------------------
//   Button configuration
//   1000 is the hold interval time
//        (when the user is holding the button down we get a
//         HOLD event every 1000 ms)
MomentaryButton button(BUTTON_PIN, 1000);

// ------------------------------------------------------------
//   Main timer state

// -- LED elements
CRGB leds[NUM_LEDS];

// --  Timer states
#define PROGRAM 1
#define STARTING 2
#define TIMING 3
#define PAUSED 4
#define FINALE 5
#define SLEEP 6

int g_timerState;

// -- Total amount of time programmed on the timer (ms)
//    Only changes when in PROGRAM mode
uint32_t g_total_time = 0;

// -- End time (in real time)
uint32_t g_end_time = 0;

// -- Time remaining until the end (ms)
//    This value is only used to hold the time remaining during a pause
uint32_t g_time_remaining = 0;

// -- Time per grain (just g_total_time/NUM_LEDS)
float g_time_per_grain = 0.0;

// -- Time for a grain to drop 1 unit (1 LED)
float g_time_per_drop = 0.0;

// -- Last total time used (for repeat timing)
uint32_t g_last_totalTime = 0;

// -- Generate a "tick" event every 10ms
uint32_t g_lastTimeEvent = 0;

// -- Time of start of pause
uint32_t g_timeOfPause = 0;

// -- Blink time (for programming)
uint32_t g_timeOfBlink = 0;

// --------------------------------------------------
//  Grain: the visual "unit" of the timer

// -- Table of values representing the overlap between two
//    circles of unit size

#define OVERLAP_INCR 0.125
float g_frac_overlap[] = { 1.000000, 0.920474, 0.841260, 0.762674,
                           0.685038, 0.608687, 0.533975, 0.461277,
                           0.391002, 0.323604, 0.259597, 0.199583,
                           0.144294, 0.094673, 0.052046, 0.018580, 0.000000,
                         };
class Grain
{
  public:
    enum State { Waiting, Dropping, Done };

  private:
    enum State m_state;

    // -- Initial position (does not change)
    float m_index;

    // -- Current location (changes as grain drops)
    float m_location;

    // -- Color
    uint8_t m_hue;

    // -- Time the grain should start dropping
    uint32_t m_droptime;

    // -- Velocity of drop
    float m_speed;

    // -- Acceleration of drop
    float m_acceleration;

  public:
    Grain()
      : m_state(Waiting),
        m_index(0.0),
        m_location(0.0),
        m_hue(0),
        m_droptime(0),
        m_speed(0.0),
        m_acceleration(0.0)
    {}

    // -- Set color of this grain
    void setHue(uint8_t hue) {
      m_hue = hue;
    }

    // -- Set time at which this grain will drop
    void setDropTime(uint32_t droptime) {
      m_droptime = droptime;
    }

    // -- Set the speed of the drop
    void setSpeed(float speed) {
      m_speed = speed;
    }

    // -- Set the acceleration
    void setAcceleration(float acc) {
      m_acceleration = acc;
    }

    // -- Initialize
    void init(int index)
    {
      // -- Reset state
      m_state = Waiting;

      // -- Index of this grain (does not change)
      m_index = (float) index;

      // -- Location: initially, just a float representation of index
      m_location = (float) index;
    }

    void update(uint32_t time_so_far)
    {
      if (m_state == Waiting) {
        if (time_so_far >= m_droptime)
          m_state = Dropping;
      }

      if (m_state == Dropping) {

        if (NON_DISTRACTING) {
          // -- Non-distracting mode: instead of animating the
          //    the dropping grains, just fade them out.
          m_location = -1.0;
        } else {
          // -- Regular animated mode

          // -- How long has this grain been dropping?
          //    Compute the value in seconds
          float T = ((float) (time_so_far - m_droptime)) / 1000.0;

          // -- Function:
          //    offset = a * t^2 + v * t
          //    offset = v * t
          // float offset = m_acceleration * T * T + m_speed * T;
          float offset = m_speed * T;

          //    First cut: simple linear version
          // float offset = 0.5 * since_drop / g_time_per_drop;
          // float offset = since_drop / g_time_per_drop;
          m_location = m_index - offset;
        }

        if (m_location < -0.5)
          m_state = Done;
      }
    }

    uint8_t brightness(float distance)
    {

      // -- NEW: use formula for overlapping circles
      //    Think of the dropping grain as a circle, then compute
      //    how much it would overlap with the top and bottom
      //    lights. The table above holds precomputed overlap
      //    area percentages for a fixed set of distances.

      // -- Compute distance from the top LED
      //    The 1.5 scale represents the fact that the LEDs are
      //    not right next to each other.
      float scaled_distance = distance * 1.2;
      int index = (int)(scaled_distance / OVERLAP_INCR);
      float frac = g_frac_overlap[index];
      uint8_t res = (uint8_t) (frac * 255.0);
      return res;
    }

    void render(uint8_t scaledown)
    {
      /*
        if (m_state == Waiting) {
        int pos = (int) m_location;
        CHSV hsv_top((uint8_t) m_hue, 250, scaledown);
        hsv2rgb_rainbow( hsv_top, leds[pos]);
        }

        if (m_state == Dropping) {
        // -- Dropping
        */

      // ---
      //  |
      //  +  3
      //  |    <-- 2.8
      // ---
      //  |
      //  +  2
      //  |
      // ---
      //
      // Distance from top = 0.2
      // Distance from bottom = 0.8

      /*
        float top_frac = m_location - floor(m_location);
        uint8_t top_bright = (uint8_t) (top_frac * 255.0);
        uint8_t bottom_bright = 255-top_bright;
      */

      // -- Animated dot with anti-aliasing
      float bottom_distance = m_location - floor(m_location);
      uint8_t bottom_bright = brightness(bottom_distance);
      int bottom_pixel = (int) floor(m_location);

      if (bottom_pixel >= 0) {
        leds[bottom_pixel] = ColorFromPalette(RainbowColors_p, m_hue);
        leds[bottom_pixel].nscale8_video(bottom_bright);
        // CHSV hsv_bottom((uint8_t) m_hue, 250, bottom_bright);
        // hsv2rgb_rainbow( hsv_bottom, leds[bottom_pixel]);
        if (scaledown < 255)
          leds[bottom_pixel].nscale8_video(scaledown);
      }
    }
};

// ============================================================
//   Global timer functions
//   Rendering and timer update
// ------------------------------------------------------------

// --------------------------------------------------
//   State of the timer

Grain g_Grains[NUM_LEDS];

// --------------------------------------------------
//   Programming mode

void renderProgram(bool show_cursor)
{
  // -- Show minutes
  int minutes = g_total_time / 60000;

  // -- For safety
  if (minutes >= NUM_LEDS) {
    minutes = NUM_LEDS - 1;
    g_total_time = minutes * 60000;
  }

  // -- Draw the pattern
  //  NEW: Go top-down
  int pos = NUM_LEDS - 1;
  for (int i = 0; i < minutes; i++) {
    if (i % 5 == 4) {
      leds[pos] = CRGB::White;
      leds[pos].nscale8_video(128);
    }
    else {
      CHSV hsv(180, 200, 255);
      hsv2rgb_rainbow( hsv, leds[pos]);
    }

    pos--;
    if (pos == 0) break;
  }

  // -- Show 15-second ticks
  uint32_t ms_minutes = minutes * 60000;
  int ticks = (g_total_time - ms_minutes) / 15000;
  for (int i = 0; i < ticks; i++) {
    CHSV hsv((uint8_t) 120, 200, 255);
    hsv2rgb_rainbow( hsv, leds[pos]);

    pos--;
    if (pos == 0) break;
  }


  // -- Show cursor, if requested
  if (show_cursor) {
    leds[pos] = CRGB::Yellow;
    leds[pos].nscale8_video(128);
    pos--;
  }

  // -- Color the rest black
  while (pos >= 0) {
    leds[pos] = CRGB::Black;
    pos--;
  }
}

// ------------------------------------------------------------
//   Timing mode

void renderStart()
{
}

void initTimer()
{
  // -- Set up a few global timing values
  g_time_per_grain = g_total_time / g_num_leds;
  g_time_per_drop = g_time_per_grain / g_num_leds;

  // -- Calculate the velocity of the grains
  //    (Currently, the same for all grains)
  //    time_per_grain = total_time / num_leds
  //    speed = num_leds / time_per_grain = (num_leds^2) / total_time;
  //    NOTE: units are leds/sec (not millisecond)
  float f_num_leds = ((float) g_num_leds);
  float f_total_time = ((float) g_total_time / 1000);
  float f_speed = (f_num_leds * f_num_leds) / f_total_time;

  for (int i = 0; i < NUM_LEDS; i++) {
    g_Grains[i].init(i);

    // -- Colors range from green (32) to red (97)
    g_Grains[i].setHue((uint8_t) ((i * (END_HUE - START_HUE)) / NUM_LEDS + START_HUE));

    // -- For Jonah
    // m_hue = random(40,200);

    // -- Calculate the time at which this grain should drop
    //    (relative to how long the timer has been running)
    g_Grains[i].setDropTime((g_total_time * (i + 1)) / (NUM_LEDS + 1));

    // -- Speed (as calculated above).
    g_Grains[i].setSpeed(f_speed * 0.75);

    // -- Fairly low acceleration (just to make sure there is always something going on)
    g_Grains[i].setAcceleration(4.0);
  }

  renderTimer(255);
}

void fadeTimer()
{
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].fadeToBlackBy(20);
  }
}

void renderTimer(uint8_t scaledown)
{
  // -- Re-render the active "grains"
  for (int i = NUM_LEDS - 1; i >= 0; i--) {
    g_Grains[i].render(scaledown);
  }
}

void renderOscillator(uint32_t atTime)
{
  // -- This is too distracting
  uint32_t per_pos = 1000 / NUM_LEDS;
  uint32_t pos = (atTime / per_pos);
  for (int i = 0; i < 5; i++) {
    uint32_t adj_pos = (pos - 5 + i);
    if (adj_pos >= 0) {
      uint32_t led_pos = adj_pos % (NUM_LEDS * 2);
      if (led_pos >= NUM_LEDS) {
        led_pos = NUM_LEDS - (led_pos - NUM_LEDS);
      }
      uint8_t brightness = (i + 1) * 10;
      leds[led_pos] += CRGB(0, 0, brightness);
    }
  }
}

void updateTimer(uint32_t atTime)
{
  for (int i = 0; i < NUM_LEDS; i++) {
    g_Grains[i].update(atTime);
  }
}

void renderPause(uint32_t timeSincePause)
{
  // -- Want a full wave (2pi radians) to take 2 seconds
  //    i.e., 2000ms/x = 6.282 ==> x = 320
  double f_time = (double) timeSincePause;
  double wave = cos(f_time / 320.0) + 1.0;
  // -- Scale the value from 0-2.0 to 0-255
  double d_scale = wave * 80.0 + 90.0;
  renderTimer((uint8_t) d_scale);
}

// ------------------------------------------------------------
//   Finale mode

// -- Storage for the finale light show
#define DISCO_HUE_RANGE 40
int g_Disco_start_hue = 0;
int g_Disco_end_hue = g_Disco_start_hue + DISCO_HUE_RANGE;
int disco_location[NUM_LEDS];
uint8_t disco_hue[NUM_LEDS];

void initFinale()
{
  // -- Random seed
  // Not very random: randomSeed(analogRead(UNUSED_PIN));
  randomSeed(millis());
  g_Disco_start_hue = random8();
  g_Disco_end_hue = g_Disco_start_hue + DISCO_HUE_RANGE;
  
  for (int i = 0; i < NUM_LEDS; i++) {
    // -- Random hues
    disco_hue[i] = random(g_Disco_start_hue, g_Disco_end_hue + 1);
    // -- Random starting points on the curve
    //    628 will be divided by 100.0 to get a number between 0 and 2pi
    disco_location[i] = random(1, 628);
  }
}

void renderFinale(uint32_t timeSinceEnd)
{
  uint32_t fade_scale = (FINALE_TIME - FINALE_FADE) / 250;
  
  for (int i = 0; i < NUM_LEDS; i++) {
    // -- Compute brightness
    double f_time = (double) timeSinceEnd;
    double offset = ((double) disco_location[i]) / 100.0;
    // -- Want a full wave to be 1/2 second
    //    500ms/x = 6.282 ==> x = 80
    double wave = cos(f_time / 160.0 + offset) + 1.0;
    // -- Scale from 0-2.0 to 0-250
    double w_scale = wave * 120.0 + 10;

    leds[i] = ColorFromPalette(RainbowColors_p, (uint8_t) disco_hue[i]);
    leds[i].nscale8_video(w_scale);
    // CHSV hsv((uint8_t) disco_hue[i], 250, (uint8_t) w_scale);
    // hsv2rgb_rainbow( hsv, leds[i]);

    if (w_scale < 12) {
      // -- Pick a new color
      disco_hue[i] = random(g_Disco_start_hue, g_Disco_end_hue + 1);
    }
    if (timeSinceEnd > FINALE_FADE) {
      uint32_t fade = (FINALE_TIME - timeSinceEnd) / fade_scale;
      leds[i].nscale8_video((uint8_t) fade);
    }
  }
}

// ------------------------------------------------------------
//   Accelerometer

Accelerometer accel(X_PIN, Y_PIN, Z_PIN);

Accelerometer::Orientation g_orientation = Accelerometer::NONE;

uint32_t g_lastOrientationTime = 0;

// -- Orientation events
#define NO_FLIP 0
#define FLIP_UP 1
#define FLIP_DOWN 2
#define FLIP_FLAT 3

int getOrientationEvent(uint32_t currentTime)
{
  int orientationEvent = NO_FLIP;

  // -- Only check every 50ms
  if (currentTime - g_lastOrientationTime > 50) {
    // -- Get current orientation
    Accelerometer::Orientation curOrientation = accel.getOrientation();

    // -- If it has changed...
    if (curOrientation != g_orientation) {
      if (curOrientation == ORIENT_UP) {
        orientationEvent = FLIP_UP;
      }
      else if (curOrientation == ORIENT_DOWN) {
        orientationEvent = FLIP_DOWN;
      }
      else {
        orientationEvent = FLIP_FLAT;
      }
    }

    g_orientation = curOrientation;
    g_lastOrientationTime = currentTime;
  }

  return orientationEvent;
}

// ============================================================
//   Set up
// ------------------------------------------------------------

void setup() 
{
  delay(500);
  Serial.begin(9600);

  // -- Create the LED entries
  FastLED.addLeds<WS2812, LED_PIN, RGB>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(MAX_BRIGHTNESS);

  // -- Set up the pins
  pinMode(LED_PIN, OUTPUT);

  // -- Initialize the button
  button.init();

  // -- Initial state
  g_total_time = 0;
  g_timerState = PROGRAM;
  g_lastTimeEvent = millis();
}

// ============================================================
//   Main loop
// ------------------------------------------------------------

void startTimer(uint32_t currentTime)
{
  // -- Start the timer
  if (g_total_time < 1000) {
    // -- No time entered? Reuse the last programmed time
    g_total_time = g_last_totalTime;
  }

  if (g_total_time == 0) {
    // -- Still no time? Do nothing
    g_timerState = PROGRAM;
    g_lastTimeEvent = currentTime;
  } else {
    // -- Init timer and start
    g_timerState = TIMING;
    g_end_time = currentTime + g_total_time;
    initTimer();
    g_last_totalTime = g_total_time;
  }
}

void pauseTimer(uint32_t currentTime)
{
  g_timerState = PAUSED;
  g_time_remaining = g_end_time - currentTime;
  g_timeOfPause = currentTime;
}

void resetTimer(uint32_t currentTime)
{
  g_timerState = PROGRAM;
  g_total_time = 0;
  g_lastTimeEvent = currentTime;
}

void sleepTimer(uint32_t currentTime)
{
  // -- All done - go back to program mode
  g_total_time = 0;
  g_timerState = SLEEP;
  g_lastTimeEvent = currentTime;
}

void loop()
{
  // -- Read time
  unsigned long currentTime = millis();

  // -- Tick of the timer
  bool TICK = false;
  if (currentTime - g_lastTimeEvent > 10) {
    TICK = true;
    g_lastTimeEvent = currentTime;
  }

  // -- Get any button event
  Button::EventKind buttonEvent = button.getEvent(currentTime);

  // -- Get any orientation event
  int orientationEvent = getOrientationEvent(currentTime);

  // -- Main state transition function

  // if (buttonEvent != Button::NONE) {

  bool show_cursor = false;

  // -- Handle the different button events, if any
  // Program + hold --> Add minutes (1 minute for each second held)
  // Program + click --> Add 15 seconds
  // Program + rotate --> Start timer
  // Timing + rotate -->
  //
  switch (g_timerState) {
    case PROGRAM:
      if (orientationEvent == FLIP_UP)
        startTimer(currentTime);

      // -- Button presses add time
      if (buttonEvent == Button::CLICK) {
        g_total_time += 15000;
      }

      if (buttonEvent == Button::HOLD) {
        g_total_time += 60000;
      }

      // -- Render the program
      //    With blinking cursor while waiting for user
      if (buttonEvent == Button::NONE) {
        uint32_t sinceLastBlink = currentTime - g_timeOfBlink;
        if (sinceLastBlink > 400) {
          show_cursor = true;
          if (sinceLastBlink > 800)
            g_timeOfBlink = currentTime;
        }
      }

      renderProgram(show_cursor);

      break;

    case TIMING:
      if (orientationEvent == FLIP_FLAT)
        pauseTimer(currentTime);

      if (orientationEvent == FLIP_DOWN)
        resetTimer(currentTime);

      // -- Time is up!
      if (currentTime > g_end_time) {
        g_timerState = FINALE;
        initFinale();
      } else {
        uint32_t atTime = g_total_time - (g_end_time - currentTime);

        // -- Update and fade
        if (TICK) {
          fadeTimer();
          updateTimer(atTime);
        }

        // -- Render
        renderTimer(255);
      }
      break;

    case PAUSED:
      if (orientationEvent == FLIP_UP) {
        // -- Continue the timer
        g_timerState = TIMING;
        g_end_time = currentTime + g_time_remaining;
      }
      if (orientationEvent == FLIP_DOWN)
        resetTimer(currentTime);

      // -- Fade rendering
      if (TICK)
        fadeTimer();

      // -- Render pause
      //    "Pulse" the leds
      renderPause(currentTime - g_timeOfPause);

      break;

    case FINALE:
      if (orientationEvent == FLIP_DOWN)
        resetTimer(currentTime);

      // -- Render the finale -- disco lights!
      if ((currentTime - g_end_time) > FINALE_TIME) {
        sleepTimer(currentTime);
      } else {
        // -- Render the finale
        if (TICK) {
          renderFinale(currentTime - g_end_time);
        }
      }

      break;

    case SLEEP:
      if (orientationEvent == FLIP_DOWN)
        resetTimer(currentTime);
      break;
  }
  //}

  // -- Last step: show the LEDS
  FastLED.show();
}
