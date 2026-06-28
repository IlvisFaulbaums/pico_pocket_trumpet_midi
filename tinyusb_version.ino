#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

int  in[128];
byte NoteV[13] = {8, 23, 40, 57, 76, 96, 116, 138, 162, 187, 213, 241, 255};
float f_peaks[5];
int Mic_pin;

const int POGA_1 = 2;
const int POGA_2 = 3;
const int POGA_3 = 4;

bool stavoklis1 = false, stavoklis2 = false, stavoklis3 = false;
bool ieprieks1 = false, ieprieks2 = false, ieprieks3 = false;

const byte harmonikas[8][7] = {
  {36, 48, 55, 60, 64, 67, 72}, // 000
  {33, 45, 52, 57, 61, 64, 69}, // 001
  {35, 47, 54, 59, 63, 66, 71}, // 010
  {32, 44, 51, 56, 60, 63, 68}, // 011
  {34, 46, 53, 58, 62, 65, 70}, // 100
  {31, 43, 50, 55, 59, 62, 67}, // 101
  {33, 45, 52, 57, 61, 64, 69}, // 110
  {30, 42, 49, 54, 58, 61, 66}  // 111
};

int lastMidiNote = -1;

// Dinamiskās filtrēšanas mainīgie
const float AMPLITUDE_THRESHOLD = 5.0; // zemāks slieksnis vājākām notīm

int candidateNote = -1;
int confirmCounter = 0;

void printNoteName(byte midiNote) {
  const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  Serial.print(names[midiNote % 12]);
  Serial.print(midiNote / 12 - 1);
}

void setup() {
  #if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
    TinyUSB_Device_Init(0);
  #endif
  MIDI.begin(MIDI_CHANNEL_OFF);
  Serial.begin(115200);
  analogReadResolution(12);
  Mic_pin = A0;
  pinMode(Mic_pin, INPUT);
  pinMode(POGA_1, INPUT_PULLUP);
  pinMode(POGA_2, INPUT_PULLUP);
  pinMode(POGA_3, INPUT_PULLUP);
}

void loop() {
  stavoklis1 = (digitalRead(POGA_1) == LOW);
  stavoklis2 = (digitalRead(POGA_2) == LOW);
  stavoklis3 = (digitalRead(POGA_3) == LOW);

  // Ventiļu maiņa – tūlīt izslēdzam noti un attīrām kandidātu
  if (stavoklis1 != ieprieks1 || stavoklis2 != ieprieks2 || stavoklis3 != ieprieks3) {
    if (lastMidiNote >= 0) {
      MIDI.sendNoteOff(lastMidiNote, 0, 1);
      lastMidiNote = -1;
    }
    ieprieks1 = stavoklis1; ieprieks2 = stavoklis2; ieprieks3 = stavoklis3;
    candidateNote = -1;
    confirmCounter = 0;
  }

  Tone_det();
  delay(10);
}

void Tone_det() {
  unsigned long a1, b;
  float a, sum1 = 0, sum2 = 0, sampling;

  a1 = micros();
  for (int i = 0; i < 128; i++) {
    a = (float)(analogRead(Mic_pin) - 2048);
    sum1 += a; sum2 += a * a;
    a = a * (sin(i * 3.14159 / 128) * sin(i * 3.14159 / 128));
    in[i] = (int)(10 * a);
    delayMicroseconds(195);
  }
  b = micros();
  sum1 /= 128;
  sum2 = sqrt(sum2 / 128);
  sampling = 128000000.0 / (b - a1);

  // Skaņas klātbūtne
  if (sum2 - sum1 > AMPLITUDE_THRESHOLD) {
    FFT(128, sampling);

    float max_peak_freq = f_peaks[0];
    if (max_peak_freq < 60) return;

    int detektetaNote = round(12.0 * log2(max_peak_freq / 440.0) + 69.0);
    int rakstitaNotis = detektetaNote + 2;

    byte comb = (stavoklis1 ? 4 : 0) | (stavoklis2 ? 2 : 0) | (stavoklis3 ? 1 : 0);

    int labakaNotis = -1;
    int mazakaStarpiba = 99;
    for (int n = 0; n < 7; n++) {
      int h = harmonikas[comb][n];
      int diff = abs(rakstitaNotis - h);
      if (diff < mazakaStarpiba) {
        mazakaStarpiba = diff;
        labakaNotis = h;
      }
    }

    if (labakaNotis >= 0 && mazakaStarpiba <= 2) {
      int concertMidi = labakaNotis - 2;

      // Ja šī pati nots jau skan, neko nedarām
      if (concertMidi == lastMidiNote) {
        candidateNote = -1;
        confirmCounter = 0;
        return;
      }

      // ------------------- DINAMISKAIS APSTIPRINĀJUMS -------------------
      // Nosakām, cik reižu jāapstiprina:
      // - mazs intervāls (≤6 pustoņi) -> 1 reize (tūlītēja)
      // - liels intervāls (≥7 pustoņi) -> 3 reizes (novērš oktāvu lēcienus)
      int requiredConfirms = 1;
      if (lastMidiNote != -1) {
        int interval = abs(concertMidi - lastMidiNote);
        if (interval >= 7) {
          requiredConfirms = 3;
        }
      }

      if (concertMidi == candidateNote) {
        confirmCounter++;
        if (confirmCounter >= requiredConfirms) {
          // Apstiprināta jauna nots – ieslēdzam
          if (lastMidiNote >= 0) MIDI.sendNoteOff(lastMidiNote, 0, 1);
          MIDI.sendNoteOn(concertMidi, 127, 1);
          Serial.print("Rakstītā: "); printNoteName(labakaNotis);
          Serial.print(" -> MIDI: "); Serial.println(concertMidi);
          lastMidiNote = concertMidi;
          candidateNote = -1;
          confirmCounter = 0;
        }
      } else {
        // Jauna kandidāta noteikšana
        candidateNote = concertMidi;
        confirmCounter = 1;
      }
      // ------------------------------------------------------------------
    } else {
      // Neatbilst harmonikai – nullejam kandidātu
      candidateNote = -1;
      confirmCounter = 0;
    }
  } else {
    // Skaņas nav – izslēdzam noti un attīrām kandidātu
    if (lastMidiNote >= 0) {
      MIDI.sendNoteOff(lastMidiNote, 0, 1);
      lastMidiNote = -1;
    }
    candidateNote = -1;
    confirmCounter = 0;
  }
}

// ======================= FFT (nemainīta) ====================================
float FFT(byte N, float Frequency) {
  byte data[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  int a, c1, f, o, x;
  a = N;

  for (int i = 0; i < 8; i++) {
    if (data[i] <= a) o = i;
  }
  o = 7;
  byte in_ps[data[o]] = {};
  float out_r[data[o]] = {};
  float out_im[data[o]] = {};

  x = 0;
  for (int b = 0; b < o; b++) {
    c1 = data[b];
    f = data[o] / (c1 + c1);
    for (int j = 0; j < c1; j++) {
      x = x + 1;
      in_ps[x] = in_ps[j] + f;
    }
  }

  for (int i = 0; i < data[o]; i++) {
    if (in_ps[i] < a) out_r[i] = in[in_ps[i]];
    if (in_ps[i] > a) out_r[i] = in[in_ps[i] - a];
  }

  int i10, i11, n1;
  float e, c, s, tr, ti;

  for (int i = 0; i < o; i++) {
    i10 = data[i];
    i11 = data[o] / data[i + 1];
    e = 6.283 / data[i + 1];
    e = 0 - e;
    n1 = 0;

    for (int j = 0; j < i10; j++) {
      c = cos(e * j);
      s = sin(e * j);
      n1 = j;

      for (int k = 0; k < i11; k++) {
        tr = c * out_r[i10 + n1] - s * out_im[i10 + n1];
        ti = s * out_r[i10 + n1] + c * out_im[i10 + n1];

        out_r[n1 + i10] = out_r[n1] - tr;
        out_r[n1] = out_r[n1] + tr;

        out_im[n1 + i10] = out_im[n1] - ti;
        out_im[n1] = out_im[n1] + ti;

        n1 = n1 + i10 + i10;
      }
    }
  }

  for (int i = 0; i < data[o - 1]; i++) {
    out_r[i] = sqrt((out_r[i] * out_r[i]) + (out_im[i] * out_im[i]));
    out_im[i] = (i * Frequency) / data[o];
  }

  x = 0;
  for (int i = 1; i < data[o - 1] - 1; i++) {
    if (out_r[i] > out_r[i - 1] && out_r[i] > out_r[i + 1]) {
      in_ps[x] = i;
      x = x + 1;
    }
  }

  float s_val = 0;
  int c_val = 0;
  for (int i = 0; i < x; i++) {
    for (int j = c_val; j < x; j++) {
      if (out_r[in_ps[i]] < out_r[in_ps[j]]) {
        s_val = in_ps[i];
        in_ps[i] = in_ps[j];
        in_ps[j] = (byte)s_val;
      }
    }
    c_val = c_val + 1;
  }

  for (int i = 0; i < 5; i++) {
    f_peaks[i] = (out_im[in_ps[i] - 1] * out_r[in_ps[i] - 1] +
                  out_im[in_ps[i]] * out_r[in_ps[i]] +
                  out_im[in_ps[i] + 1] * out_r[in_ps[i] + 1]) /
                 (out_r[in_ps[i] - 1] + out_r[in_ps[i]] + out_r[in_ps[i] + 1]);
  }
  return 0;
}
